#include "TestScene.h"
#include "TestSceneBossTuning.h"
#include "TestSceneKnockbackPreview.h"

#include "GameApp.h"
#include "Input.h"
#include "Camera.h"
#include "Player.h"
#include "Particle/ParticleManager.h"
#include "Effect/EffectManager.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "SrvManager.h"

#include <d3d12.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

void TestScene::OnEnter(GameApp& app) {
  //  TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

    input_ = app.GetInput();
    assert(input_);

    camera_ = std::make_unique<Camera>();

    // GameScene縺ｨ蜷後§繧ｫ繝｡繝ｩ縺ｧOK
    camera_->SetTranslate({ 0.0f, 20.0f, -50.0f });
    camera_->SetRotate({ 0.35f, 0.0f, 0.0f });

    app.ObjCom()->SetDefaultCamera(camera_.get());

    // Player
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    player_->SetSpawnPos(playerSpawnPos_);

    // EnemyManager
    enemyMgr_.Initialize(app.ObjCom(), app.Dx(), camera_.get());
    if (std::filesystem::exists(bossTuningPath_)) {
        TestSceneBossTuning::Load(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_);
    }

    enemyMgr_.Spawn(EnemyType::Boss, bossSpawnPos_);
    
    // 笘・㍾邨撰ｼ・etEnemies() 縺ｯ繝・ヰ繝・げ遒ｺ隱阪↓繧ゆｽｿ縺・・縺ｧ蟄伜惠縺励※繧句燕謠撰ｼ・
    auto& enemies = enemyMgr_.GetEnemies();
    if (!enemies.empty()) {
        enemies.back().SetInvincible(true); // 豁ｻ縺ｪ縺ｪ縺・
        enemies.back().SetAIDisabled(!bossAIEnabled_);
    }

	TextureManager::GetInstance()->LoadTexture("resources/ui/text1.png");

    playTxst_ = std::make_unique<Sprite>();
    playTxst_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/text1.png");
    playTxst_->AdjustTextureSize();
    playTxst_->SetScale({ 1.0f, 1.0f ,1.0f });
	playTxst_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

    light_.dirIntensity = 1.6f;
    light_.pointIntensity = 2.5f;
    light_.spotIntensity = 0.0f;

    player_->SetLighting(light_);
    enemyMgr_.SetLighting(light_);


    // 縺ｩ縺薙°縺ｧ・・nEnter縺ｮ荳ｭ・・
    //auto* mgr = ModelManager::GetInstance();
    //mgr->LoadModel("ground/ground.obj");   // resources/ground/ground.obj 繧呈Φ螳・

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/ground.obj");

    

    // 菴咲ｽｮ繝ｻ螟ｧ縺阪＆縺ｯ螂ｽ縺ｿ縺ｧ隱ｿ謨ｴ
    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
    ground_->SetEnableLighting(2);     // 2縺ｯ繝上・繝輔Λ繝ｳ繝舌・繝・
    ground_->SetIntensity(2.0f);
    ground_->SetLightColor(light_.dirColor);
    ground_->SetEnableLighting(0);
    // Ground縺ｯ point縺縺台ｽｿ縺・
    groundLight_ = light_;              // 縺ｨ繧翫≠縺医★譌｢蟄倥ｒ繧ｳ繝斐・縺励※繧０K
    groundLight_.dirIntensity = 0.0f;   // 笘・irectional 辟｡蜉ｹ
    groundLight_.spotIntensity = 0.0f;  // 笘・pot 辟｡蜉ｹ

    groundLight_.pointIntensity = 16.0f;
    groundLight_.pointPos = { 0.0f, -42.0f, -1.0f };
    groundLight_.pointRadius = 200.0f;
    groundLight_.pointDecay = 1.0f;
    groundLight_.pointColor = { 1.0f, 1.0f, 1.0f }; // Vector3諠ｳ螳・

    // Spot ON
    groundLight_.spotIntensity = 20.0f;
    groundLight_.spotPos = { 0.0f, 15.0f, 15.0f };

    // 笘・irection 縺ｯ縲後←縺薙ｒ蜷代￥縺九・
    // 縺ｨ繧翫≠縺医★蝨ｰ髱｢縺ｮ荳ｭ螟ｮ縺ｸ蜷代￠繧具ｼ亥ｾ後〒豈弱ヵ繝ｬ譖ｴ譁ｰ縺励※繧０K・・
    Vector3 target = { 0.0f, 0.0f, 15.0f };
    Vector3 d = { target.x - groundLight_.spotPos.x, target.y - groundLight_.spotPos.y, target.z - groundLight_.spotPos.z };
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-6f) { d.x /= len; d.y /= len; d.z /= len; }
    }
    groundLight_.spotDir = d;

    groundLight_.spotDistance = 80.0f;
    groundLight_.spotDecay = 1.0f;

    // 隗貞ｺｦ・・egree邂｡逅・＠縺ｦ繧句燕謠撰ｼ・
    groundLight_.spotAngleDeg = 25.0f;
    groundLight_.spotFalloffStartDeg = 15.0f;
    groundLight_.spotColor = { 1.0f, 1.0f, 1.0f };

    // --- Spot 繝槭・繧ｫ繝ｼ ---
    spotMarker_ = std::make_unique<Object3d>();
    spotMarker_->Initialize(app.ObjCom(), app.Dx());
    spotMarker_->SetCamera(camera_.get());
    spotMarker_->SetModel("cube/cube.obj");
    spotMarker_->SetEnableLighting(0);
    spotMarker_->SetMaterialColor({ 0, 1, 1, 1 }); // 繧ｷ繧｢繝ｳ
    spotMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    // PointLight繝槭・繧ｫ繝ｼ
    pointMarker_ = std::make_unique<Object3d>();
    pointMarker_->Initialize(app.ObjCom(), app.Dx());
    pointMarker_->SetCamera(camera_.get());
    pointMarker_->SetModel("cube/cube.obj");

    // 隕九◆逶ｮ
    pointMarker_->SetEnableLighting(0);                 // 笘・Λ繧､繝医・蠖ｱ髻ｿ繧貞女縺代↑縺・
    pointMarker_->SetMaterialColor({ 1, 1, 0, 1 });     // 鮟・牡縺ｨ縺具ｼ亥･ｽ縺ｿ縺ｧ・・
    pointMarker_->SetShininess(1.0f);
    pointMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");

    // 笘・せ繧ｫ繧､繝峨・繝縺ｯ蝓ｺ譛ｬ縲後Λ繧､繝育┌隕悶・
    skyDome_->SetEnableLighting(0);              // 竊・縺ゅ↑縺溘・莉墓ｧ倥・縲檎┌辣ｧ譏弱Δ繝ｼ繝峨阪↓蜷医ｏ縺帙※
    skyDome_->SetMaterialColor({ 1,1,1,1 });       // 蠢ｵ縺ｮ縺溘ａ
    skyDome_->SetShininess(1.0f);                // 蠖ｱ髻ｿ縺励↑縺・￠縺ｩ菫晞匱
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    knockbackPreviewLine_ = std::make_unique<Object3d>();
    knockbackPreviewLine_->Initialize(app.ObjCom(), app.Dx());
    knockbackPreviewLine_->SetCamera(camera_.get());
    knockbackPreviewLine_->SetModel("cube/cube.obj");
    knockbackPreviewLine_->SetEnableLighting(0);
    knockbackPreviewLine_->SetMaterialColor({ 1.0f, 0.15f, 0.05f, 1.0f });
    knockbackPreviewLine_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    bossHitboxPreview_ = std::make_unique<Object3d>();
    bossHitboxPreview_->Initialize(app.ObjCom(), app.Dx());
    bossHitboxPreview_->SetCamera(camera_.get());
    bossHitboxPreview_->SetModel("cube/cube.obj");
    bossHitboxPreview_->SetEnableLighting(0);
    bossHitboxPreview_->SetMaterialColor({ 0.1f, 0.8f, 1.0f, 0.35f });
    bossHitboxPreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    // ===== LevelLoader: JSON縺九ｉ繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ隱ｭ縺ｿ霎ｼ繧 =====
    // 1. Blender繧｢繝峨が繝ｳ縺ｧJSON繧偵お繧ｯ繧ｹ繝昴・繝医☆繧・
    // 2. resources/levels/stage1.json 縺ｫ驟咲ｽｮ縺吶ｋ
    // 3. 荳九・繧ｳ繝｡繝ｳ繝医い繧ｦ繝医ｒ螟悶☆
    /*
    LevelLoader::LevelData levelData = LevelLoader::Load("stage1");
    for (auto& objData : levelData.objects) {
        if (objData.fileName.empty()) { continue; }
        ModelManager::GetInstance()->LoadModel(objData.fileName);
        auto obj = std::make_unique<Object3d>();
        obj->Initialize(app.ObjCom(), app.Dx());
        obj->SetCamera(camera_.get());
        obj->SetModel(objData.fileName);
        obj->SetTranslate(objData.translation);
        obj->SetRotate(objData.rotation);
        obj->SetScale(objData.scaling);
        levelObjects_.push_back(std::move(obj));
    }
    */

    EffectManager::GetInstance()->Initialize();
    EffectManager::GetInstance()->SetGraphicsResources(app.ObjCom(), app.Dx(), camera_.get());
    ParticleManager::GetInstance()->ClearGroups();
    const std::vector<std::string> skipPreviewGroups = { "gpu_test" };
    ParticleManager::GetInstance()->LoadAdditional("playerHitEffect.json", "", skipPreviewGroups);
    ParticleManager::GetInstance()->LoadAdditional("fallAttak_Effect.json", "fallAttak_", skipPreviewGroups);
    EffectManager::GetInstance()->LoadEffect("fallAttak", "resources/effects/fallAttak.json");

}

