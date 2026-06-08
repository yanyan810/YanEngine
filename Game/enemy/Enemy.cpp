#include "Enemy.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>

static float CalcXMaxByZ_Boss(float z) {
	// 笘・・繧ｹ縺ｯ繝励Ξ繧､繝､繝ｼ繧医ｊ Z 遽・峇繧堤強縺・
	// 萓具ｼ壹・繝ｬ繧､繝､繝ｼ (-15..20) 竊・繝懊せ (-10..15)
	const float zNear = -10.0f;
	const float zFar = 15.0f;

	// 笘・蟷・ｂ蟆代＠迢ｭ繧√ｋ・亥･ｽ縺ｿ・・
	const float xMaxNear = 13.0f; // 謇句燕蛛ｴ
	const float xMaxFar = 17.0f; // 螂･蛛ｴ

	float t = (z - zNear) / (zFar - zNear);
	t = std::clamp(t, 0.0f, 1.0f);
	return xMaxNear + (xMaxFar - xMaxNear) * t;
}


struct AttackHitboxParam {
	float rangeX = 0.0f;  // 蜑阪↓蜃ｺ縺呵ｷ晞屬
	float liftY = 0.0f;  // 荳贋ｸ九が繝輔そ繝・ヨ・・縺ｧ荳翫・縺ｧ荳具ｼ・
	float hx = 0.8f;  // 蜊雁ｹ・
	float hy = 1.0f;  // 鬮倥＆・井ｸ狗ｫｯ縺九ｉ荳翫∈・・
	float hz = 0.6f;  // Z蜴壹∩・按ｱ・・
};

inline AABB MakeAttackHitboxAABB(
	const Vector3& attackerPos,
	int facing,                 // +1 right / -1 left
	const AttackHitboxParam& p
) {
	AABB hit{};
	const float cx = attackerPos.x + float(facing) * p.rangeX;

	hit.min = { cx - p.hx, attackerPos.y + p.liftY,       attackerPos.z - p.hz };
	hit.max = { cx + p.hx, attackerPos.y + p.liftY + p.hy, attackerPos.z + p.hz };
	return hit;
}

void Enemy::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam,
	EnemyType type, const Vector3& spawnXYZ) {
	type_ = type;
	alive_ = true;

	pos_ = { spawnXYZ.x, spawnXYZ.y, spawnXYZ.z };
	vel_ = { 0,0,0 };

	hitstun_ = false;
	hitstunTime_ = 0.0f;
	onGround_ = true;
	airborne_ = false;
	if (type_ == EnemyType::Boss) {
		maxHp_ = 400;
		hp_ = maxHp_;
		damageTaken_ = 1;
		bossAI_.Reset(maxHp_);
	} else if (type_ == EnemyType::Shooter) {
		maxHp_ = 20;
		hp_ = maxHp_;
		damageTaken_ = 1;
	} else {
		maxHp_ = 20;
		hp_ = maxHp_;
		damageTaken_ = 1;
	}



	model_ = std::make_unique<Object3d>();
	model_->Initialize(objCommon, dx);
	model_->SetCamera(cam);

	// 莉ｮ繝｢繝・Ν・医≠繧九ｂ縺ｮ縺ｫ蟾ｮ縺玲崛縺医※OK・・
	//model_->SetModel("cube/cube.obj");

	UpdateBody_();
	//UpdateModel_(dt);

	meleeState_ = MeleeState::Approach;
	shooterState_ = ShooterState::Retreat;

	meleeWindup_ = 0.0f;
	meleeAttack_ = 0.0f;
	shootWindup_ = 0.0f;

	requestMeleeAttack_ = false;
	requestShoot_ = false;
	shootDir_ = +1;
	shootMuzzlePos_ = pos_;

	debugHitboxCube_ = std::make_unique<Object3d>();
	debugHitboxCube_->Initialize(objCommon, dx);
	debugHitboxCube_->SetCamera(cam);
	//debugHitboxCube_->SetModel("cube/cube.obj");

	//繝懊せ逕ｨ
	meleeKind_ = MeleeKind::Normal;

	auto* mgr = ModelManager::GetInstance();

	if (type_ == EnemyType::Melee) {
		model_->SetModel("enemy/melee/melee.gltf");
		currentAnim_.clear();
		currentAnimLoop_ = true;
		ChangeAnimIfChanged_(meleeAnimIdle_, true);
		prevMeleeState_ = meleeState_;
	} else if (type_ == EnemyType::Shooter) {
		model_->SetModel("enemy/shooter/enemyShooter.gltf"); // 竊舌ヱ繧ｹ縺ｯ縺ゅ↑縺溘・resources讒区・縺ｫ蜷医ｏ縺帙※

		currentAnim_.clear();
		currentAnimLoop_ = true;

		// Idle・域律譛ｬ隱槫錐・峨ｒ繝ｫ繝ｼ繝・
		ChangeAnimIfChanged_(shooterAnimIdle_, true);

		prevShooterState_ = shooterState_;
	} else {
		// 笘・oss
		model_->SetModel("enemy/boss/boss.gltf");   // 竊・縺ゅ↑縺溘・ resources 讒区・縺ｫ蜷医ｏ縺帙※
		currentAnim_.clear();
		currentAnimLoop_ = true;

		// 蛻晄悄縺ｯ Idle
		ChangeAnimIfChanged_("Idle", true);
	}


	// Initialize縺ｮ譛蠕御ｻ倩ｿ托ｼ医Δ繝・Ν豎ｺ繧√◆蠕鯉ｼ・
	UpdateBody_();

	// 笘・・譛滉ｽ咲ｽｮ繧貞叉蜿肴丐・磯㍾隕・ｼ・
	UpdateModel_(0.0f);     // private縺ｪ繧峨√％縺薙□縺代・蜻ｼ縺ｹ繧倶ｽ咲ｽｮ縺ｪ縺ｮ縺ｧOK
	if (model_) model_->Update(0.0f);


}

