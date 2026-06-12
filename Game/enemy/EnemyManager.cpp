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
		return;
	}
	pendingSpawns_.push_back({ type, delaySec });
}

void EnemyManager::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
	replaySpawnOverrides_ = overrides;
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

EnemyManager::BossHitTuning& EnemyManager::BossTuning(MeleeKind kind) {
	switch (kind) {
	case MeleeKind::Land:
		return bossLandTuning_;
	case MeleeKind::Rush:
		return bossRushTuning_;
	case MeleeKind::Normal:
	default:
		return bossNormalTuning_;
	}
}

const EnemyManager::BossHitTuning& EnemyManager::BossTuning(MeleeKind kind) const {
	switch (kind) {
	case MeleeKind::Land:
		return bossLandTuning_;
	case MeleeKind::Rush:
		return bossRushTuning_;
	case MeleeKind::Normal:
	default:
		return bossNormalTuning_;
	}
}

std::vector<EnemyManager::PlayerAttackHitEvent> EnemyManager::ApplyPlayerAttack(Player& player) {
	std::vector<PlayerAttackHitEvent> hitEvents;
	AABB attackBox{};
	const unsigned int attackSerial = player.GetAttackSerial();
	const int damage = player.GetAttackDamage();
	if (attackSerial == 0 || damage <= 0 || !player.GetAttackHitBox(attackBox)) {
		return hitEvents;
	}

	const AABB3 attackBox3 = ToAABB3(attackBox);
	for (size_t enemyIndex = 0; enemyIndex < enemies_.size(); ++enemyIndex) {
		auto& enemy = enemies_[enemyIndex];
		if (!enemy.IsAlive() || enemy.WasHitByPlayerAttack(attackSerial)) {
			continue;
		}
		if (!Intersect3(attackBox3, ToAABB3(enemy.GetBodyAABB()))) {
			continue;
		}

		const int hpBefore = enemy.GetHP();
		const float knockDir = (enemy.GetPos3D().x >= player.GetPos3D().x) ? 1.0f : -1.0f;
		enemy.ApplyHit2D(8.0f * knockDir, 8.0f, true, damage);
		enemy.MarkHitByPlayerAttack(attackSerial);

		PlayerAttackHitEvent event;
		event.targetId = "enemy_" + std::to_string(enemyIndex);
		event.targetType = DebugEnemyTypeName(enemy.GetType());
		event.attackSerial = attackSerial;
		event.damage = damage;
		event.hpBefore = hpBefore;
		event.hpAfter = enemy.GetHP();
		event.playerPosition = player.GetPos3D();
		event.targetPosition = enemy.GetPos3D();
		hitEvents.push_back(event);
	}

	return hitEvents;
}

void EnemyManager::AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const {
	int entityIndex = 0;
	for (const Enemy& enemy : enemies_) {
		if (!enemy.IsAlive()) {
			continue;
		}

		DebugEntityState state;
		state.id = "enemy_" + std::to_string(entityIndex++);
		state.category = "Enemy";
		state.type = DebugEnemyTypeName(enemy.GetType());
		state.hp = enemy.GetHP();
		state.position = enemy.GetPos3D();
		state.velocity = enemy.GetVel();
		if (enemy.IsBoss()) {
			BossAI::BossDebugState bossState = enemy.GetBossAI().GetDebugState();
			state.aiState1 = static_cast<int>(bossState.st);
			state.aiState2 = static_cast<int>(bossState.phase);
			state.aiFloat1 = bossState.t;
			state.aiFloat2 = bossState.stateTime;
			int flags = 0;
			if (bossState.did50) flags |= 1;
			if (bossState.did25) flags |= 2;
			state.aiFloat3 = static_cast<float>(flags);
			state.bossWanderVel = bossState.wanderVel;
			state.bossWanderChange = bossState.wanderChange;
			state.bossMoveMul = bossState.moveMul;
			state.bossDropStartY = bossState.dropStartY;
			state.bossRushSpeed = bossState.rushSpeed;
			state.bossChaseSpeed = bossState.chaseSpeed;
			state.bossRushZMin = bossState.rushZMin;
			state.bossRushZMax = bossState.rushZMax;
		}
		state.alive = true;
		state.pending = false;
		state.delay = 0.0f;
		outEntities.push_back(state);
	}

	int pendingIndex = 0;
	for (const PendingSpawn& pending : pendingSpawns_) {
		DebugEntityState state;
		state.id = "pending_spawn_" + std::to_string(pendingIndex++);
		state.category = "PendingSpawn";
		state.type = DebugEnemyTypeName(pending.type);
		state.hp = 0;
		state.alive = true;
		state.pending = true;
		state.delay = pending.t;
		outEntities.push_back(state);
	}

	int healDropIndex = 0;
	for (const HealDrop& drop : healDrops_) {
		DebugEntityState state;
		state.id = "heal_drop_" + std::to_string(healDropIndex++);
		state.category = "HealDrop";
		state.type = "Heal";
		state.hp = drop.amount; // Use hp for amount
		state.life = drop.life; // Use life for remaining duration
		state.position = drop.pos;
		state.alive = true;
		state.pending = false;
		outEntities.push_back(state);
	}

	bullets_.AppendDebugEntities(outEntities);
}

