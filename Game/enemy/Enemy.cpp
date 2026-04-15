#include "Enemy.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>

static float CalcXMaxByZ_Boss(float z) {
	// ★ボスはプレイヤーより Z 範囲を狭く
	// 例：プレイヤー (-15..20) → ボス (-10..15)
	const float zNear = -10.0f;
	const float zFar = 15.0f;

	// ★X幅も少し狭める（好み）
	const float xMaxNear = 13.0f; // 手前側
	const float xMaxFar = 17.0f; // 奥側

	float t = (z - zNear) / (zFar - zNear);
	t = std::clamp(t, 0.0f, 1.0f);
	return xMaxNear + (xMaxFar - xMaxNear) * t;
}


struct AttackHitboxParam {
	float rangeX = 0.0f;  // 前に出す距離
	float liftY = 0.0f;  // 上下オフセット（+で上、-で下）
	float hx = 0.8f;  // 半幅
	float hy = 1.0f;  // 高さ（下端から上へ）
	float hz = 0.6f;  // Z厚み（±）
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

inline bool Intersect3(const EnemyManager::AABB3& a, const EnemyManager::AABB3& b) {
	return (std::abs(a.x - b.x) <= (a.hx + b.hx)) &&
		(std::abs(a.y - b.y) <= (a.hy + b.hy)) &&
		(std::abs(a.z - b.z) <= (a.hz + b.hz));
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

	// 仮モデル（あるものに差し替えてOK）
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

	//ボス用
	meleeKind_ = MeleeKind::Normal;

	auto* mgr = ModelManager::GetInstance();

	if (type_ == EnemyType::Melee) {
		model_->SetModel("enemy/melee/melee.gltf");
		currentAnim_.clear();
		currentAnimLoop_ = true;
		ChangeAnimIfChanged_(meleeAnimIdle_, true);
		prevMeleeState_ = meleeState_;
	} else if (type_ == EnemyType::Shooter) {
		model_->SetModel("enemy/shooter/enemyShooter.gltf"); // ←パスはあなたのresources構成に合わせて

		currentAnim_.clear();
		currentAnimLoop_ = true;

		// Idle（日本語名）をループ
		ChangeAnimIfChanged_(shooterAnimIdle_, true);

		prevShooterState_ = shooterState_;
	} else {
		// ★Boss
		model_->SetModel("enemy/boss/boss.gltf");   // ← あなたの resources 構成に合わせて
		currentAnim_.clear();
		currentAnimLoop_ = true;

		// 初期は Idle
		ChangeAnimIfChanged_("Idle", true);
	}


	// Initializeの最後付近（モデル決めた後）
	UpdateBody_();

	// ★初期位置を即反映（重要）
	UpdateModel_(0.0f);     // privateなら、ここだけは呼べる位置なのでOK
	if (model_) model_->Update(0.0f);


}

void Enemy::ChangeAnimIfChanged_(const char* name, bool loop) {
	if (!model_ || !name) return;

	// 同じなら何もしない（毎フレームリスタート防止）
	if (currentAnim_ == name && currentAnimLoop_ == loop) return;

	currentAnim_ = name;
	currentAnimLoop_ = loop;
	model_->PlayAnimation(currentAnim_.c_str(), loop);
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

	// ★Test用：完全停止
	if (frozen_) {
		vel_ = { 0,0,0 };
		requestMeleeAttack_ = false;
		requestShoot_ = false;

		// 見た目と当たり判定は更新しておく（攻撃判定がちゃんと当たる）
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

// 先に OneShot タイマーだけ進める（重要）
	if (oneShotPlaying_) {
		oneShotTimer_ -= dt;
		if (oneShotTimer_ <= 0.0f) {
			oneShotPlaying_ = false;
		}
	}

	// ★OneShot中は移動を止める（ここが肝）
	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.z = 0.0f;

		// 空中で吹き飛んでる最中なら Y は止めない方が自然なので、基本は触らない
		// ただ「地上攻撃中は完全停止」にしたいなら onGround_ の時だけ止める
		if (onGround_) {
			vel_.y = 0.0f;
		}
	}


	// AIは「OneShot中は止める」
	// ただしBossは例外にしたいなら条件を調整
	if (!aiDisabled_ && !oneShotPlaying_) {
		if (!hitstun_ || type_ == EnemyType::Boss) {
			if (type_ == EnemyType::Melee) UpdateAI_Melee_(dt, playerXY, playerZ);
			else if (type_ == EnemyType::Shooter) UpdateAI_Shooter_(dt, playerXY, playerZ);
			else UpdateAI_Boss_(dt, playerXY, playerZ);
		}
	}

	// ★Boss Rush中の向きは速度で固定（Charge中は維持）
	if (type_ == EnemyType::Boss) {
		const auto st = bossAI_.GetState();

		if (st == BossAI::State::Rush_ToRight || st == BossAI::State::Rush_Return) {
			facing_ = +1;
		} else if (st == BossAI::State::Rush_ExitLeft) {
			facing_ = -1;
		}
		// Rush_Charge は向き維持（何もしない）
	}


	// ★被弾フラッシュ更新
	if (hitFlashSec_ > 0.0f) {
		hitFlashSec_ -= dt;
		if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
	}

	// ★色反映（毎フレームでOK）
	if (model_) {
		if (hitFlashSec_ > 0.0f) model_->SetMaterialColor(hitColor_);
		else                      model_->SetMaterialColor(normalColor_);
	}





	// ★物理は常に回す（吹き飛びたいので）
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
// ★ダメージ（invincible なら減らさない）
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


	hitFlashSec_ = std::max(hitFlashSec_, 0.20f); // 0.2秒赤く

	if (type_ == EnemyType::Melee) {
		StartOneShot_(meleeAnimDamage_, 0.20f);
	} else if (type_ == EnemyType::Shooter) {
		StartOneShot_(shooterAnimDamage_, 0.20f);
	}


	// =========================
	// ★攻撃（AI）リセット：殴られたら最初から
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
	// ★吹き飛ばし（ここは invincible でも効かせる）
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

	// Z追従（今のまま）
	if (adz > zFollowDeadZone_) vel_.z = (dz > 0) ? depthSpeed_ : -depthSpeed_;
	else                       vel_.z = 0.0f;

	const bool inX = (adx <= meleeRangeX_);
	const bool inZ = (adz <= meleeRangeZ_);

	switch (meleeState_) {
	case MeleeState::Approach:
		// ★XかZどっちかでも足りないなら「近づく」
		if (!inX || !inZ) {
			vel_.x = (dx > 0) ? moveSpeed_ : -moveSpeed_;
		} else {
			// ★XZ両方OKの時だけ攻撃準備
			vel_.x = 0.0f;
			meleeWindup_ = meleeWindupTime_;
			meleeState_ = MeleeState::Windup;
		}
		break;

	case MeleeState::Windup:
		vel_.x = 0.0f;

		// ★溜め中に距離が崩れたらキャンセルして追い直す（重要）
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
	// 死亡/ひるみ中は撃たない（好みで）
	if (!alive_) return;
	if (hitstun_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		return;
	}

	// ★Fire/Damage など OneShot 中はAI停止（動かない）
	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		vel_.z = 0.0f;
		return;
	}


	// プレイヤー方向
	const float dx = playerXY.x - pos_.x;
	const float dy = playerXY.y - pos_.y;

	// 向き更新
	auto CalcFacingToPlayer = [&]() {
		return (playerXY.x < pos_.x) ? -1 : +1;
		};

	if (type_ != EnemyType::Boss) {
		facing_ = CalcFacingToPlayer();
	} else {
		// ★ボスはRush中だけ向きを固定（パカパカ防止）
		const auto st = bossAI_.GetState();

		const bool isRush =
			(st == BossAI::State::Rush_ToRight) ||
			(st == BossAI::State::Rush_Charge) ||
			(st == BossAI::State::Rush_ExitLeft) ||
			(st == BossAI::State::Rush_Return);

		if (!isRush) {
			facing_ = CalcFacingToPlayer();
		}
		// isRush の時は facing_ をここでは更新しない
	}


	// =========================
// ★ステージ境界へ戻す（X/Zが制限外なら、入るまで移動）
// =========================
	{
		// Playerと同じZ制限
		const float zNear = -10.0f;
		const float zFar = 20.0f;

		// Zはプレイヤーへ追従してるけど、まず制限内に収めたい
		const float zClamped = std::clamp(pos_.z, zNear, zFar);

		// Zの制限外なら、まずZを制限内へ押し戻す（優先）
		if (pos_.z != zClamped) {
			const float targetZ = zClamped;
			const float dz = targetZ - pos_.z;
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
			vel_.x = 0.0f;
			vel_.y = 0.0f;
			return; // ★このフレームは「戻る」だけ
		}

		// X制限（Shooterは少し内側に入れたいなら margin）
		const float margin = 0.5f; // 0でもOK。内側に寄せたいなら少し
		const float xMax = CalcXMaxByZ(pos_.z) - margin;

		const float xClamped = std::clamp(pos_.x, -xMax, xMax);

		if (pos_.x != xClamped) {
			// Xが外なら、境界へ戻る
			const float targetX = xClamped;
			const float dxTo = targetX - pos_.x;

			// 速度で戻す（moveSpeed_ を使う）
			vel_.x = (dxTo > 0.0f) ? moveSpeed_ : -moveSpeed_;
			vel_.y = 0.0f;
			// vel_.z は既にプレイヤー追従の設定があるならそのままでもOK
			// ただし「戻る優先」にしたいなら vel_.z = 0 にしても良い
			return; // ★このフレームは「戻る」だけ
		}
	}


	// ★Z追従（チャージ中はしない）
	if (shooterState_ != ShooterState::Windup) {
		const float dz = playerZ - pos_.z;
		if (std::abs(dz) > zFollowDeadZone_) {
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
		} else {
			vel_.z = 0.0f;
		}
	}


	// ShooterState：簡易ステートマシン
	switch (shooterState_) {
	case ShooterState::Retreat:
	{
		// ★プレイヤーが近づいても動かない
		vel_.x = 0.0f;
		vel_.y = 0.0f;

		// すぐ狙いへ（またはAim固定でもOK）
		shooterState_ = ShooterState::Aim;
	}
	break;



	case ShooterState::Aim:
	{
		// Y がある程度揃ってたら溜めへ（揃ってなくても撃つなら条件外す）
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

			// 発射要求
			requestShoot_ = true;

			shootMuzzlePos_.x = pos_.x + 1.0f * float(facing_);
			shootMuzzlePos_.y = pos_.y + 0.8f;
			shootMuzzlePos_.z = pos_.z;

			shootDir_ = facing_;

			// ★ここで Fire を開始（発射した瞬間）
			const float fireLen = 0.35f; // 見た目で調整（0.25だと短いかも）
			StartOneShot_(shooterAnimFire_, fireLen);

			// ★Cooldown は Fire より短くしない（ここ超重要）
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

	// 地面
	if (pos_.y <= 0.0f) {
		pos_.y = 0.0f;
		vel_.y = 0.0f;
		onGround_ = true;
		airborne_ = false;
	}

	// ★ボスだけ：ステージ範囲にクランプ（Z範囲はプレイヤーより狭く）
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
	// 足元基準の簡易AABB（ボスは大きく）
	float hx = 0.4f, hy = 0.75f, hz = 0.6f;         // ★hz追加
	if (type_ == EnemyType::Boss) { hx = 1.2f; hy = 2.0f; hz = 1.4f; }

	body_.min = { pos_.x - hx, pos_.y,           pos_.z - hz };   // ★Zもpos_.z基準
	body_.max = { pos_.x + hx, pos_.y + hy * 2.0f, pos_.z + hz }; // ★Zもpos_.z基準
}

void Enemy::UpdateModel_(float dt) {
	if (!model_) return;

	model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

	float flipX = (facing_ > 0) ? 1.0f : -1.0f;

	// ★boss.gltf が「逆向き」なら見た目だけ反転補正
	if (type_ == EnemyType::Boss) {
		flipX *= -1.0f;
	}

	if (type_ == EnemyType::Boss) model_->SetScale({ 2.0f * flipX, 2.0f, 2.0f });
	else                         model_->SetScale({ 1.0f * flipX, 1.0f, 1.0f });

	// ===== Melee は「アニメ」で切替（モデル差し替えしない）=====
	if (type_ == EnemyType::Melee) {

		// 攻撃中判定（あなたの既存ロジック）
		const bool isAttacking =
			(meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack);

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		// ---- 1) OneShot 再生中（攻撃/被弾など）なら時間で終わらせる
		//if (oneShotPlaying_) {
		//	oneShotTimer_ -= dt;
		//	if (oneShotTimer_ <= 0.0f) {
		//		oneShotPlaying_ = false;
		//	} else {
		//		// OneShot中は他に切り替えない
		//		return;
		//	}
		//}

		if (oneShotPlaying_) {
			// タイマー減算は Update() 側だけでやる
			return; // OneShot中は他に切り替えない
		}


		// ---- 2) 状態遷移の瞬間だけ Attack を開始（毎フレーム開始しない）
		if ((meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack) &&
			!(prevMeleeState_ == MeleeState::Windup || prevMeleeState_ == MeleeState::Attack)) {

			const float atkLen = float(kMeleeAttackFrames_) / kAnimFps_; // 40fを秒へ
			StartOneShot_(meleeAnimAttack_, atkLen);


			// ★ここで即return：このフレームでIdle/Walkに上書きしない
			prevMeleeState_ = meleeState_;
			return;
		}
		prevMeleeState_ = meleeState_;


		// ---- 3) 通常時：Walk / Idle
		if (isMoving) {
			ChangeAnimIfChanged_(meleeAnimWalk_, true);
		} else {
			ChangeAnimIfChanged_(meleeAnimIdle_, true);
		}

		return;
	}

	// ===== Shooter は「アニメ」で切替（モデル差し替えしない）=====
	if (type_ == EnemyType::Shooter) {

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		// ---- 1) OneShot 再生中は他へ切り替えない
	// ---- 1) OneShot 再生中は他へ切り替えない（タイマー減算は Update() 側）
		if (oneShotPlaying_) {
			return;
		}

		// ---- 2) 状態に応じてアニメ決める
		switch (shooterState_) {
		case ShooterState::Windup:
			// 溜め：Charge（ループ）
			ChangeAnimIfChanged_(shooterAnimCharge_, true);
			break;

		case ShooterState::Cooldown:
		case ShooterState::Aim:
		case ShooterState::Retreat:
		default:
			// それ以外：動いてたらWalk / 止まってたらIdle
			if (isMoving) ChangeAnimIfChanged_(shooterAnimWalk_, true);
			else          ChangeAnimIfChanged_(shooterAnimIdle_, true);
			break;
		}

		//// ---- 3) 「撃つ瞬間」に Fire を 1回だけ再生
		//// Windup -> Cooldown に遷移した瞬間を「発射」扱いにする
		//if (prevShooterState_ == ShooterState::Windup &&
		//	shooterState_ == ShooterState::Cooldown) {

		//	// Fireの長さは仮。見た目で調整
		//	StartOneShot_(shooterAnimFire_, 0.25f);
		//}

		prevShooterState_ = shooterState_;
		return;
	}


	// ===== Boss は glTF アニメで切替 =====
	if (type_ == EnemyType::Boss) {

		// ★OneShot中は他に切り替えない（Meleeと同じ方針）
		if (oneShotPlaying_) {
			return;
		}

		// BossAI の State を見て切り替える
		// ※BossAI::GetState() を追加した前提
		const auto st = bossAI_.GetState();

		switch (st) {
		case BossAI::State::Wander: {
			// Wander中は移動してるなら Walk、止まってるなら Idle
			const float moveEps = 0.05f;
			const bool isMoving =
				(std::abs(vel_.x) > moveEps) ||
				(std::abs(vel_.z) > moveEps) ||
				(std::abs(vel_.y) > moveEps);

			if (isMoving) ChangeAnimIfChanged_("Walk", true);
			else          ChangeAnimIfChanged_("Idle", true);
		} break;

		case BossAI::State::Drop_Windup:
			ChangeAnimIfChanged_("Drop_Windup", true); // ループでOK
			break;
		case BossAI::State::Drop_Fall:
			ChangeAnimIfChanged_("Drop_Fall", true);
			break;
		case BossAI::State::Drop_Land:
			ChangeAnimIfChanged_("Drop_Land", false);  // 1回っぽくしたいなら false
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
			// とりあえず溜め演出＝Idle
			ChangeAnimIfChanged_("Idle", true);
			break;
		}

		return;
	}





}


// -------- EnemyManager --------

float EnemyManager::RandRange_(float a, float b) {
	return a + (b - a) * Rand01_();
}

void EnemyManager::QueueSpawn(EnemyType type, float delaySec) {
	if (enemies_.size() >= maxAlive_) {
		return; // ★上限なら予約しない（B）
	}
	pendingSpawns_.push_back({ type, delaySec });
}


void EnemyManager::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
	objCommon_ = objCommon;
	dx_ = dx;
	cam_ = cam;

	enemies_.clear();
	meleeHitboxes_.clear();

	debugHitboxCube_ = std::make_unique<Object3d>();
	debugHitboxCube_->Initialize(objCommon_, dx_);
	debugHitboxCube_->SetCamera(cam_);
	debugHitboxCube_->SetModel("heal/heal.obj");

	//弾
	bullets_.Initialize(objCommon_, dx_, cam_);

}

void EnemyManager::Clear() {
	enemies_.clear();
	meleeHitboxes_.clear();
	bullets_.Clear();
}

void EnemyManager::Spawn(EnemyType type, const Vector3& posXYZ) {
	Enemy e;
	e.Initialize(objCommon_, dx_, cam_, type, posXYZ);
	e.SetLighting(light_);
	enemies_.push_back(std::move(e));
}

void EnemyManager::Update(float dt, const Vector2& playerXY, float playerZ, Player& player) {
	// 1) 敵本体の更新
	for (auto& e : enemies_) {
		e.Update(dt, playerXY, playerZ); // ← もし使うなら引数を戻してOK
		e.SetLighting(light_);
	}

	// 2) 近接ヒットボックス寿命更新
	for (auto& h : meleeHitboxes_) h.life -= dt;
	meleeHitboxes_.erase(
		std::remove_if(meleeHitboxes_.begin(), meleeHitboxes_.end(),
			[](const MeleeHitbox& h) { return h.life <= 0.0f; }),
		meleeHitboxes_.end()
	);

	auto ToAABB3 = [](const AABB& a)->AABB3 {
		AABB3 b{};
		b.x = (a.min.x + a.max.x) * 0.5f;
		b.y = (a.min.y + a.max.y) * 0.5f;
		b.z = (a.min.z + a.max.z) * 0.5f;
		b.hx = (a.max.x - a.min.x) * 0.5f;
		b.hy = (a.max.y - a.min.y) * 0.5f;
		b.hz = (a.max.z - a.min.z) * 0.5f;
		return b;
		};

	// 近接ヒットボックス vs プレイヤー
	const AABB3 playerBody3 = ToAABB3(player.GetBodyAABB());

	for (auto& h : meleeHitboxes_) {
		if (Intersect3(h.box, playerBody3)) {
			player.TriggerHitFlash(0.25f); // 好きな秒数
			player.Damage(h.damage);

			// 1回当たったら消すなら
			h.life = 0.0f;
		}
	}


	// 3) 攻撃要求を回収
	for (auto& e : enemies_) {

		// ---- Shooter：弾発射要求 ----
		Vector3 muzzle{};
		int dir = +1;
		if (e.ConsumeShootRequest(muzzle, dir)) {

			OutputDebugStringA("[Shoot] request OK\n");

			bullets_.Spawn(muzzle, dir, 7);
		}

		// ---- Melee：近接攻撃ヒットボックス生成 ----
		MeleeKind kind{};
		if (e.ConsumeMeleeRequest(kind)) {

			Vector3 ep = e.GetPos3D();
			const bool isBoss = e.IsBoss();

			// ★ 先に保険：非ボスは必ず Normal
			if (!isBoss) {
				kind = MeleeKind::Normal;
			}

			int facing = (playerXY.x < ep.x) ? -1 : +1;
			const float zCenter = ep.z;

			float offX = 1.2f, offY = 0.0f;
			float halfX = 0.6f, halfY = 0.5f, halfZ = 0.5f;
			float life = 0.10f;

			int dmg = 1;

			switch (kind) {
			case MeleeKind::Normal:

				dmg = 5;

				break;

			case MeleeKind::Land:

				dmg = 10;

				offX = 0.0f; halfX = 2.2f; halfY = 1.3f; halfZ = 1.2f; life = 0.08f;
				break;

			case MeleeKind::Rush:

				dmg = 10;

				offX = 0.8f; halfX = 1.4f; halfY = 1.0f; halfZ = 1.2f; life = 0.06f;
				break;
			}



			AABB3 hb{};
			hb.x = ep.x + offX * float(facing);
			hb.y = ep.y + offY;
			hb.z = zCenter;
			hb.hx = halfX;
			hb.hy = halfY;
			hb.hz = halfZ;

			meleeHitboxes_.push_back({ hb, life, dmg });
		}

	}

	// 4) 死亡削除（★削除直前に回復ドロップ抽選）
	enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
		[this](const Enemy& e) {
			if (!e.IsAlive()) {

				// ★ボス死亡フラグ
				if (e.GetType() == EnemyType::Boss) {
					bossDefeated_ = true;
					// ボスは復活予約しない・回復ドロップもしないならここでreturnでもOK
				}

				// ★回復ドロップ抽選（ボスはTrySpawnHealDrop_側で弾いてる）
				TrySpawnHealDrop_(e);

				// ★Melee / Shooter が倒されたら予約スポーン
				if (e.GetType() == EnemyType::Melee || e.GetType() == EnemyType::Shooter) {
					QueueSpawn(e.GetType(), respawnDelay_);
				}

				return true;
			}
			return false;
		}), enemies_.end());


