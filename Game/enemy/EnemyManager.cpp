#include "EnemyManager.h"
#include "Enemy.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

// -------- EnemyManager --------

namespace {
bool Intersect3(const EnemyManager::AABB3& a, const EnemyManager::AABB3& b) {
	return (std::abs(a.x - b.x) <= (a.hx + b.hx)) &&
		(std::abs(a.y - b.y) <= (a.hy + b.hy)) &&
		(std::abs(a.z - b.z) <= (a.hz + b.hz));
}

EnemyManager::AABB3 ToAABB3(const AABB& a) {
	EnemyManager::AABB3 b{};
	b.x = (a.min.x + a.max.x) * 0.5f;
	b.y = (a.min.y + a.max.y) * 0.5f;
	b.z = (a.min.z + a.max.z) * 0.5f;
	b.hx = (a.max.x - a.min.x) * 0.5f;
	b.hy = (a.max.y - a.min.y) * 0.5f;
	b.hz = (a.max.z - a.min.z) * 0.5f;
	return b;
}

const char* DebugEnemyTypeName(EnemyType type) {
	switch (type) {
	case EnemyType::Melee:
		return "Melee";
	case EnemyType::Shooter:
		return "Shooter";
	case EnemyType::Boss:
		return "Boss";
	default:
		return "Unknown";
	}
}

bool TryParseDebugEnemyType(const std::string& typeName, EnemyType& outType) {
	if (typeName == "Melee") {
		outType = EnemyType::Melee;
		return true;
	}
	if (typeName == "Shooter") {
		outType = EnemyType::Shooter;
		return true;
	}
	if (typeName == "Boss") {
		outType = EnemyType::Boss;
		return true;
	}
	return false;
}
}

float EnemyManager::RandRange_(float a, float b) {
	return a + (b - a) * Rand01_();
}