void Enemy::ChangeAnimIfChanged_(const char* name, bool loop) {
	if (!model_ || !name) return;

	// 蜷後§縺ｪ繧我ｽ輔ｂ縺励↑縺・ｼ域ｯ弱ヵ繝ｬ繝ｼ繝繝ｪ繧ｹ繧ｿ繝ｼ繝磯亟豁｢・・
	if (currentAnim_ == name && currentAnimLoop_ == loop) return;

	currentAnim_ = name;
	currentAnimLoop_ = loop;
	model_->CrossFadeTo(currentAnim_.c_str(), 0.20f, loop);
}

void Enemy::StartOneShot_(const char* name, float lengthSec) {
	if (!model_ || !name) return;

	oneShotPlaying_ = true;
	oneShotTimer_ = lengthSec;
	oneShotLength_ = lengthSec;
	ChangeAnimIfChanged_(name, false);
}


void Enemy::Update(float dt, const Vector2& playerXY, float playerZ) {
	if (!alive_) return;

	// 笘・est逕ｨ・壼ｮ悟・蛛懈ｭ｢
	if (frozen_) {
		vel_ = { 0,0,0 };
		requestMeleeAttack_ = false;
		requestShoot_ = false;

		// 隕九◆逶ｮ縺ｨ蠖薙◆繧雁愛螳壹・譖ｴ譁ｰ縺励※縺翫￥・域判謦・愛螳壹′縺｡繧・ｓ縺ｨ蠖薙◆繧具ｼ・
		UpdateBody_();
		UpdateModel_(dt);
		return;
	}

	facing_ = (playerXY.x < pos_.x) ? -1 : +1;

	if (hitstunTime_ > 0.0f) {
		hitstunTime_ -= dt;
		if (hitstunTime_ <= 0.0f) hitstun_ = false;
	}

	if (meleeTimer_ > 0.0f) meleeTimer_ -= dt;
	//if (shootTimer_ > 0.0f) shootTimer_ -= dt;

// 蜈医↓ OneShot 繧ｿ繧､繝槭・縺縺鷹ｲ繧√ｋ・磯㍾隕・ｼ・
	if (oneShotPlaying_) {
		oneShotTimer_ -= dt;
		if (oneShotTimer_ <= 0.0f) {
			oneShotPlaying_ = false;
		}
	}

	// 笘・neShot荳ｭ縺ｯ遘ｻ蜍輔ｒ豁｢繧√ｋ・医％縺薙′閧晢ｼ・
	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.z = 0.0f;

		// 遨ｺ荳ｭ縺ｧ蜷ｹ縺埼｣帙ｓ縺ｧ繧区怙荳ｭ縺ｪ繧・Y 縺ｯ豁｢繧√↑縺・婿縺瑚・辟ｶ縺ｪ縺ｮ縺ｧ縲∝渕譛ｬ縺ｯ隗ｦ繧峨↑縺・
		// 縺溘□縲悟慍荳頑判謦・ｸｭ縺ｯ螳悟・蛛懈ｭ｢縲阪↓縺励◆縺・↑繧・onGround_ 縺ｮ譎ゅ□縺第ｭ｢繧√ｋ
		if (onGround_) {
			vel_.y = 0.0f;
		}
	}


	// AI縺ｯ縲薫neShot荳ｭ縺ｯ豁｢繧√ｋ縲・
	// 縺溘□縺唯oss縺ｯ萓句､悶↓縺励◆縺・↑繧画擅莉ｶ繧定ｪｿ謨ｴ
	if (!aiDisabled_ && !oneShotPlaying_) {
		if (!hitstun_ || type_ == EnemyType::Boss) {
			if (type_ == EnemyType::Melee) UpdateAI_Melee_(dt, playerXY, playerZ);
			else if (type_ == EnemyType::Shooter) UpdateAI_Shooter_(dt, playerXY, playerZ);
			else UpdateAI_Boss_(dt, playerXY, playerZ);
		}
	}

	// 笘・oss Rush荳ｭ縺ｮ蜷代″縺ｯ騾溷ｺｦ縺ｧ蝗ｺ螳夲ｼ・harge荳ｭ縺ｯ邯ｭ謖・ｼ・
	if (type_ == EnemyType::Boss) {
		const auto st = bossAI_.GetState();

		if (st == BossAI::State::Rush_ToRight || st == BossAI::State::Rush_Return) {
			facing_ = +1;
		} else if (st == BossAI::State::Rush_ExitLeft) {
			facing_ = -1;
		}
		// Rush_Charge 縺ｯ蜷代″邯ｭ謖・ｼ井ｽ輔ｂ縺励↑縺・ｼ・
	}


	// 笘・｢ｫ蠑ｾ繝輔Λ繝・す繝･譖ｴ譁ｰ
	if (hitFlashSec_ > 0.0f) {
		hitFlashSec_ -= dt;
		if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
	}

	// 笘・牡蜿肴丐・域ｯ弱ヵ繝ｬ繝ｼ繝縺ｧOK・・
	if (model_) {
		if (hitFlashSec_ > 0.0f) model_->SetMaterialColor(hitColor_);
		else                      model_->SetMaterialColor(normalColor_);
	}





	// 笘・黄逅・・蟶ｸ縺ｫ蝗槭☆・亥聖縺埼｣帙・縺溘＞縺ｮ縺ｧ・・
	ApplyPhysics_(dt);
	UpdateBody_();
	UpdateModel_(dt);
	SetLighting(light_);

	if (model_) model_->Update(dt);
	if (debugHitboxCube_) debugHitboxCube_->Update(dt);


}