void TestScene::OnExit(GameApp& /*app*/) {
    EffectManager::GetInstance()->Finalize();
    player_.reset();
    camera_.reset();
    enemyMgr_.Clear();
}

void TestScene::Update(GameApp& app, float dt) {
    if (!input_) return;

    if (resetFightersRequested_) {
        resetFightersRequested_ = false;

        if (player_) {
            player_->SetSpawnPos(playerSpawnPos_);
            player_->SetDamagePercent(0.0f);
            player_->SetHP(player_->GetMaxHP());
        }

        enemyMgr_.Clear();
        enemyMgr_.Spawn(EnemyType::Boss, bossSpawnPos_);
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            boss->SetInvincible(true);
            boss->SetAIDisabled(!bossAIEnabled_);
        }

        reachedEdge_ = false;
        previewLineWasLaunched_ = false;
        hitStopTimer_ = 0.0f;
    }

    camera_->Update();

    ground_->Update(dt);
    skyDome_->Update(dt);

    // LevelLoader 縺ｧ隱ｭ縺ｿ霎ｼ繧薙□繧ｪ繝悶ず繧ｧ繧ｯ繝医・譖ｴ譁ｰ
    for (auto& obj : levelObjects_) {
        obj->Update(dt);
    }

    const bool hitStopActive = hitStopTimer_ > 0.0f;
    if (hitStopActive) {
        hitStopTimer_ = std::max(0.0f, hitStopTimer_ - dt);
    }

    if (!hitStopActive && player_) {
        player_->Update(dt, *input_, enemyMgr_);
        enemyMgr_.ApplyPlayerAttack(*player_);
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    Vector2 playerPos2D = player_->GetPos2D();
    float playerZ = player_->GetZ();

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        bool isAttacking = boss->GetBossAI().GetState() != BossAI::State::Wander;
        boss->SetAIDisabled(!bossAIEnabled_ && !isAttacking);
    }

    if (!hitStopActive && hitStopTimer_ <= 0.0f) {
        enemyMgr_.Update(dt, playerPos2D, playerZ, *player_);
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    // ===============================
    // 笘・繧ｯ繝ｩ繝ｳ繝怜芦驕斐メ繧ｧ繝・け
    // ===============================
    const float zNear = -10.0f;
    const float zFar = 20.0f;
    const float xMaxNear = 15.0f;
    const float xMaxFar = 20.0f;

    float z = player_->GetZ();
    float t = (z - zNear) / (zFar - zNear);
    t = std::clamp(t, 0.0f, 1.0f);
    float xMax = xMaxNear + (xMaxFar - xMaxNear) * t;

    float x = player_->GetX();

    // 笘・蜿ｳ遶ｯ縺ｫ蛻ｰ驕斐＠縺溘ｉ GameScene 縺ｸ

    if (enableEdgeTransition_ && !reachedEdge_ && x >= xMax - 0.01f) {
        reachedEdge_ = true;
        RequestChangeScene_("Game");
    }

    if (outOfBoundsEnabled_ && player_) {
        const Vector3 p = player_->GetPos3D();
        const bool isOut =
            p.x < outLeftX_ ||
            p.x > outRightX_ ||
            p.y < outBottomY_;

        if (isOut) {
            player_->SetDropRespawnPos(dropRespawnPos_);
            if (resetDamageOnOutOfBounds_) {
                player_->SetDamagePercent(0.0f);
            }
        }
    }

    if (knockbackPreviewLine_ && player_) {
        const bool launched = player_->IsLaunched();
        const bool justLaunched = launched && !previewLineWasLaunched_;
        const bool shouldFreezeLine = freezeKnockbackPreviewWhileLaunched_ && launched && !justLaunched;

        if (!shouldFreezeLine) {
        const size_t previewAttackIndex = std::min<size_t>(
            static_cast<size_t>(std::max(previewAttackKind_, 0)),
            enemyMgr_.BossAttackCount() > 0 ? enemyMgr_.BossAttackCount() - 1 : 0);
        const float percent = previewUsesPlayerPercent_ ? player_->GetDamagePercent() : previewPercent_;
        const TestSceneKnockbackPreview::Metrics metrics = TestSceneKnockbackPreview::Calculate(
            *player_,
            enemyMgr_,
            previewAttackIndex,
            percent,
            outOfBoundsEnabled_,
            outLeftX_,
            outRightX_,
            outBottomY_);

        const AABB body = player_->GetBodyAABB();
        const Vector3 previewStart{
            (body.min.x + body.max.x) * 0.5f,
            (body.min.y + body.max.y) * 0.5f,
            (body.min.z + body.max.z) * 0.5f,
        };
        Vector3 previewVec{};
        if (previewLineMode_ == 0) {
            const float previewDistance = metrics.reachesOutBeforeLanding ? metrics.outDistance : metrics.groundDistance;
            previewVec = {
                metrics.direction.x * previewDistance,
                metrics.direction.y * previewDistance,
                metrics.direction.z * previewDistance,
            };
        } else {
            previewVec = metrics.velocity;
        }

        const Vector3 previewEnd{
            previewStart.x + previewVec.x * previewLineScale_,
            previewStart.y + previewVec.y * previewLineScale_,
            previewStart.z + previewVec.z * previewLineScale_,
        };

        TestSceneKnockbackPreview::SetLineSegment(*knockbackPreviewLine_, previewStart, previewEnd, previewLineThickness_, dt);
        }

        previewLineWasLaunched_ = launched;
    }

    if (bossHitboxPreview_) {
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const size_t previewAttackIndex = std::min<size_t>(
                static_cast<size_t>(std::max(previewAttackKind_, 0)),
                enemyMgr_.BossAttackCount() > 0 ? enemyMgr_.BossAttackCount() - 1 : 0);
            const int facing = player_ && player_->GetX() < boss->GetPos3D().x ? -1 : 1;
            const EnemyManager::AABB3 box = enemyMgr_.MakeBossAttackHitbox(previewAttackIndex, boss->GetPos3D(), facing);
            bossHitboxPreview_->SetTranslate({ box.x, box.y, box.z });
            bossHitboxPreview_->SetScale({ box.hx, box.hy, box.hz });
            bossHitboxPreview_->Update(dt);
        }
    }

    player_->SetLighting(light_);
    enemyMgr_.SetLighting(light_);

    for (const auto& event : enemyMgr_.ConsumeBossAttackEffectEvents()) {
        if (event.kind == MeleeKind::Land) {
            EffectManager::GetInstance()->Play("fallAttak", event.position);
        }
    }

    const float effectDt = hitStopTimer_ > 0.0f ? 0.0f : dt;
    EffectManager::GetInstance()->Update(effectDt);
    ParticleManager::GetInstance()->Update(effectDt, *camera_);
   
}