	bullets_.Update(dt, player);

	UpdateHealDrops_(dt, player);

	UpdatePendingSpawns_(dt, playerXY, playerZ);


}

void EnemyManager::Draw() {

	DrawHealDrops_();

	for (auto& e : enemies_) e.Draw();

	for (auto& e : enemies_) {
		if (e.IsBoss()) {
			Vector3 p = e.GetPos3D();
			char buf[128];
			sprintf_s(buf, "[Boss] pos=(%.2f, %.2f, %.2f)\n", p.x, p.y, p.z);
			OutputDebugStringA(buf);
		}
	}


	bullets_.Draw();

	//if (debugDrawMeleeHitbox_ && debugHitboxCube_) {
	//	for (const auto& h : meleeHitboxes_) {
	//		const auto& b = h.box; // AABB3（center + half）

	//		// center はそのまま使える
	//		Vector3 center{ b.x, b.y, b.z };

	//		// Object3d の cube は「scale が全サイズ」なら 2倍する
	//		// （あなたの実装が半幅スケールならここは調整）
	//		Vector3 size{ b.hx * 2.0f, b.hy * 2.0f, b.hz * 2.0f };

	//		debugHitboxCube_->SetTranslate(center);
	//		debugHitboxCube_->SetScale(size);
	//		debugHitboxCube_->Update();
	//		debugHitboxCube_->Draw();
	//	}
	//}



}