void Enemy::Draw() {
	if (!alive_) return;
	if (!model_) return;

	if (hitFlashSec_ > 0.0f) model_->SetMaterialColor(hitColor_);
	else                      model_->SetMaterialColor(normalColor_);

	model_->Draw();
}


EnemyHitResult Enemy::ApplyHit2D(float knockVx, float launchVy, bool requestHitstun, int damage) {
	EnemyHitResult r{};
	if (!alive_) return r;
	r.hit = true;

	{
		char buf[256];
		sprintf_s(buf, "[ApplyHit2D] type=%d hp_before=%d damage=%d\n",
			(int)type_, hp_, damage);
		OutputDebugStringA(buf);
	}

	// =========================
// 笘・ム繝｡繝ｼ繧ｸ・・nvincible 縺ｪ繧画ｸ帙ｉ縺輔↑縺・ｼ・
// =========================
	if (!invincible_) {
		const int d = std::max(0, damage);
		hp_ -= d;

		if (hp_ <= 0) {
			hp_ = 0;
			alive_ = false;
			r.killed = true;
			return r;
		}
	}


	hitFlashSec_ = std::max(hitFlashSec_, 0.20f); // 0.2遘定ｵ､縺・

	if (type_ == EnemyType::Melee) {
		StartOneShot_(meleeAnimDamage_, 0.20f);
	} else if (type_ == EnemyType::Shooter) {
		StartOneShot_(shooterAnimDamage_, 0.20f);
	}


	// =========================
	// 笘・判謦・ｼ・I・峨Μ繧ｻ繝・ヨ・壽ｮｴ繧峨ｌ縺溘ｉ譛蛻昴°繧・
	// =========================
	meleeState_ = MeleeState::Approach;
	meleeWindup_ = 0.0f;
	meleeAttack_ = 0.0f;
	meleeTimer_ = 0.0f;
	requestMeleeAttack_ = false;

	shooterState_ = ShooterState::Retreat;
	shootWindup_ = 0.0f;
	shootTimer_ = 0.0f;
	requestShoot_ = false;

	// =========================
	// 笘・聖縺埼｣帙・縺暦ｼ医％縺薙・ invincible 縺ｧ繧ょ柑縺九○繧具ｼ・
	// =========================
	vel_.x = knockVx;
	vel_.y = launchVy;
	airborne_ = true;
	onGround_ = false;

	if (requestHitstun) {
		hitstun_ = true;
		hitstunTime_ = 0.40f;
	}

	return r;
}