void EnemyManager::QueueSpawn(EnemyType type, float delaySec) {
	if (enemies_.size() >= maxAlive_) {
		return; // 笘・ｸ企剞縺ｪ繧我ｺ育ｴ・＠縺ｪ縺・ｼ・・・
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

	//蠑ｾ
	bullets_.Initialize(objCommon_, dx_, cam_);

}

void EnemyManager::Clear() {
	enemies_.clear();
	meleeHitboxes_.clear();
	healDrops_.clear();
	pendingSpawns_.clear();
	bossDefeated_ = false;
	bullets_.Clear();
}

void EnemyManager::Spawn(EnemyType type, const Vector3& posXYZ) {
	Enemy e;
	e.Initialize(objCommon_, dx_, cam_, type, posXYZ);
	e.SetLighting(light_);
	enemies_.push_back(std::move(e));
}

void EnemyManager::ApplyPlayerAttack(Player& player) {
	AABB attackBox{};
	const unsigned int attackSerial = player.GetAttackSerial();
	const int damage = player.GetAttackDamage();
	if (attackSerial == 0 || damage <= 0 || !player.GetAttackHitBox(attackBox)) {
		return;
	}

	const AABB3 attackBox3 = ToAABB3(attackBox);
	for (auto& enemy : enemies_) {
		if (!enemy.IsAlive() || enemy.WasHitByPlayerAttack(attackSerial)) {
			continue;
		}
		if (!Intersect3(attackBox3, ToAABB3(enemy.GetBodyAABB()))) {
			continue;
		}

		const float knockDir = (enemy.GetPos3D().x >= player.GetPos3D().x) ? 1.0f : -1.0f;
		enemy.ApplyHit2D(8.0f * knockDir, 8.0f, true, damage);
		enemy.MarkHitByPlayerAttack(attackSerial);
	}
}

void EnemyManager::AppendDebugEnemyStates(std::vector<DebugEnemyState>& outStates) const {
	for (const Enemy& enemy : enemies_) {
		if (!enemy.IsAlive()) {
			continue;
		}

		DebugEnemyState state;
		state.type = DebugEnemyTypeName(enemy.GetType());
		state.hp = enemy.GetHP();
		state.position = enemy.GetPos3D();
		state.pendingSpawn = false;
		state.spawnDelay = 0.0f;
		outStates.push_back(state);
	}

	for (const PendingSpawn& pending : pendingSpawns_) {
		DebugEnemyState state;
		state.type = DebugEnemyTypeName(pending.type);
		state.hp = 0;
		state.pendingSpawn = true;
		state.spawnDelay = pending.t;
		outStates.push_back(state);
	}
}

void EnemyManager::RestoreDebugEnemyStates(const std::vector<DebugEnemyState>& states) {
	Clear();

	for (const DebugEnemyState& state : states) {
		EnemyType type{};
		if (!TryParseDebugEnemyType(state.type, type)) {
			continue;
		}

		if (state.pendingSpawn) {
			QueueSpawn(type, state.spawnDelay);
			continue;
		}

		Spawn(type, state.position);
		if (!enemies_.empty()) {
			enemies_.back().SetHP(state.hp);
		}
	}
}

void EnemyManager::Update(float dt, const Vector2& playerXY, float playerZ, Player& player) {
	// 1) 謨ｵ譛ｬ菴薙・譖ｴ譁ｰ
	for (auto& e : enemies_) {
		e.Update(dt, playerXY, playerZ); // 竊・繧ゅ＠菴ｿ縺・↑繧牙ｼ墓焚繧呈綾縺励※OK
		e.SetLighting(light_);
	}

	// 2) 霑第磁繝偵ャ繝医・繝・け繧ｹ蟇ｿ蜻ｽ譖ｴ譁ｰ
	for (auto& h : meleeHitboxes_) h.life -= dt;
	meleeHitboxes_.erase(
		std::remove_if(meleeHitboxes_.begin(), meleeHitboxes_.end(),
			[](const MeleeHitbox& h) { return h.life <= 0.0f; }),
		meleeHitboxes_.end()
	);

	// 霑第磁繝偵ャ繝医・繝・け繧ｹ vs 繝励Ξ繧､繝､繝ｼ
	const AABB3 playerBody3 = ToAABB3(player.GetBodyAABB());

	for (auto& h : meleeHitboxes_) {
		if (Intersect3(h.box, playerBody3)) {
			player.TriggerHitFlash(0.25f); // 螂ｽ縺阪↑遘呈焚
			player.Damage(h.damage);

			// 1蝗槫ｽ薙◆縺｣縺溘ｉ豸医☆縺ｪ繧・
			h.life = 0.0f;
		}
	}


	// 3) 謾ｻ謦・ｦ∵ｱゅｒ蝗槫庶
	for (auto& e : enemies_) {

		// ---- Shooter・壼ｼｾ逋ｺ蟆・ｦ∵ｱ・----
		Vector3 muzzle{};
		int dir = +1;
		if (e.ConsumeShootRequest(muzzle, dir)) {

			OutputDebugStringA("[Shoot] request OK\n");

			bullets_.Spawn(muzzle, dir, 7);
		}

		// ---- Melee・夊ｿ第磁謾ｻ謦・ヲ繝・ヨ繝懊ャ繧ｯ繧ｹ逕滓・ ----
		MeleeKind kind{};
		if (e.ConsumeMeleeRequest(kind)) {

			Vector3 ep = e.GetPos3D();
			const bool isBoss = e.IsBoss();

			// 笘・蜈医↓菫晞匱・夐撼繝懊せ縺ｯ蠢・★ Normal
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

	// 4) 豁ｻ莠｡蜑企勁・遺・蜑企勁逶ｴ蜑阪↓蝗槫ｾｩ繝峨Ο繝・・謚ｽ驕ｸ・・
	enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
		[this](const Enemy& e) {
			if (!e.IsAlive()) {

				// 笘・・繧ｹ豁ｻ莠｡繝輔Λ繧ｰ
				if (e.GetType() == EnemyType::Boss) {
					bossDefeated_ = true;
					// 繝懊せ縺ｯ蠕ｩ豢ｻ莠育ｴ・＠縺ｪ縺・・蝗槫ｾｩ繝峨Ο繝・・繧ゅ＠縺ｪ縺・↑繧峨％縺薙〒return縺ｧ繧０K
				}

				// 笘・屓蠕ｩ繝峨Ο繝・・謚ｽ驕ｸ・医・繧ｹ縺ｯTrySpawnHealDrop_蛛ｴ縺ｧ蠑ｾ縺・※繧具ｼ・
				TrySpawnHealDrop_(e);

				// 笘・elee / Shooter 縺悟偵＆繧後◆繧我ｺ育ｴ・せ繝昴・繝ｳ
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
	//		const auto& b = h.box; // AABB3・・enter + half・・

	//		// center 縺ｯ縺昴・縺ｾ縺ｾ菴ｿ縺医ｋ
	//		Vector3 center{ b.x, b.y, b.z };

	//		// Object3d 縺ｮ cube 縺ｯ縲茎cale 縺悟・繧ｵ繧､繧ｺ縲阪↑繧・2蛟阪☆繧・
	//		// ・医≠縺ｪ縺溘・螳溯｣・′蜊雁ｹ・せ繧ｱ繝ｼ繝ｫ縺ｪ繧峨％縺薙・隱ｿ謨ｴ・・
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
	// 繝懊せ縺ｯ關ｽ縺ｨ縺輔↑縺・
	if (e.GetType() != EnemyType::Melee && e.GetType() != EnemyType::Shooter) return;

	// 遒ｺ邇・
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

			// 笘・ョ繝舌ャ繧ｰ繝ｭ繧ｰ・亥屓蠕ｩ縺励◆縺狗｢ｺ隱搾ｼ・
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
	// 隕九◆逶ｮ縺ｯ縲後く繝･繝ｼ繝悶阪〒莉｣逕ｨ・域焔霆ｽ・・
	// 譌｢縺ｫ debugHitboxCube_ 繧呈戟縺｣縺ｦ繧九・縺ｧ縺昴ｌ繧呈ｵ∫畑縺ｧ縺阪∪縺・
	if (!debugHitboxCube_) return;

	for (auto& d : healDrops_) {
		// 縺薙％縺ｯ縺ゅ↑縺溘・ Object3d 縺ｮ菴ｿ縺・婿縺ｫ蜷医ｏ縺帙※縺上□縺輔＞
		// 萓具ｼ壻ｽ咲ｽｮ縺縺醍ｽｮ縺・※謠冗判・郁牡譖ｿ縺医〒縺阪ｋ縺ｪ繧臥ｷ代▲縺ｽ縺擾ｼ・
		debugHitboxCube_->SetTranslate(d.pos);
		debugHitboxCube_->SetScale({ 0.4f, 0.4f, 0.4f });

		debugHitboxCube_->Draw();
	}
}

Vector3 EnemyManager::MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ) {
	const float halfW = 12.0f;   // 逕ｻ髱｢縺ｮ蜊雁・蟷・ｼ郁ｪｿ謨ｴ・・
	const float pad = 3.0f;    // 逕ｻ髱｢螟悶↓縺ｩ繧後□縺大・縺吶°
	const float xRand = 3.0f;    // 螟門・縺ｧ縺ｮ縺ｰ繧峨▽縺・
	const float yRand = 0.0f;    // Y縺ｮ縺ｰ繧峨▽縺・

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

	// Z縺ｯ隕九◆逶ｮ逕ｨ縺ｪ繧・playerZ 縺ｫ蜷医ｏ縺帙ｋ縺ｮ縺檎┌髮｣
	return Vector3{ x, y, playerZ };
}

void EnemyManager::UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ) {
	// 繧ｿ繧､繝槭・譖ｴ譁ｰ
	for (auto& p : pendingSpawns_) p.t -= dt;

	// 笘・ｸ企剞縺ｫ驕斐＠縺ｦ繧九↑繧我ｺ育ｴ・ｒ蜈ｨ驛ｨ謐ｨ縺ｦ繧具ｼ・・・
	if (enemies_.size() >= maxAlive_) {
		pendingSpawns_.clear();
		return;
	}

	// t<=0 縺ｮ繧ゅ・繧偵∫ｩｺ縺阪′縺ゅｋ蛻・□縺代せ繝昴・繝ｳ
	for (size_t i = 0; i < pendingSpawns_.size();) {
		if (enemies_.size() >= maxAlive_) {
			// 騾比ｸｭ縺ｧ荳企剞縺ｫ驕斐＠縺溘ｉ縲∵ｮ九ｊ莠育ｴ・・謐ｨ縺ｦ繧具ｼ・・・
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