// 0..1
float EnemyManager::Rand01_() {
	return float(std::rand()) / float(RAND_MAX);
}

void EnemyManager::TrySpawnHealDrop_(const Enemy& e) {
	// ボスは落とさない
	if (e.GetType() != EnemyType::Melee && e.GetType() != EnemyType::Shooter) return;

	// 確率
	if (Rand01_() > healDropChance_) return;

	HealDrop d;
	d.pos = e.GetPos3D();
	d.life = 10.0f;
	d.radius = 0.6f;
	d.amount = healDropAmount_;
	healDrops_.push_back(d);
}

void EnemyManager::UpdateHealDrops_(float dt, Player& player) {
	const Vector3 p = player.GetPos3D();

	for (auto& d : healDrops_) {
		d.life -= dt;
		if (d.life <= 0.0f) continue;

		const float dx = p.x - d.pos.x;
		const float dy = p.y - d.pos.y;
		const float dz = p.z - d.pos.z;

		const float r = d.radius;
		if ((dx * dx + dy * dy + dz * dz) <= (r * r)) {
			player.AddHP(d.amount);

			// ★デバッグログ（回復したか確認）
			char buf[128];
			sprintf_s(buf, "[Heal] +%d hp -> %d\n", d.amount, player.GetHP());
			OutputDebugStringA(buf);

			d.life = 0.0f;
		}
	}

	healDrops_.erase(
		std::remove_if(healDrops_.begin(), healDrops_.end(),
			[](const HealDrop& d) { return d.life <= 0.0f; }),
		healDrops_.end()
	);
}