void Enemy::UpdateAI_Melee_(float dt, const Vector2& playerXY, float playerZ) {
	const float dx = playerXY.x - pos_.x;
	const float adx = std::abs(dx);

	const float dz = playerZ - pos_.z;
	const float adz = std::abs(dz);

	// Z霑ｽ蠕難ｼ井ｻ翫・縺ｾ縺ｾ・・
	if (adz > zFollowDeadZone_) vel_.z = (dz > 0) ? depthSpeed_ : -depthSpeed_;
	else                       vel_.z = 0.0f;

	const bool inX = (adx <= meleeRangeX_);
	const bool inZ = (adz <= meleeRangeZ_);

	switch (meleeState_) {
	case MeleeState::Approach:
		// 笘・縺技縺ｩ縺｣縺｡縺九〒繧りｶｳ繧翫↑縺・↑繧峨瑚ｿ代▼縺上・
		if (!inX || !inZ) {
			vel_.x = (dx > 0) ? moveSpeed_ : -moveSpeed_;
		} else {
			// 笘・Z荳｡譁ｹOK縺ｮ譎ゅ□縺第判謦・ｺ門ｙ
			vel_.x = 0.0f;
			meleeWindup_ = meleeWindupTime_;
			meleeState_ = MeleeState::Windup;
		}
		break;

	case MeleeState::Windup:
		vel_.x = 0.0f;

		// 笘・ｺ懊ａ荳ｭ縺ｫ霍晞屬縺悟ｴｩ繧後◆繧峨く繝｣繝ｳ繧ｻ繝ｫ縺励※霑ｽ縺・峩縺呻ｼ磯㍾隕・ｼ・
		if (!inX || !inZ) {
			meleeState_ = MeleeState::Approach;
			break;
		}

		meleeWindup_ -= dt;
		if (meleeWindup_ <= 0.0f) {
			RequestMelee(MeleeKind::Normal);
			meleeAttack_ = meleeAttackTime_;
			meleeState_ = MeleeState::Attack;
		}
		break;

	case MeleeState::Attack:
		vel_.x = 0.0f;
		meleeAttack_ -= dt;
		if (meleeAttack_ <= 0.0f) {
			meleeTimer_ = meleeCooldown_;
			meleeState_ = MeleeState::Cooldown;
		}
		break;

	case MeleeState::Cooldown:
		vel_.x = 0.0f;
		if (meleeTimer_ <= 0.0f) {
			meleeState_ = MeleeState::Approach;
		}
		break;
	}
}

static float CalcXMaxByZ(float z) {
	const float zNear = -10.0f;
	const float zFar = 20.0f;
	const float xMaxNear = 15.0f;
	const float xMaxFar = 20.0f;

	float t = (z - zNear) / (zFar - zNear);
	t = std::clamp(t, 0.0f, 1.0f);
	return xMaxNear + (xMaxFar - xMaxNear) * t;
}