void EnemyManager::RestoreDebugEntities(const std::vector<DebugEntityState>& entities) {
	Clear();

	for (const DebugEntityState& entity : entities) {
		if (entity.category == "HealDrop") {
			HealDrop d;
			d.pos = entity.position;
			d.life = entity.life;
			d.amount = entity.hp;
			d.radius = 0.6f; // Default radius
			healDrops_.push_back(d);
			continue;
		}

		if (entity.category != "Enemy" && entity.category != "PendingSpawn") {
			continue;
		}

		EnemyType type{};
		if (!TryParseDebugEnemyType(entity.type, type)) {
			continue;
		}

		if (entity.pending || entity.category == "PendingSpawn") {
			QueueSpawn(type, entity.delay);
			continue;
		}

		Spawn(type, entity.position);
		if (!enemies_.empty()) {
			Enemy& newEnemy = enemies_.back();
			newEnemy.SetHP(entity.hp);
			newEnemy.SetVel(entity.velocity);
			if (newEnemy.IsBoss()) {
				int flags = static_cast<int>(entity.aiFloat3);
				BossAI::BossDebugState bossState;
				bossState.st = static_cast<BossAI::State>(entity.aiState1);
				bossState.phase = static_cast<BossAI::Phase>(entity.aiState2);
				bossState.t = entity.aiFloat1;
				bossState.stateTime = entity.aiFloat2;
				bossState.did50 = (flags & 1) != 0;
				bossState.did25 = (flags & 2) != 0;
				bossState.wanderVel = entity.bossWanderVel;
				bossState.wanderChange = entity.bossWanderChange;
				bossState.moveMul = entity.bossMoveMul;
				bossState.dropStartY = entity.bossDropStartY;
				bossState.rushSpeed = entity.bossRushSpeed;
				bossState.chaseSpeed = entity.bossChaseSpeed;
				bossState.rushZMin = entity.bossRushZMin;
				bossState.rushZMax = entity.bossRushZMax;
				newEnemy.GetBossAIMutable().RestoreDebugState(bossState);
			}
		}
	}

	bullets_.RestoreDebugEntities(entities);
}