void EnemyManager::DrawHealDrops_() {
	// 見た目は「キューブ」で代用（手軽）
	// 既に debugHitboxCube_ を持ってるのでそれを流用できます
	if (!debugHitboxCube_) return;

	for (auto& d : healDrops_) {
		// ここはあなたの Object3d の使い方に合わせてください
		// 例：位置だけ置いて描画（色替えできるなら緑っぽく）
		debugHitboxCube_->SetTranslate(d.pos);
		debugHitboxCube_->SetScale({ 0.4f, 0.4f, 0.4f });

		debugHitboxCube_->Draw();
	}
}

Vector3 EnemyManager::MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ) {
	const float halfW = 12.0f;   // 画面の半分幅（調整）
	const float pad = 3.0f;    // 画面外にどれだけ出すか
	const float xRand = 3.0f;    // 外側でのばらつき
	const float yRand = 0.0f;    // Yのばらつき

	const bool fromLeft = (std::rand() % 2) == 0;

	float x;
	if (fromLeft) {
		x = RandRange_(playerXY.x - halfW - pad - xRand,
			playerXY.x - halfW - pad);
	} else {
		x = RandRange_(playerXY.x + halfW + pad,
			playerXY.x + halfW + pad + xRand);
	}

	float y = RandRange_(playerXY.y - yRand, playerXY.y + yRand);

	// Zは見た目用なら playerZ に合わせるのが無難
	return Vector3{ x, y, playerZ };
}