// 繝昴せ繝医お繝輔ぉ繧ｯ繝亥ｯｾ雎｡縺ｮ3D謠冗判・医が繝輔せ繧ｯ繝ｪ繝ｼ繝ｳ縺ｸ・・
void TestScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

   // if (skyDome_) skyDome_->Draw();
    if (ground_) ground_->Draw();

    skyDome_->Draw();

    // LevelLoader 縺ｧ隱ｭ縺ｿ霎ｼ繧薙□繧ｪ繝悶ず繧ｧ繧ｯ繝医・謠冗判
    for (auto& obj : levelObjects_) {
        obj->Draw();
    }

    if (drawPointMarker_ && pointMarker_) pointMarker_->Draw();
    if (drawSpotMarker_ && spotMarker_) spotMarker_->Draw();

    if (player_) player_->Draw();
    if (drawKnockbackPreview_ && knockbackPreviewLine_) knockbackPreviewLine_->Draw();
    if (drawBossHitboxPreview_ && bossHitboxPreview_) bossHitboxPreview_->Draw();

#ifdef _DEBUG
    player_->DrawDebugHitBoxes(enemyMgr_);
#endif

    enemyMgr_.Draw();

    // 3D繧ｨ繝輔ぉ繧ｯ繝医が繝悶ず繧ｧ繧ｯ繝医・謠冗判
    EffectManager::GetInstance()->Draw();

    // GPU Particle
    app.ParticleCom()->SetGraphicsPipelineState();
    ParticleManager::GetInstance()->Draw(cmd);
}