void Enemy::UpdateAI_Shooter_(float dt, const Vector2& playerXY, float playerZ)
{
	// 豁ｻ莠｡/縺ｲ繧九∩荳ｭ縺ｯ謦・◆縺ｪ縺・ｼ亥･ｽ縺ｿ縺ｧ・・
	if (!alive_) return;
	if (hitstun_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		return;
	}

	// 笘・ire/Damage 縺ｪ縺ｩ OneShot 荳ｭ縺ｯAI蛛懈ｭ｢・亥虚縺九↑縺・ｼ・
	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		vel_.z = 0.0f;
		return;
	}


	// 繝励Ξ繧､繝､繝ｼ譁ｹ蜷・
	const float dx = playerXY.x - pos_.x;
	const float dy = playerXY.y - pos_.y;

	// 蜷代″譖ｴ譁ｰ
	auto CalcFacingToPlayer = [&]() {
		return (playerXY.x < pos_.x) ? -1 : +1;
		};

	if (type_ != EnemyType::Boss) {
		facing_ = CalcFacingToPlayer();
	} else {
		// 笘・・繧ｹ縺ｯRush荳ｭ縺縺大髄縺阪ｒ蝗ｺ螳夲ｼ医ヱ繧ｫ繝代き髦ｲ豁｢・・
		const auto st = bossAI_.GetState();

		const bool isRush =
			(st == BossAI::State::Rush_ToRight) ||
			(st == BossAI::State::Rush_Charge) ||
			(st == BossAI::State::Rush_ExitLeft) ||
			(st == BossAI::State::Rush_Return);

		if (!isRush) {
			facing_ = CalcFacingToPlayer();
		}
		// isRush 縺ｮ譎ゅ・ facing_ 繧偵％縺薙〒縺ｯ譖ｴ譁ｰ縺励↑縺・
	}


	// =========================
// 笘・せ繝・・繧ｸ蠅・阜縺ｸ謌ｻ縺呻ｼ・/Z縺悟宛髯仙､悶↑繧峨∝・繧九∪縺ｧ遘ｻ蜍包ｼ・
// =========================
	{
		// Player縺ｨ蜷後§Z蛻ｶ髯・
		const float zNear = -10.0f;
		const float zFar = 20.0f;

		// Z縺ｯ繝励Ξ繧､繝､繝ｼ縺ｸ霑ｽ蠕薙＠縺ｦ繧九￠縺ｩ縲√∪縺壼宛髯仙・縺ｫ蜿弱ａ縺溘＞
		const float zClamped = std::clamp(pos_.z, zNear, zFar);

		// Z縺ｮ蛻ｶ髯仙､悶↑繧峨√∪縺啝繧貞宛髯仙・縺ｸ謚ｼ縺玲綾縺呻ｼ亥━蜈茨ｼ・
		if (pos_.z != zClamped) {
			const float targetZ = zClamped;
			const float dz = targetZ - pos_.z;
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
			vel_.x = 0.0f;
			vel_.y = 0.0f;
			return; // 笘・％縺ｮ繝輔Ξ繝ｼ繝縺ｯ縲梧綾繧九阪□縺・
		}

		// X蛻ｶ髯撰ｼ・hooter縺ｯ蟆代＠蜀・・縺ｫ蜈･繧後◆縺・↑繧・margin・・
		const float margin = 0.5f; // 0縺ｧ繧０K縲ょ・蛛ｴ縺ｫ蟇・○縺溘＞縺ｪ繧牙ｰ代＠
		const float xMax = CalcXMaxByZ(pos_.z) - margin;

		const float xClamped = std::clamp(pos_.x, -xMax, xMax);

		if (pos_.x != xClamped) {
			// X縺悟､悶↑繧峨∝｢・阜縺ｸ謌ｻ繧・
			const float targetX = xClamped;
			const float dxTo = targetX - pos_.x;

			// 騾溷ｺｦ縺ｧ謌ｻ縺呻ｼ・oveSpeed_ 繧剃ｽｿ縺・ｼ・
			vel_.x = (dxTo > 0.0f) ? moveSpeed_ : -moveSpeed_;
			vel_.y = 0.0f;
			// vel_.z 縺ｯ譌｢縺ｫ繝励Ξ繧､繝､繝ｼ霑ｽ蠕薙・險ｭ螳壹′縺ゅｋ縺ｪ繧峨◎縺ｮ縺ｾ縺ｾ縺ｧ繧０K
			// 縺溘□縺励梧綾繧句━蜈医阪↓縺励◆縺・↑繧・vel_.z = 0 縺ｫ縺励※繧り憶縺・
			return; // 笘・％縺ｮ繝輔Ξ繝ｼ繝縺ｯ縲梧綾繧九阪□縺・
		}
	}


	// 笘・霑ｽ蠕難ｼ医メ繝｣繝ｼ繧ｸ荳ｭ縺ｯ縺励↑縺・ｼ・
	if (shooterState_ != ShooterState::Windup) {
		const float dz = playerZ - pos_.z;
		if (std::abs(dz) > zFollowDeadZone_) {
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
		} else {
			vel_.z = 0.0f;
		}
	}


	// ShooterState・夂ｰ｡譏薙せ繝・・繝医・繧ｷ繝ｳ
	switch (shooterState_) {
	case ShooterState::Retreat:
	{
		// 笘・・繝ｬ繧､繝､繝ｼ縺瑚ｿ代▼縺・※繧ょ虚縺九↑縺・
		vel_.x = 0.0f;
		vel_.y = 0.0f;

		// 縺吶＄迢吶＞縺ｸ・医∪縺溘・Aim蝗ｺ螳壹〒繧０K・・
		shooterState_ = ShooterState::Aim;
	}
	break;



	case ShooterState::Aim:
	{
		// Y 縺後≠繧狗ｨ句ｺｦ謠・▲縺ｦ縺溘ｉ貅懊ａ縺ｸ・域純縺｣縺ｦ縺ｪ縺上※繧よ茶縺､縺ｪ繧画擅莉ｶ螟悶☆・・
		if (std::abs(dy) <= shooterAlignYEps_) {
			shooterState_ = ShooterState::Windup;
			shootWindup_ = shootWindupTime_;
		}

		vel_.x = 0.0f;
		vel_.y = 0.0f;
	}
	break;

	case ShooterState::Windup:
	{
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		vel_.z = 0.0f;

		shootWindup_ -= dt;
		if (shootWindup_ <= 0.0f) {

			// 逋ｺ蟆・ｦ∵ｱ・
			requestShoot_ = true;

			shootMuzzlePos_.x = pos_.x + 1.0f * float(facing_);
			shootMuzzlePos_.y = pos_.y + 0.8f;
			shootMuzzlePos_.z = pos_.z;

			shootDir_ = facing_;

			// 笘・％縺薙〒 Fire 繧帝幕蟋具ｼ育匱蟆・＠縺溽椪髢難ｼ・
			const float fireLen = 0.35f; // 隕九◆逶ｮ縺ｧ隱ｿ謨ｴ・・.25縺縺ｨ遏ｭ縺・°繧ゑｼ・
			StartOneShot_(shooterAnimFire_, fireLen);

			// 笘・ooldown 縺ｯ Fire 繧医ｊ遏ｭ縺上＠縺ｪ縺・ｼ医％縺楢ｶ・㍾隕・ｼ・
			shooterState_ = ShooterState::Cooldown;
			shootTimer_ = std::max(shootCooldown_, fireLen);
		}
	}
	break;


	case ShooterState::Cooldown:
	{
		vel_.x = 0.0f;
		vel_.y = 0.0f;

		shootTimer_ -= dt;
		if (shootTimer_ <= 0.0f) {
			shooterState_ = ShooterState::Retreat;
		}
	}
	break;
	}
}