void EnemyManager::UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ) {
	// タイマー更新
	for (auto& p : pendingSpawns_) p.t -= dt;

	// ★上限に達してるなら予約を全部捨てる（B）
	if (enemies_.size() >= maxAlive_) {
		pendingSpawns_.clear();
		return;
	}

	// t<=0 のものを、空きがある分だけスポーン
	for (size_t i = 0; i < pendingSpawns_.size();) {
		if (enemies_.size() >= maxAlive_) {
			// 途中で上限に達したら、残り予約は捨てる（B）
			pendingSpawns_.clear();
			return;
		}

		if (pendingSpawns_[i].t <= 0.0f) {
			EnemyType type = pendingSpawns_[i].type;
			Vector3 pos = MakeOutsideSpawnPos_(playerXY, playerZ);
			Spawn(type, pos);

			pendingSpawns_.erase(pendingSpawns_.begin() + i);
		} else {
			++i;
		}
	}
}

void Enemy::SetLighting(const LightingParam& p)
{
	light_ = p;
	if (!model_) return;

	model_->SetEnableLighting(light_.lightingMode);

	model_->SetDirection(light_.dir);
	model_->SetIntensity(light_.dirIntensity);
	model_->SetLightColor(light_.dirColor);

	model_->SetPointLightPos(light_.pointPos);
	model_->SetPointLightIntensity(light_.pointIntensity);
	model_->SetPointLightColor(light_.pointColor);
	model_->SetPointLightRadius(light_.pointRadius);
	model_->SetPointLightDecay(light_.pointDecay);

	light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

	const float cosOuter = std::cosf(light_.spotAngleDeg * (std::numbers::pi_v<float> / 180.0f));
	const float cosInner = std::cosf(light_.spotFalloffStartDeg * (std::numbers::pi_v<float> / 180.0f));

	model_->SetSpotLightPos(light_.spotPos);
	model_->SetSpotLightDirection(light_.spotDir);
	model_->SetSpotLightIntensity(light_.spotIntensity);
	model_->SetSpotLightDistance(light_.spotDistance);
	model_->SetSpotLightDecay(light_.spotDecay);
	model_->SetSpotLightCosAngle(cosOuter);
	model_->SetSpotLightCosFalloffStart(cosInner);
	model_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });
}

void EnemyManager::SetLighting(const LightingParam& p)
{
	light_ = p;
	for (auto& e : enemies_) {
		e.SetLighting(light_);
	}
}