void EnemyManager::Update(float dt, const Vector2& playerXY, float playerZ, Player& player, bool disablePendingSpawn) {
	for (auto& e : enemies_) {
		e.Update(dt, playerXY, playerZ);
		e.SetLighting(light_);
	}

	for (auto& h : meleeHitboxes_) h.life -= dt;
	meleeHitboxes_.erase(
		std::remove_if(meleeHitboxes_.begin(), meleeHitboxes_.end(),
			[](const MeleeHitbox& h) { return h.life <= 0.0f; }),
		meleeHitboxes_.end()
	);

	const AABB3 playerBody3 = ToAABB3(player.GetBodyAABB());

	for (auto& h : meleeHitboxes_) {
		if (Intersect3(h.box, playerBody3)) {
			player.TriggerHitFlash(0.25f);
			if (h.fromBoss) {
				BossHitTuning tuning = BossTuning(h.kind);
				Vector3 dir = tuning.knockbackDir;
				const float dirX = (player.GetX() >= h.attackerPos.x) ? 1.0f : -1.0f;
				dir.x = std::abs(dir.x) * dirX;
				player.ApplyBossHit(
					tuning.damagePercent,
					tuning.baseKnockback,
					tuning.knockbackScale,
					dir,
					tuning.hitStunSec);
			} else {
				player.Damage(h.damage);
			}

			h.life = 0.0f;
		}
	}


	for (auto& e : enemies_) {

		Vector3 muzzle{};
		int dir = +1;
		if (e.ConsumeShootRequest(muzzle, dir)) {

			OutputDebugStringA("[Shoot] request OK\n");

			bullets_.Spawn(muzzle, dir, 7);
		}

		MeleeKind kind{};
		if (e.ConsumeMeleeRequest(kind)) {

			Vector3 ep = e.GetPos3D();
			const bool isBoss = e.IsBoss();

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

			meleeHitboxes_.push_back({ hb, life, dmg, isBoss, kind, ep, facing });
		}

	}

	enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
		[this](const Enemy& e) {
			if (!e.IsAlive()) {

				if (e.GetType() == EnemyType::Boss) {
					bossDefeated_ = true;
				}

				TrySpawnHealDrop_(e);

				if (e.GetType() == EnemyType::Melee || e.GetType() == EnemyType::Shooter) {
					QueueSpawn(e.GetType(), respawnDelay_);
				}

				return true;
			}
			return false;
		}), enemies_.end());


	bullets_.Update(dt, player);

	UpdateHealDrops_(dt, player);

	if (!disablePendingSpawn) {
		UpdatePendingSpawns_(dt, playerXY, playerZ);
	}
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

	//		Vector3 center{ b.x, b.y, b.z };

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
	if (e.GetType() != EnemyType::Melee && e.GetType() != EnemyType::Shooter) return;

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
	if (!debugHitboxCube_) return;

	for (auto& d : healDrops_) {
		debugHitboxCube_->SetTranslate(d.pos);
		debugHitboxCube_->SetScale({ 0.4f, 0.4f, 0.4f });

		debugHitboxCube_->Draw();
	}
}

Vector3 EnemyManager::MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ) {
	const float halfW = 12.0f;
	const float pad = 3.0f;
	const float xRand = 3.0f;
	const float yRand = 0.0f;

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

	return Vector3{ x, y, playerZ };
}

void EnemyManager::UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ) {
	for (auto& p : pendingSpawns_) p.t -= dt;

	if (enemies_.size() >= maxAlive_) {
		pendingSpawns_.clear();
		return;
	}

	for (size_t i = 0; i < pendingSpawns_.size();) {
		if (enemies_.size() >= maxAlive_) {
			pendingSpawns_.clear();
			return;
		}

		if (pendingSpawns_[i].t <= 0.0f) {
			EnemyType type = pendingSpawns_[i].type;
			Vector3 pos{};
			bool hasReplayOverride = false;
			for (size_t overrideIndex = 0; overrideIndex < replaySpawnOverrides_.size(); ++overrideIndex) {
				EnemyType overrideType{};
				if (!TryParseDebugEnemyType(replaySpawnOverrides_[overrideIndex].type, overrideType)) {
					continue;
				}
				if (overrideType != type) {
					continue;
				}
				pos = replaySpawnOverrides_[overrideIndex].position;
				replaySpawnOverrides_.erase(replaySpawnOverrides_.begin() + overrideIndex);
				hasReplayOverride = true;
				break;
			}
			if (hasReplayOverride) {
				// Consume random numbers to keep RNG sequence identical to recording
				MakeOutsideSpawnPos_(playerXY, playerZ);
			} else {
				pos = MakeOutsideSpawnPos_(playerXY, playerZ);
			}
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