void Enemy::UpdateAI_Boss_(float dt, const Vector2& playerXY, float playerZ) {
	bossAI_.Update(*this, dt, playerXY, playerZ);
}


void Enemy::ApplyPhysics_(float dt) {
	if (!onGround_) {
		vel_.y -= gravity_ * dt;
	}

	pos_.x += vel_.x * dt;
	pos_.y += vel_.y * dt;
	pos_.z += vel_.z * dt;

	// 蝨ｰ髱｢
	if (pos_.y <= 0.0f) {
		pos_.y = 0.0f;
		vel_.y = 0.0f;
		onGround_ = true;
		airborne_ = false;
	}

	// 笘・・繧ｹ縺縺托ｼ壹せ繝・・繧ｸ遽・峇縺ｫ繧ｯ繝ｩ繝ｳ繝暦ｼ・遽・峇縺ｯ繝励Ξ繧､繝､繝ｼ繧医ｊ迢ｭ縺擾ｼ・
	if (type_ == EnemyType::Boss) {
		const float zNearBoss = -10.0f;
		const float zFarBoss = 15.0f;

		pos_.z = std::clamp(pos_.z, zNearBoss, zFarBoss);

		if ((pos_.z <= zNearBoss && vel_.z < 0.0f) || (pos_.z >= zFarBoss && vel_.z > 0.0f)) {
			vel_.z = 0.0f;
		}
	}
}


void Enemy::UpdateBody_() {
	// 雜ｳ蜈・渕貅悶・邁｡譏鄭ABB・医・繧ｹ縺ｯ螟ｧ縺阪￥・・
	float hx = 0.4f, hy = 0.75f, hz = 0.6f;         // 笘・z霑ｽ蜉
	if (type_ == EnemyType::Boss) { hx = 1.2f; hy = 2.0f; hz = 1.4f; }

	body_.min = { pos_.x - hx, pos_.y,           pos_.z - hz };   // 笘・繧Ｑos_.z蝓ｺ貅・
	body_.max = { pos_.x + hx, pos_.y + hy * 2.0f, pos_.z + hz }; // 笘・繧Ｑos_.z蝓ｺ貅・
}