// 繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｸ逶ｴ謗･謠上￥3D・医・繧ｹ繝医お繝輔ぉ繧ｯ繝井ｸ崎ｦ√↑繧ゅ・・・
void TestScene::Draw3D(GameApp& app) {
    // 莉翫・迚ｹ縺ｫ縺ｪ縺・
}

// 2D / Sprite
void TestScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0,
        float(WinApp::kClientWidth),
        float(WinApp::kClientHeight),
        0, 100
    );

    if (playTxst_) {
        playTxst_->Update(view, proj);
        playTxst_->Draw();
    }
}

// 縺昴・莉厄ｼ育ｩｺ縺ｧOK・・
void TestScene::Draw(GameApp& app) {
}


void TestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("Boss Knockback Test");

    ImGui::Checkbox("Boss AI Enabled", &bossAIEnabled_);
    ImGui::Checkbox("Enable Edge Transition", &enableEdgeTransition_);
    ImGui::Checkbox("Apply Hit Immediately", &applyBossHitImmediately_);
    ImGui::Checkbox("Draw Boss Hitbox Preview", &drawBossHitboxPreview_);

    auto clampPreviewAttackIndex = [&]() {
        const int attackCount = static_cast<int>(enemyMgr_.BossAttackCount());
        if (attackCount <= 0) {
            previewAttackKind_ = 0;
            return;
        }
        previewAttackKind_ = std::clamp(previewAttackKind_, 0, attackCount - 1);
    };

    auto makeAttackLabels = [&]() {
        std::vector<const char*> labels;
        labels.reserve(enemyMgr_.BossAttackCount());
        for (size_t i = 0; i < enemyMgr_.BossAttackCount(); ++i) {
            labels.push_back(enemyMgr_.BossAttackAt(i).name.c_str());
        }
        return labels;
    };

    clampPreviewAttackIndex();

    if (player_) {
        Vector3 p = player_->GetPos3D();
        ImGui::Text("Player Pos: %.2f, %.2f, %.2f", p.x, p.y, p.z);
        ImGui::Text("Player Damage: %.1f%%", player_->GetDamagePercent());
        ImGui::Text("Launched: %s", player_->IsLaunched() ? "true" : "false");

        float percent = player_->GetDamagePercent();
        if (ImGui::DragFloat("Player Damage Percent", &percent, 1.0f, 0.0f, 999.0f)) {
            player_->SetDamagePercent(percent);
        }
    }

    auto triggerBossTestHit = [&](Enemy& boss, size_t attackIndex) {
        if (attackIndex <= enemyMgr_.BossAttackIndex(MeleeKind::Rush)) {
            boss.RequestMelee(TestSceneKnockbackPreview::KindFromIndex(static_cast<int>(attackIndex)));
        } else {
            enemyMgr_.QueueBossAttackHitbox(boss, attackIndex, player_ ? player_->GetX() : boss.GetPos3D().x + 1.0f);
        }

        if (!applyBossHitImmediately_ || !player_) {
            return;
        }

        EnemyManager::BossHitTuning tuning = enemyMgr_.BossAttackAt(attackIndex).hit;
        Vector3 dir = tuning.knockbackDir;
        const float dirX = (player_->GetX() >= boss.GetPos3D().x) ? 1.0f : -1.0f;
        dir.x = std::abs(dir.x) * dirX;

        player_->ApplyBossHit(
            tuning.damagePercent,
            tuning.baseKnockback,
            tuning.knockbackScale,
            dir,
            tuning.hitStunSec);
        const EnemyManager::HitStopTuning& hitStop = enemyMgr_.HitStop();
        if (hitStop.enabled) {
            hitStopTimer_ = std::max(hitStopTimer_, hitStop.bossAttackSec);
        }
    };

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        Vector3 bossPos = boss->GetPos3D();
        if (ImGui::DragFloat3("Boss Pos", &bossPos.x, 0.1f)) {
            boss->SetPos(bossPos);
        }

        if (ImGui::Button("Boss Normal")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Melee_Dash);
            triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Normal));
        }
        ImGui::SameLine();
        if (ImGui::Button("Boss Jump Slash")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Drop_Windup);
            triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Land));
        }
        ImGui::SameLine();
        if (ImGui::Button("Boss Rush")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Rush_ToRight);
            triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Rush));
        }
        if (previewAttackKind_ > static_cast<int>(enemyMgr_.BossAttackIndex(MeleeKind::Rush))) {
            if (ImGui::Button("Boss Selected Custom")) {
                triggerBossTestHit(*boss, static_cast<size_t>(previewAttackKind_));
            }
        }
    } else {
        ImGui::TextUnformatted("Boss: none");
    }

    if (ImGui::Button("Reset Fighter Positions")) {
        resetFightersRequested_ = true;
    }

    if (ImGui::CollapsingHeader("Save / Load Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Tuning Path", bossTuningPath_, IM_ARRAYSIZE(bossTuningPath_));
        if (ImGui::Button("Save Attack Tuning")) {
            TestSceneBossTuning::Save(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Attack Tuning")) {
            if (TestSceneBossTuning::Load(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_)) {
                clampPreviewAttackIndex();
                previewLineWasLaunched_ = false;
            }
        }
        if (!bossTuningStatus_.empty()) {
            ImGui::TextUnformatted(bossTuningStatus_.c_str());
        }
    }

    if (ImGui::CollapsingHeader("HitStop", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyManager::HitStopTuning& hitStop = enemyMgr_.HitStop();
        ImGui::Checkbox("Enable HitStop", &hitStop.enabled);
        ImGui::DragFloat("Player Attack Sec", &hitStop.playerAttackSec, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("Boss Attack Sec", &hitStop.bossAttackSec, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::Text("Current Timer: %.3f", hitStopTimer_);
    }

    if (ImGui::CollapsingHeader("Attack Creation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("New Attack Name", newBossAttackName_, IM_ARRAYSIZE(newBossAttackName_));
        if (ImGui::Button("Add Attack")) {
            previewAttackKind_ = static_cast<int>(enemyMgr_.AddCustomBossAttack(newBossAttackName_));
            previewLineWasLaunched_ = false;
        }
        ImGui::SameLine();
        const bool canRemove = enemyMgr_.BossAttackAt(static_cast<size_t>(previewAttackKind_)).custom;
        if (!canRemove) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Remove Selected")) {
            if (enemyMgr_.RemoveCustomBossAttack(static_cast<size_t>(previewAttackKind_))) {
                clampPreviewAttackIndex();
                previewLineWasLaunched_ = false;
            }
        }
        if (!canRemove) {
            ImGui::EndDisabled();
        }

        std::vector<const char*> attackLabels = makeAttackLabels();
        if (!attackLabels.empty()) {
            ImGui::Combo("Selected Attack", &previewAttackKind_, attackLabels.data(), static_cast<int>(attackLabels.size()));
        }

        EnemyManager::BossAttackDefinition& selectedAttack = enemyMgr_.BossAttackAt(static_cast<size_t>(previewAttackKind_));
        char attackName[128]{};
        std::snprintf(attackName, sizeof(attackName), "%s", selectedAttack.name.c_str());
        if (ImGui::InputText("Selected Name", attackName, IM_ARRAYSIZE(attackName))) {
            selectedAttack.name = attackName;
        }
        ImGui::Text("Custom: %s", selectedAttack.custom ? "true" : "false");
    }

    if (ImGui::CollapsingHeader("Out Of Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &outOfBoundsEnabled_);
        ImGui::DragFloat("Left X", &outLeftX_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat("Right X", &outRightX_, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Bottom Y", &outBottomY_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat3("Drop Respawn Pos", &dropRespawnPos_.x, 0.1f);
        ImGui::Checkbox("Reset Damage On Out", &resetDamageOnOutOfBounds_);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Next Work", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("Add recovery special after launch");
        ImGui::BulletText("Promote test respawn into proper stock/KO flow");
        ImGui::BulletText("Add KO effect before drop respawn or result transition");
        ImGui::BulletText("Move tuned boss hit values into battle-side defaults");
    }

    ImGui::End();

    ImGui::Begin("Boss Attack Tuning");

    if (ImGui::CollapsingHeader("Knockback", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<const char*> attackLabels = makeAttackLabels();
        const char* lineModeLabels[] = { "Actual Distance", "Launch Velocity" };
        ImGui::Checkbox("Draw Preview Line", &drawKnockbackPreview_);
        ImGui::Checkbox("Freeze Line While Launched", &freezeKnockbackPreviewWhileLaunched_);
        ImGui::Combo("Preview Line Mode", &previewLineMode_, lineModeLabels, 2);
        if (!attackLabels.empty()) {
            ImGui::Combo("Preview Attack", &previewAttackKind_, attackLabels.data(), static_cast<int>(attackLabels.size()));
        }
        ImGui::Checkbox("Use Player Damage", &previewUsesPlayerPercent_);
        if (!previewUsesPlayerPercent_) {
            ImGui::DragFloat("Preview Percent", &previewPercent_, 1.0f, 0.0f, 999.0f);
        }
        const char* scaleLabel = previewLineMode_ == 0 ? "Distance Line Scale (1 = actual)" : "Velocity Line Scale";
        ImGui::DragFloat(scaleLabel, &previewLineScale_, 0.01f, 0.01f, 5.0f);
        ImGui::DragFloat("Line Thickness", &previewLineThickness_, 0.01f, 0.01f, 2.0f);

        const size_t previewAttackIndex = static_cast<size_t>(previewAttackKind_);
        const float percent = previewUsesPlayerPercent_ && player_ ? player_->GetDamagePercent() : previewPercent_;
        if (player_) {
            const TestSceneKnockbackPreview::Metrics metrics = TestSceneKnockbackPreview::Calculate(
                *player_,
                enemyMgr_,
                previewAttackIndex,
                percent,
                outOfBoundsEnabled_,
                outLeftX_,
                outRightX_,
                outBottomY_);

            ImGui::SeparatorText("Actual Prediction");
            ImGui::Text("Launch Velocity: %.2f, %.2f, %.2f", metrics.velocity.x, metrics.velocity.y, metrics.velocity.z);
            ImGui::Text("Launch Speed: %.2f", metrics.power);
            ImGui::Text("Launch Angle: %.1f deg", metrics.launchAngleDeg);
            ImGui::Text("Screen Angle: %.1f deg", metrics.signedScreenAngleDeg);
            ImGui::Text("Air Time: %.2f sec", metrics.airTimeSec);
            ImGui::Text("Travel X: %.2f", metrics.travelX);
            ImGui::Text("Ground Distance: %.2f", metrics.groundDistance);
            ImGui::Text("Straight Distance: %.2f", metrics.straightDistance);
            ImGui::Text("Max Height Y: %.2f", metrics.maxHeightY);
            ImGui::Text("Landing Pos: %.2f, %.2f, %.2f", metrics.landingPos.x, metrics.landingPos.y, metrics.landingPos.z);

            if (metrics.reachesOutBeforeLanding) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.55f, 0.15f, 1.0f),
                    "Out Before Landing: %.2f sec / %.2f distance",
                    metrics.outTimeSec,
                    metrics.outDistance);
                ImGui::Text("Out Pos: %.2f, %.2f, %.2f", metrics.outPos.x, metrics.outPos.y, metrics.outPos.z);
            } else {
                ImGui::TextUnformatted("Out Before Landing: false");
            }
        }

        auto drawBossTuning = [](const char* label, EnemyManager::BossHitTuning& tuning) {
            ImGui::PushID(label);
            if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PopID();
                return;
            }
            ImGui::DragFloat("Damage Percent", &tuning.damagePercent, 0.5f, 0.0f, 100.0f);
            ImGui::DragFloat("Base Knockback", &tuning.baseKnockback, 0.1f, 0.0f, 80.0f);
            ImGui::DragFloat("Knockback Scale", &tuning.knockbackScale, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat3("Knockback Dir", &tuning.knockbackDir.x, 0.01f, -2.0f, 2.0f);
            ImGui::DragFloat("Hit Stun Sec", &tuning.hitStunSec, 0.01f, 0.0f, 3.0f);
            ImGui::PopID();
        };

        for (size_t i = 0; i < enemyMgr_.BossAttackCount(); ++i) {
            drawBossTuning(enemyMgr_.BossAttackAt(i).name.c_str(), enemyMgr_.BossAttackAt(i).hit);
        }
    }

    if (ImGui::CollapsingHeader("HitBox", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<const char*> attackLabels = makeAttackLabels();
        if (!attackLabels.empty()) {
            ImGui::Combo("Preview Hitbox Attack", &previewAttackKind_, attackLabels.data(), static_cast<int>(attackLabels.size()));
        }

        auto drawBossHitboxTuning = [](const char* label, EnemyManager::BossAttackHitboxTuning& tuning) {
            ImGui::PushID(label);
            if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PopID();
                return;
            }
            ImGui::DragFloat3("Offset", &tuning.offset.x, 0.05f, -20.0f, 20.0f);
            ImGui::DragFloat3("Half Size", &tuning.halfSize.x, 0.05f, 0.01f, 20.0f);
            ImGui::DragFloat("Start Delay Sec", &tuning.startDelaySec, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Active Sec", &tuning.activeSec, 0.01f, 0.01f, 5.0f);
            ImGui::DragInt("Damage", &tuning.damage, 1, 0, 999);
            ImGui::PopID();
        };

        for (size_t i = 0; i < enemyMgr_.BossAttackCount(); ++i) {
            drawBossHitboxTuning(enemyMgr_.BossAttackAt(i).name.c_str(), enemyMgr_.BossAttackAt(i).hitbox);
        }
    }

    if (player_ && ImGui::CollapsingHeader("Player U Attacks", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto drawPlayerAttack = [](const char* label, Player::PlayerAttackDefinition& attack) {
            ImGui::PushID(label);
            if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PopID();
                return;
            }

            char name[128]{};
            std::snprintf(name, sizeof(name), "%s", attack.name.c_str());
            if (ImGui::InputText("Name", name, IM_ARRAYSIZE(name))) {
                attack.name = name;
            }
            ImGui::DragFloat3("Offset", &attack.offset.x, 0.05f, -20.0f, 20.0f);
            ImGui::DragFloat3("Half Size", &attack.halfSize.x, 0.05f, 0.01f, 20.0f);
            ImGui::DragFloat("Start Delay Sec", &attack.startDelaySec, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Active Sec", &attack.activeSec, 0.01f, 0.01f, 5.0f);
            ImGui::DragFloat("Action Sec", &attack.actionSec, 0.01f, 0.01f, 5.0f);
            ImGui::DragInt("Damage", &attack.damage, 1, 0, 999);
            ImGui::PopID();
        };

        for (int groupIndex = 0; groupIndex < static_cast<int>(Player::PlayerAttackGroup::Count); ++groupIndex) {
            const auto group = static_cast<Player::PlayerAttackGroup>(groupIndex);
            if (!ImGui::TreeNodeEx(Player::AttackGroupName(group), ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }
            for (int variantIndex = 0; variantIndex < static_cast<int>(Player::PlayerAttackVariant::Count); ++variantIndex) {
                const auto variant = static_cast<Player::PlayerAttackVariant>(variantIndex);
                drawPlayerAttack(Player::AttackVariantName(variant), player_->AttackDefinition(group, variant));
            }
            ImGui::TreePop();
        }
    }

    ImGui::End();
#endif
}

#if 0

    // ===== ImGui =====
    ImGui::Begin("Camera Debug");
    ImGui::End();

    ImGui::Begin("Ground PointLight");

    ImGui::Checkbox("Point Only (ground)", &groundPointOnly_);

    ImGui::DragFloat3("Point Pos", &groundLight_.pointPos.x, 0.1f);
    ImGui::DragFloat("Point Intensity", &groundLight_.pointIntensity, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloat("Point Radius", &groundLight_.pointRadius, 0.1f, 0.1f, 200.0f);
    ImGui::DragFloat("Point Decay", &groundLight_.pointDecay, 0.01f, 0.0f, 10.0f);

    ImGui::ColorEdit3("Point Color", &groundLight_.pointColor.x);

    if (ImGui::Button("Reset")) {
        groundLight_.dirIntensity = 0.0f;
        groundLight_.spotIntensity = 0.0f;
        groundLight_.pointIntensity = 2.5f;
        groundLight_.pointPos = { 0.0f, 5.0f, 15.0f };
        groundLight_.pointRadius = 15.0f;
        groundLight_.pointDecay = 1.0f;
        groundLight_.pointColor = { 1.0f, 1.0f, 1.0f };
    }

    ImGui::Checkbox("Draw Point Marker", &drawPointMarker_);
    ImGui::DragFloat("Marker Scale", &pointMarkerScale_, 0.01f, 0.01f, 5.0f);

    ImGui::End();

    ImGui::Begin("Ground SpotLight");

    ImGui::Checkbox("Spot Only (ground)", &groundSpotOnly_);

    ImGui::DragFloat3("Spot Pos", &groundLight_.spotPos.x, 0.1f);
    ImGui::DragFloat3("Spot Dir", &groundLight_.spotDir.x, 0.01f, -1.0f, 1.0f);

    // 笘・ir縺ｯ豁｣隕丞喧縺励↑縺・→螢翫ｌ繧・☆縺・・縺ｧ縲√・繧ｿ繝ｳ縺ｧ豁｣隕丞喧繧ょ・繧後ｋ
    if (ImGui::Button("Normalize Dir")) {
        Vector3 d = groundLight_.spotDir;
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-6f) { groundLight_.spotDir = { d.x / len, d.y / len, d.z / len }; }
    }

    ImGui::DragFloat("Spot Intensity", &groundLight_.spotIntensity, 0.05f, 0.0f, 200.0f);
    ImGui::DragFloat("Spot Distance", &groundLight_.spotDistance, 0.1f, 0.1f, 500.0f);
    ImGui::DragFloat("Spot Decay", &groundLight_.spotDecay, 0.01f, 0.0f, 10.0f);

    ImGui::DragFloat("Spot Angle (deg)", &groundLight_.spotAngleDeg, 0.1f, 1.0f, 89.0f);
    ImGui::DragFloat("Falloff Start (deg)", &groundLight_.spotFalloffStartDeg, 0.1f, 0.0f, 89.0f);

    if (groundLight_.spotFalloffStartDeg > groundLight_.spotAngleDeg - 0.1f) {
        groundLight_.spotFalloffStartDeg = groundLight_.spotAngleDeg - 0.1f;
    }

    ImGui::ColorEdit3("Spot Color", &groundLight_.spotColor.x);

    if (ImGui::Button("Reset Spot")) {
        groundLight_.dirIntensity = 0.0f;
        groundLight_.pointIntensity = 0.0f;

        groundLight_.spotIntensity = 20.0f;
        groundLight_.spotPos = { 0.0f, 15.0f, 15.0f };
        groundLight_.spotDir = { 0.0f, -1.0f, 0.0f };
        groundLight_.spotDistance = 80.0f;
        groundLight_.spotDecay = 1.0f;
        groundLight_.spotAngleDeg = 25.0f;
        groundLight_.spotFalloffStartDeg = 15.0f;
        groundLight_.spotColor = { 1.0f,1.0f,1.0f };
    }

    ImGui::Checkbox("Draw Spot Marker", &drawSpotMarker_);
    ImGui::DragFloat("Marker Scale", &spotMarkerScale_, 0.01f, 0.01f, 5.0f);

    ImGui::End();
#endif