void Enemy::UpdateModel_(float dt) {
	if (!model_) return;

	model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

	float flipX = (facing_ > 0) ? 1.0f : -1.0f;

	// 笘・oss.gltf 縺後碁・髄縺阪阪↑繧芽ｦ九◆逶ｮ縺縺大渚霆｢陬懈ｭ｣
	if (type_ == EnemyType::Boss) {
		flipX *= -1.0f;
	}

	if (type_ == EnemyType::Boss) model_->SetScale({ 2.0f * flipX, 2.0f, 2.0f });
	else                         model_->SetScale({ 1.0f * flipX, 1.0f, 1.0f });

	// ===== Melee 縺ｯ縲後い繝九Γ縲阪〒蛻・崛・医Δ繝・Ν蟾ｮ縺玲崛縺医＠縺ｪ縺・ｼ・====
	if (type_ == EnemyType::Melee) {

		// 謾ｻ謦・ｸｭ蛻､螳夲ｼ医≠縺ｪ縺溘・譌｢蟄倥Ο繧ｸ繝・け・・
		const bool isAttacking =
			(meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack);

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		// ---- 1) OneShot 蜀咲函荳ｭ・域判謦・陲ｫ蠑ｾ縺ｪ縺ｩ・峨↑繧画凾髢薙〒邨ゅｏ繧峨○繧・
		//if (oneShotPlaying_) {
		//	oneShotTimer_ -= dt;
		//	if (oneShotTimer_ <= 0.0f) {
		//		oneShotPlaying_ = false;
		//	} else {
		//		// OneShot荳ｭ縺ｯ莉悶↓蛻・ｊ譖ｿ縺医↑縺・
		//		return;
		//	}
		//}

		if (oneShotPlaying_) {
			// 繧ｿ繧､繝槭・貂帷ｮ励・ Update() 蛛ｴ縺縺代〒繧・ｋ
			return; // OneShot荳ｭ縺ｯ莉悶↓蛻・ｊ譖ｿ縺医↑縺・
		}


		// ---- 2) 迥ｶ諷矩・遘ｻ縺ｮ迸ｬ髢薙□縺・Attack 繧帝幕蟋具ｼ域ｯ弱ヵ繝ｬ繝ｼ繝髢句ｧ九＠縺ｪ縺・ｼ・
		if ((meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack) &&
			!(prevMeleeState_ == MeleeState::Windup || prevMeleeState_ == MeleeState::Attack)) {

			const float atkLen = float(kMeleeAttackFrames_) / kAnimFps_; // 40f繧堤ｧ偵∈
			StartOneShot_(meleeAnimAttack_, atkLen);


			// 笘・％縺薙〒蜊ｳreturn・壹％縺ｮ繝輔Ξ繝ｼ繝縺ｧIdle/Walk縺ｫ荳頑嶌縺阪＠縺ｪ縺・
			prevMeleeState_ = meleeState_;
			return;
		}
		prevMeleeState_ = meleeState_;


		// ---- 3) 騾壼ｸｸ譎ゑｼ啗alk / Idle
		if (isMoving) {
			ChangeAnimIfChanged_(meleeAnimWalk_, true);
		} else {
			ChangeAnimIfChanged_(meleeAnimIdle_, true);
		}

		return;
	}

	// ===== Shooter 縺ｯ縲後い繝九Γ縲阪〒蛻・崛・医Δ繝・Ν蟾ｮ縺玲崛縺医＠縺ｪ縺・ｼ・====
	if (type_ == EnemyType::Shooter) {

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		// ---- 1) OneShot 蜀咲函荳ｭ縺ｯ莉悶∈蛻・ｊ譖ｿ縺医↑縺・
	// ---- 1) OneShot 蜀咲函荳ｭ縺ｯ莉悶∈蛻・ｊ譖ｿ縺医↑縺・ｼ医ち繧､繝槭・貂帷ｮ励・ Update() 蛛ｴ・・
		if (oneShotPlaying_) {
			return;
		}

		// ---- 2) 迥ｶ諷九↓蠢懊§縺ｦ繧｢繝九Γ豎ｺ繧√ｋ
		switch (shooterState_) {
		case ShooterState::Windup:
			// 貅懊ａ・咾harge・医Ν繝ｼ繝暦ｼ・
			ChangeAnimIfChanged_(shooterAnimCharge_, true);
			break;

		case ShooterState::Cooldown:
		case ShooterState::Aim:
		case ShooterState::Retreat:
		default:
			// 縺昴ｌ莉･螟厄ｼ壼虚縺・※縺溘ｉWalk / 豁｢縺ｾ縺｣縺ｦ縺溘ｉIdle
			if (isMoving) ChangeAnimIfChanged_(shooterAnimWalk_, true);
			else          ChangeAnimIfChanged_(shooterAnimIdle_, true);
			break;
		}

		//// ---- 3) 縲梧茶縺､迸ｬ髢薙阪↓ Fire 繧・1蝗槭□縺大・逕・
		//// Windup -> Cooldown 縺ｫ驕ｷ遘ｻ縺励◆迸ｬ髢薙ｒ縲檎匱蟆・肴桶縺・↓縺吶ｋ
		//if (prevShooterState_ == ShooterState::Windup &&
		//	shooterState_ == ShooterState::Cooldown) {

		//	// Fire縺ｮ髟ｷ縺輔・莉ｮ縲りｦ九◆逶ｮ縺ｧ隱ｿ謨ｴ
		//	StartOneShot_(shooterAnimFire_, 0.25f);
		//}

		prevShooterState_ = shooterState_;
		return;
	}


	// ===== Boss 縺ｯ glTF 繧｢繝九Γ縺ｧ蛻・崛 =====
	if (type_ == EnemyType::Boss) {

		// 笘・neShot荳ｭ縺ｯ莉悶↓蛻・ｊ譖ｿ縺医↑縺・ｼ・elee縺ｨ蜷後§譁ｹ驥晢ｼ・
		if (oneShotPlaying_) {
			return;
		}

		// BossAI 縺ｮ State 繧定ｦ九※蛻・ｊ譖ｿ縺医ｋ
		// 窶ｻBossAI::GetState() 繧定ｿｽ蜉縺励◆蜑肴署
		const auto st = bossAI_.GetState();

		switch (st) {
		case BossAI::State::Wander: {
			// Wander荳ｭ縺ｯ遘ｻ蜍輔＠縺ｦ繧九↑繧・Walk縲∵ｭ｢縺ｾ縺｣縺ｦ繧九↑繧・Idle
			const float moveEps = 0.05f;
			const bool isMoving =
				(std::abs(vel_.x) > moveEps) ||
				(std::abs(vel_.z) > moveEps) ||
				(std::abs(vel_.y) > moveEps);

			if (isMoving) ChangeAnimIfChanged_("Walk", true);
			else          ChangeAnimIfChanged_("Idle", true);
		} break;

		case BossAI::State::Drop_Windup:
			ChangeAnimIfChanged_("Drop_Windup", true); // 繝ｫ繝ｼ繝励〒OK
			break;
		case BossAI::State::Drop_Fall:
			ChangeAnimIfChanged_("Drop_Fall", true);
			break;
		case BossAI::State::Drop_Land:
			ChangeAnimIfChanged_("Drop_Land", false);  // 1蝗槭▲縺ｽ縺上＠縺溘＞縺ｪ繧・false
			break;

		case BossAI::State::Melee_Dash:
			ChangeAnimIfChanged_("Melee_Dash", true);
			break;
		case BossAI::State::Melee_Attack:
			ChangeAnimIfChanged_("Melee_Attack", false);
			break;
		case BossAI::State::Melee_Recover:
			ChangeAnimIfChanged_("Idle", true);
			break;

		case BossAI::State::Rush_ToRight:
			ChangeAnimIfChanged_("Rush_ToRight", true);
			break;
		case BossAI::State::Rush_Charge:
			ChangeAnimIfChanged_("Rush_Charge", true);
			break;
		case BossAI::State::Rush_ExitLeft:
			ChangeAnimIfChanged_("Rush_ExitLeft", true);
			break;
		case BossAI::State::Rush_Return:
			ChangeAnimIfChanged_("Walk", true);
			break;

		case BossAI::State::Super50:
		case BossAI::State::Super25:
			// 縺ｨ繧翫≠縺医★貅懊ａ貍泌・・扞dle
			ChangeAnimIfChanged_("Idle", true);
			break;
		}

		return;
	}





}


