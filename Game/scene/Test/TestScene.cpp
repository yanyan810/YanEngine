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
#include <cctype>
#include <limits>

#ifdef USE_IMGUI
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gTestSceneAttackTuningSwitcherVisible;
extern int gTestSceneAttackTuningTarget;
#endif

namespace {

constexpr float kKnockbackPreviewLineThicknessToPixels = 40.0f;

bool ProjectWorldToRect(
    const Camera& camera,
    const Vector3& world,
    float rectMinX,
    float rectMinY,
    float rectWidth,
    float rectHeight,
    Vector2& out) {
    const Matrix4x4& vp = camera.GetViewProjectionMatrix();
    const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
    const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
    const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
    if (w <= 0.001f) {
        return false;
    }

    const float ndcX = x / w;
    const float ndcY = y / w;
    out.x = rectMinX + (ndcX * 0.5f + 0.5f) * rectWidth;
    out.y = rectMinY + (0.5f - ndcY * 0.5f) * rectHeight;
    return true;
}

bool ProjectWorldToScreen(const Camera& camera, const Vector3& world, Vector2& out) {
    return ProjectWorldToRect(
        camera,
        world,
        0.0f,
        0.0f,
        static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight),
        out);
}

std::vector<Vector3> SimulateKnockbackTrajectory(
    const Player& player,
    const Vector3& start,
    const Vector3& initialVelocity,
    float hitStunSec,
    bool outOfBoundsEnabled,
    float outLeftX,
    float outRightX,
    float outBottomY,
    float outTopY,
    float scale) {
    constexpr float kStep = 1.0f / 60.0f;
    constexpr float kMaxTime = 8.0f;

    const float gravity = player.GetGravity();
    const float initialSpeed = std::sqrt(
        initialVelocity.x * initialVelocity.x +
        initialVelocity.y * initialVelocity.y +
        initialVelocity.z * initialVelocity.z);
    const float totalTime = std::max(0.0f, hitStunSec);

    Vector3 pos = start;
    Vector3 vel = initialVelocity;
    float timer = totalTime;
    std::vector<Vector3> points;
    points.reserve(64);
    points.push_back(start);

    const float left = std::min(outLeftX, outRightX);
    const float right = std::max(outLeftX, outRightX);
    const float bottom = std::min(outBottomY, outTopY);
    const float top = std::max(outBottomY, outTopY);
    const float pointScale = std::max(0.0f, scale);

    auto pushScaledPoint = [&](const Vector3& p) {
        points.push_back({
            start.x + (p.x - start.x) * pointScale,
            start.y + (p.y - start.y) * pointScale,
            start.z + (p.z - start.z) * pointScale,
        });
    };

    for (float elapsed = 0.0f; elapsed < kMaxTime; elapsed += kStep) {
        const Vector3 prev = pos;

        vel.y -= gravity * kStep;

        float drag = 1.0f;
        if (player.GetLaunchDragUseTime()) {
            const float timeRatio = (totalTime > 0.0f) ? (timer / totalTime) : 0.0f;
            drag = timeRatio >= player.GetLaunchDragThreshold()
                ? player.GetLaunchXZDragHigh()
                : player.GetLaunchXZDragLow();
        } else {
            const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            const float speedRatio = (initialSpeed > 1.0e-4f) ? (speed / initialSpeed) : 0.0f;
            drag = speedRatio >= player.GetLaunchDragThreshold()
                ? player.GetLaunchXZDragHigh()
                : player.GetLaunchXZDragLow();
        }

        if (drag < 1.0f) {
            const float dragMul = std::pow(drag, kStep);
            vel.x *= dragMul;
            vel.z *= dragMul;
        }

        pos.x += vel.x * kStep;
        pos.y += vel.y * kStep;
        pos.z += vel.z * kStep;
        timer = std::max(0.0f, timer - kStep);

        if (pos.y <= 0.0f && vel.y <= 0.0f) {
            const float denom = prev.y - pos.y;
            const float t = (std::abs(denom) > 1.0e-5f) ? std::clamp(prev.y / denom, 0.0f, 1.0f) : 1.0f;
            const Vector3 hitGround{
                prev.x + (pos.x - prev.x) * t,
                0.0f,
                prev.z + (pos.z - prev.z) * t,
            };
            pushScaledPoint(hitGround);
            break;
        }

        if (outOfBoundsEnabled &&
            (pos.x < left || pos.x > right || pos.y < bottom || pos.y > top)) {
            pushScaledPoint(pos);
            break;
        }

        if (points.empty() || elapsed == 0.0f || (static_cast<int>(elapsed / kStep) % 3) == 0) {
            pushScaledPoint(pos);
        }
    }

    if (points.size() == 1) {
        pushScaledPoint(pos);
    }
    return points;
}

bool FitAABBToObject(Object3d& object, Vector3& outCenter, Vector3& outHalfSize, float padding) {
    Model* model = object.GetModel();
    if (!model) {
        return false;
    }

    AABB local{};
    if (!model->GetLocalAABB(local)) {
        return false;
    }

    const Vector3 translate = object.GetTranslate();
    const Vector3 scale = object.GetScale();
    Vector3 worldMin{
        local.min.x * scale.x + translate.x,
        local.min.y * scale.y + translate.y,
        local.min.z * scale.z + translate.z,
    };
    Vector3 worldMax{
        local.max.x * scale.x + translate.x,
        local.max.y * scale.y + translate.y,
        local.max.z * scale.z + translate.z,
    };

    if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
    if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
    if (worldMin.z > worldMax.z) std::swap(worldMin.z, worldMax.z);

    const float pad = std::max(0.0f, padding);
    worldMin.x -= pad;
    worldMin.y -= pad;
    worldMin.z -= pad;
    worldMax.x += pad;
    worldMax.y += pad;
    worldMax.z += pad;

    outCenter = {
        (worldMin.x + worldMax.x) * 0.5f,
        (worldMin.y + worldMax.y) * 0.5f,
        (worldMin.z + worldMax.z) * 0.5f,
    };
    outHalfSize = {
        std::max((worldMax.x - worldMin.x) * 0.5f, 0.01f),
        std::max((worldMax.y - worldMin.y) * 0.5f, 0.01f),
        std::max((worldMax.z - worldMin.z) * 0.5f, 0.01f),
    };
    return true;
}

bool FitMeshAABBsToObject(Object3d& object, std::vector<TestScene::MeshCollisionInfo>& outMeshes, float padding) {
    Model* model = object.GetModel();
    if (!model) {
        return false;
    }

    std::vector<Model::MeshCollisionData> localAABBs = model->GetMeshesLocalAABBs();
    if (localAABBs.empty()) {
        return false;
    }

    const Vector3 translate = object.GetTranslate();
    const Vector3 scale = object.GetScale();
    const float pad = std::max(0.0f, padding);

    outMeshes.clear();
    outMeshes.reserve(localAABBs.size());

    for (const auto& local : localAABBs) {
        Vector3 worldMin{
            local.localAABB.min.x * scale.x + translate.x,
            local.localAABB.min.y * scale.y + translate.y,
            local.localAABB.min.z * scale.z + translate.z,
        };
        Vector3 worldMax{
            local.localAABB.max.x * scale.x + translate.x,
            local.localAABB.max.y * scale.y + translate.y,
            local.localAABB.max.z * scale.z + translate.z,
        };

        if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
        if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
        if (worldMin.z > worldMax.z) std::swap(worldMin.z, worldMax.z);

        worldMin.x -= pad;
        worldMin.y -= pad;
        worldMin.z -= pad;
        worldMax.x += pad;
        worldMax.y += pad;
        worldMax.z += pad;

        TestScene::MeshCollisionInfo info{};
        info.name = local.name;
        info.worldAABB.min = worldMin;
        info.worldAABB.max = worldMax;

        // フィルタリングロジックの適用
        std::string nameLower = info.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return std::tolower(c); });
        
        bool shouldDisable = false;
        const std::vector<std::string> excludeKeywords = {
            "sky", "dome", "cloud", "bg", "background", "water", "sea", "ocean", 
            "light", "marker", "camera", "player", "enemy", "boss", "preview"
        };
        for (const auto& keyword : excludeKeywords) {
            if (nameLower.find(keyword) != std::string::npos) {
                shouldDisable = true;
                break;
            }
        }

        const float halfX = (worldMax.x - worldMin.x) * 0.5f;
        const float halfY = (worldMax.y - worldMin.y) * 0.5f;
        const float halfZ = (worldMax.z - worldMin.z) * 0.5f;
        if (halfX > 100.0f || halfY > 100.0f || halfZ > 100.0f) {
            shouldDisable = true;
        }

        if (worldMax.z < -20.0f || worldMin.z > 25.0f) {
            shouldDisable = true;
        }

        if (halfX < 0.05f && halfY < 0.05f && halfZ < 0.05f) {
            shouldDisable = true;
        }

        info.enabled = !shouldDisable;
        outMeshes.push_back(info);
    }

    return true;
}

} // namespace

void TestScene::OnEnter(GameApp& app) {
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = true;
#endif
  //  TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

    input_ = app.GetInput();
    assert(input_);

    camera_ = std::make_unique<Camera>();

    // GameSceneと同じカメラでOK
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
    
    // ★重要：GetEnemies() はデバッグ確認にも使うので存在している前提
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


    // どこかで（OnEnterの中など）
    //auto* mgr = ModelManager::GetInstance();
    //mgr->LoadModel("ground/ground.obj");   // resources/ground/ground.obj 繧呈Φ螳・

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/ground.obj");

    

    // 位置・大きさは好みで調整
    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
    ground_->SetEnableLighting(2);     // 2はハーフランバート
    ground_->SetIntensity(2.0f);
    ground_->SetLightColor(light_.dirColor);
    ground_->SetEnableLighting(0);
    // Groundはpointだけ使う
    groundLight_ = light_;              // とりあえず既存をコピーしてもOK
    groundLight_.dirIntensity = 0.0f;   // ★Directional 無効
    groundLight_.spotIntensity = 0.0f;  // ★Spot 無効

    groundLight_.pointIntensity = 16.0f;
    groundLight_.pointPos = { 0.0f, -42.0f, -1.0f };
    groundLight_.pointRadius = 200.0f;
    groundLight_.pointDecay = 1.0f;
    groundLight_.pointColor = { 1.0f, 1.0f, 1.0f }; // Vector3諠ｳ螳・

    // Spot ON
    groundLight_.spotIntensity = 20.0f;
    groundLight_.spotPos = { 0.0f, 15.0f, 15.0f };

    // ★direction は「どこを向くか」
    // とりあえず地面の中央へ向ける（後で毎フレ更新してもOK）
    Vector3 target = { 0.0f, 0.0f, 15.0f };
    Vector3 d = { target.x - groundLight_.spotPos.x, target.y - groundLight_.spotPos.y, target.z - groundLight_.spotPos.z };
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-6f) { d.x /= len; d.y /= len; d.z /= len; }
    }
    groundLight_.spotDir = d;

    groundLight_.spotDistance = 80.0f;
    groundLight_.spotDecay = 1.0f;

    // 角度（degree）管理している前提
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
    pointMarker_->SetEnableLighting(0);                 // ★ライトの影響を受けない
    pointMarker_->SetMaterialColor({ 1, 1, 0, 1 });     // 黄色とか（好みで）
    pointMarker_->SetShininess(1.0f);
    pointMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");

    // ★スカイドームは基本「ライト無視」
    skyDome_->SetEnableLighting(0);              // ← あなたの仕様の「無照明モード」に合わせて
    skyDome_->SetMaterialColor({ 1,1,1,1 });       // 念のため
    skyDome_->SetShininess(1.0f);                // 影響しないけど一応設定
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    knockbackPreviewLine_ = std::make_unique<Sprite>();
    knockbackPreviewLine_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    knockbackPreviewLine_->SetAnchorPoint({ 0.0f, 0.5f });
    knockbackPreviewLine_->SetColor({ 1.0f, 0.15f, 0.05f, 1.0f });

    bossHitboxPreview_ = std::make_unique<Object3d>();
    bossHitboxPreview_->Initialize(app.ObjCom(), app.Dx());
    bossHitboxPreview_->SetCamera(camera_.get());
    bossHitboxPreview_->SetModel("cube/cube.obj");
    bossHitboxPreview_->SetEnableLighting(0);
    bossHitboxPreview_->SetMaterialColor({ 0.1f, 0.8f, 1.0f, 0.35f });
    bossHitboxPreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    outLeftPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.1f, 0.05f, 0.45f });
    outRightPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.1f, 0.05f, 0.45f });
    outBottomPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.75f, 0.05f, 0.45f });
    outTopPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.75f, 0.05f, 0.45f });
    if (autoFitGroundCollisionToObj_ && ground_) {
        FitMeshAABBsToObject(*ground_, groundMeshes_, groundCollisionPadding_);
        groundCollisionPreviews_.clear();
        for (const auto& mesh : groundMeshes_) {
            groundCollisionPreviews_.push_back(CreateBoundaryPreview(app, { 0.1f, 1.0f, 0.35f, 0.35f }));
        }
    }

    // ===== LevelLoader: JSONからオブジェクトを読み込む =====
    // 1. BlenderアドオンでJSONをエクスポートする
    // 2. resources/levels/stage1.json に配置する
    // 3. 下のコメントアウトを外す
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
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = false;
#endif
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

    // LevelLoader で読み込んだオブジェクトの更新
    for (auto& obj : levelObjects_) {
        obj->Update(dt);
    }

    const bool hitStopActive = hitStopTimer_ > 0.0f;
    if (hitStopActive) {
        hitStopTimer_ = std::max(0.0f, hitStopTimer_ - dt);
    }

    if (!hitStopActive && player_) {
        player_->Update(dt, *input_, enemyMgr_);
        const auto playerAttackHits = enemyMgr_.ApplyPlayerAttack(*player_);
        if (!playerAttackHits.empty()) {
            player_->NotifyAttackHit();
        }
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    Vector2 playerPos2D = player_->GetPos2D();
    float playerZ = player_->GetZ();

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        // bossAIEnabled_ が OFF でも、攻撃シーケンス中（Wander以外）はAIを動かし続ける
        // Wander（シーケンス終了）に戻ったときだけ無効化する
        const bool isAttacking = boss->GetBossAI().GetState() != BossAI::State::Wander;
        boss->SetAIDisabled(!bossAIEnabled_ && !isAttacking);
    }

    if (!hitStopActive && hitStopTimer_ <= 0.0f) {
        enemyMgr_.Update(dt, playerPos2D, playerZ, *player_);
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    if (dynamicBattleCamera_ && player_) {
        Vector3 target = player_->GetPos3D();
        float battleDistance = 0.0f;
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const Vector3 bossPos = boss->GetPos3D();
            const Vector3 playerPos = player_->GetPos3D();
            target.x = (playerPos.x + bossPos.x) * 0.5f;
            target.y = (playerPos.y + bossPos.y) * 0.5f;
            target.z = (playerPos.z + bossPos.z) * 0.5f;
            const float dx = playerPos.x - bossPos.x;
            const float dz = playerPos.z - bossPos.z;
            battleDistance = std::sqrt(dx * dx + dz * dz);
        }

        const float cameraDistance = std::clamp(
            battleCameraMinDistance_ + battleDistance * battleCameraDistanceScale_,
            battleCameraMinDistance_,
            battleCameraMaxDistance_);
        const Vector3 desiredCamera{
            target.x,
            battleCameraHeight_,
            target.z - cameraDistance,
        };
        const Vector3 currentCamera = camera_->GetTranslate();
        const float follow = std::clamp(dt * battleCameraFollowLerp_, 0.0f, 1.0f);
        camera_->SetTranslate({
            currentCamera.x + (desiredCamera.x - currentCamera.x) * follow,
            currentCamera.y + (desiredCamera.y - currentCamera.y) * follow,
            currentCamera.z + (desiredCamera.z - currentCamera.z) * follow,
        });
        camera_->SetRotate({ 0.35f, 0.0f, 0.0f });
        camera_->Update();
    }

    // ===============================
    // ★ クランプ・到達チェック
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

    // ★ 右端に到達したら GameScene へ

    if (enableEdgeTransition_ && !reachedEdge_ && x >= xMax - 0.01f) {
        reachedEdge_ = true;
        RequestChangeScene_("Game");
    }

    if (groundCollisionEnabled_ && player_) {
        std::vector<AABB> activeGrounds;
        for (const auto& mesh : groundMeshes_) {
            if (mesh.enabled) {
                activeGrounds.push_back(mesh.worldAABB);
            }
        }
        if (!activeGrounds.empty()) {
            player_->ResolveObstaclesAABB(activeGrounds);
        }
    }

    if (outOfBoundsEnabled_ && player_) {
        const Vector3 p = player_->GetPos3D();
        const float left = std::min(outLeftX_, outRightX_);
        const float right = std::max(outLeftX_, outRightX_);
        const float bottom = std::min(outBottomY_, outTopY_);
        const float top = std::max(outBottomY_, outTopY_);
        const bool isOut =
            p.x < left ||
            p.x > right ||
            p.y < bottom ||
            p.y > top;

        if (isOut) {
            player_->SetDropRespawnPos(dropRespawnPos_);
            if (resetDamageOnOutOfBounds_) {
                player_->SetDamagePercent(0.0f);
            }
        }
    }

    if (outLeftPreview_ && outRightPreview_ && outBottomPreview_ && outTopPreview_) {
        const float left = std::min(outLeftX_, outRightX_);
        const float right = std::max(outLeftX_, outRightX_);
        const float bottom = std::min(outBottomY_, outTopY_);
        const float top = std::max(outBottomY_, outTopY_);
        const float zNear = std::min(outPreviewZNear_, outPreviewZFar_);
        const float zFar = std::max(outPreviewZNear_, outPreviewZFar_);
        const float xCenter = (left + right) * 0.5f;
        const float yCenter = (bottom + top) * 0.5f;
        const float zCenter = (zNear + zFar) * 0.5f;
        const float xHalf = std::max((right - left) * 0.5f, outPreviewThickness_);
        const float yHalf = std::max((top - bottom) * 0.5f, outPreviewThickness_);
        const float zHalf = std::max((zFar - zNear) * 0.5f, outPreviewThickness_);

        outLeftPreview_->SetTranslate({ left, yCenter, zCenter });
        outLeftPreview_->SetScale({ outPreviewThickness_, yHalf, zHalf });
        outLeftPreview_->Update(dt);

        outRightPreview_->SetTranslate({ right, yCenter, zCenter });
        outRightPreview_->SetScale({ outPreviewThickness_, yHalf, zHalf });
        outRightPreview_->Update(dt);

        outBottomPreview_->SetTranslate({ xCenter, bottom, zCenter });
        outBottomPreview_->SetScale({ xHalf, outPreviewThickness_, zHalf });
        outBottomPreview_->Update(dt);

        outTopPreview_->SetTranslate({ xCenter, top, zCenter });
        outTopPreview_->SetScale({ xHalf, outPreviewThickness_, zHalf });
        outTopPreview_->Update(dt);
    }

    if (groundCollisionPreviews_.size() == groundMeshes_.size()) {
        for (size_t i = 0; i < groundMeshes_.size(); ++i) {
            const auto& mesh = groundMeshes_[i];
            auto& preview = groundCollisionPreviews_[i];
            if (preview && mesh.enabled) {
                const Vector3 center = {
                    (mesh.worldAABB.min.x + mesh.worldAABB.max.x) * 0.5f,
                    (mesh.worldAABB.min.y + mesh.worldAABB.max.y) * 0.5f,
                    (mesh.worldAABB.min.z + mesh.worldAABB.max.z) * 0.5f,
                };
                const Vector3 halfSize = {
                    (mesh.worldAABB.max.x - mesh.worldAABB.min.x) * 0.5f,
                    (mesh.worldAABB.max.y - mesh.worldAABB.min.y) * 0.5f,
                    (mesh.worldAABB.max.z - mesh.worldAABB.min.z) * 0.5f,
                };
                preview->SetTranslate(center);
                preview->SetScale(halfSize);
                preview->Update(dt);
            }
        }
    }

    if (knockbackPreviewLine_ && player_) {
        const bool launched = player_->IsLaunched();
        const bool shouldFreezeLine = freezeKnockbackPreviewWhileLaunched_ && launched;

        if (!shouldFreezeLine) {
        const size_t previewAttackIndex = std::min<size_t>(
            static_cast<size_t>(std::max(previewAttackKind_, 0)),
            enemyMgr_.BossAttackCount() > 0 ? enemyMgr_.BossAttackCount() - 1 : 0);
        if (previewUsesPlayerPercent_ && !launched) {
            knockbackPreviewDamagePercent_ = player_->GetDamagePercent();
        }
        const float percent = previewUsesPlayerPercent_
            ? knockbackPreviewDamagePercent_
            : previewPercent_;
        const TestSceneKnockbackPreview::Metrics metrics = TestSceneKnockbackPreview::Calculate(
            *player_,
            enemyMgr_,
            previewAttackIndex,
            percent,
            outOfBoundsEnabled_,
            outLeftX_,
            outRightX_,
            outBottomY_,
            outTopY_);

        const AABB body = player_->GetBodyAABB();
        const Vector3 previewStart{
            (body.min.x + body.max.x) * 0.5f,
            (body.min.y + body.max.y) * 0.5f,
            (body.min.z + body.max.z) * 0.5f,
        };
        knockbackPreviewLinePoints_.clear();
        if (previewLineMode_ == 0) {
            const EnemyManager::BossHitTuning& tuning = enemyMgr_.BossAttackAt(previewAttackIndex).hit;
            knockbackPreviewLinePoints_ = SimulateKnockbackTrajectory(
                *player_,
                player_->GetPos3D(),
                metrics.velocity,
                tuning.hitStunSec,
                outOfBoundsEnabled_,
                outLeftX_,
                outRightX_,
                outBottomY_,
                outTopY_,
                previewLineScale_);
        } else {
            knockbackPreviewLinePoints_.push_back(previewStart);
            knockbackPreviewLinePoints_.push_back({
                previewStart.x + metrics.velocity.x * previewLineScale_,
                previewStart.y + metrics.velocity.y * previewLineScale_,
                previewStart.z + metrics.velocity.z * previewLineScale_,
            });
        }

        if (knockbackPreviewLinePoints_.size() >= 2) {
            knockbackPreviewLineStart_ = knockbackPreviewLinePoints_.front();
            knockbackPreviewLineEnd_ = knockbackPreviewLinePoints_.back();
            knockbackPreviewLineVisible_ = true;
        } else {
            knockbackPreviewLineVisible_ = false;
        }
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


// ポストエフェクト対象の3D描画（オフスクリーンへ）
void TestScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

   // if (skyDome_) skyDome_->Draw();
    if (ground_) ground_->Draw();

    skyDome_->Draw();

    // LevelLoader で読み込んだオブジェクトの描画
    for (auto& obj : levelObjects_) {
        obj->Draw();
    }

    if (drawPointMarker_ && pointMarker_) pointMarker_->Draw();
    if (drawSpotMarker_ && spotMarker_) spotMarker_->Draw();

    if (player_) player_->Draw();
    if (drawBossHitboxPreview_ && bossHitboxPreview_) bossHitboxPreview_->Draw();
    if (drawOutOfBoundsPreview_) {
        if (outLeftPreview_) outLeftPreview_->Draw();
        if (outRightPreview_) outRightPreview_->Draw();
        if (outBottomPreview_) outBottomPreview_->Draw();
        if (outTopPreview_) outTopPreview_->Draw();
    }
    if (drawGroundCollisionPreview_ && groundCollisionPreviews_.size() == groundMeshes_.size()) {
        for (size_t i = 0; i < groundCollisionPreviews_.size(); ++i) {
            if (groundMeshes_[i].enabled && groundCollisionPreviews_[i]) {
                groundCollisionPreviews_[i]->Draw();
            }
        }
    }

    enemyMgr_.Draw();

#ifdef _DEBUG
    if (player_) player_->DrawDebugHitBoxes(enemyMgr_);
#endif

    // 3Dエフェクトオブジェクトの描画
    EffectManager::GetInstance()->Draw();

    // GPU Particle
    app.ParticleCom()->SetGraphicsPipelineState();
    ParticleManager::GetInstance()->Draw(cmd);
}

// バックバッファへ直接描く3D（ポストエフェクト不要なもの）
void TestScene::Draw3D(GameApp& app) {
    // 今は特になし
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

// その他（空でOK）
void TestScene::Draw(GameApp& app) {
}


void TestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = true;
    gTestSceneAttackTuningTarget = std::clamp(gTestSceneAttackTuningTarget, 0, 1);

    if (drawKnockbackPreview_ && knockbackPreviewLineVisible_ && camera_ && gHasSceneImageRect && knockbackPreviewLinePoints_.size() >= 2) {
        const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
        const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
        const float thickness = std::max(1.0f, previewLineThickness_ * kKnockbackPreviewLineThicknessToPixels);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
        for (size_t i = 1; i < knockbackPreviewLinePoints_.size(); ++i) {
            Vector2 start{};
            Vector2 end{};
            if (!ProjectWorldToRect(
                *camera_,
                knockbackPreviewLinePoints_[i - 1],
                gSceneImageMin.x,
                gSceneImageMin.y,
                sceneW,
                sceneH,
                start) ||
                !ProjectWorldToRect(
                    *camera_,
                    knockbackPreviewLinePoints_[i],
                    gSceneImageMin.x,
                    gSceneImageMin.y,
                    sceneW,
                    sceneH,
                    end)) {
                continue;
            }
            drawList->AddLine(
                ImVec2(start.x, start.y),
                ImVec2(end.x, end.y),
                IM_COL32(255, 38, 13, 255),
                thickness);
        }
        drawList->PopClipRect();
    }

    if (player_ && camera_ && gHasSceneImageRect) {
        Vector3 center{};
        Vector3 halfSize{};
        bool activeHitBox = false;
        if (player_->GetAttackDebugVisualBox(center, halfSize, activeHitBox)) {
            const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
            const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
            const Vector3 corners[] = {
                { center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
                { center.x + halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
                { center.x - halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
                { center.x + halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
                { center.x - halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
                { center.x + halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
                { center.x - halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
                { center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
            };
            ImVec2 minPos{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            ImVec2 maxPos{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };
            bool hasProjectedCorner = false;
            for (const Vector3& corner : corners) {
                Vector2 screen{};
                if (!ProjectWorldToRect(*camera_, corner, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, screen)) {
                    continue;
                }
                minPos.x = std::min(minPos.x, screen.x);
                minPos.y = std::min(minPos.y, screen.y);
                maxPos.x = std::max(maxPos.x, screen.x);
                maxPos.y = std::max(maxPos.y, screen.y);
                hasProjectedCorner = true;
            }
            if (hasProjectedCorner) {
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
                const ImU32 color = activeHitBox
                    ? IM_COL32(30, 255, 70, 255)
                    : IM_COL32(255, 220, 30, 255);
                drawList->AddRect(minPos, maxPos, color, 0.0f, 0, 4.0f);
                drawList->AddText(
                    ImVec2(minPos.x, std::max(gSceneImageMin.y, minPos.y - 18.0f)),
                    color,
                    activeHitBox ? "Player Hitbox ACTIVE" : "Player Hitbox Preview");
                drawList->PopClipRect();
            }
        }
    }

    if (player_ && gHasSceneImageRect) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);

        const ImVec2 panelPos{ gSceneImageMin.x + 14.0f, gSceneImageMin.y + 14.0f };
        const ImVec2 panelSize{ 285.0f, 158.0f };
        const bool canCancel = player_->CanSpecialCancelNow();
        const bool cancelFlash = player_->GetSpecialCancelDebugFlashSec() > 0.0f;
        const ImU32 panelColor = cancelFlash
            ? IM_COL32(20, 110, 55, 230)
            : IM_COL32(10, 10, 10, 185);
        const ImU32 accentColor = canCancel
            ? IM_COL32(40, 255, 90, 255)
            : IM_COL32(255, 210, 50, 255);

        drawList->AddRectFilled(panelPos, ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), panelColor, 6.0f);
        drawList->AddRect(panelPos, ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), accentColor, 6.0f, 0, 2.0f);
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 8.0f), IM_COL32(255, 255, 255, 255), "Special Cancel Debug");

        const int gauge = player_->GetCancelGauge();
        const int maxGauge = player_->GetMaxCancelGauge();
        const ImVec2 gaugeStart{ panelPos.x + 10.0f, panelPos.y + 34.0f };
        for (int i = 0; i < maxGauge; ++i) {
            const float x = gaugeStart.x + i * 34.0f;
            const ImU32 fill = i < gauge ? IM_COL32(80, 180, 255, 255) : IM_COL32(60, 60, 60, 255);
            drawList->AddRectFilled(ImVec2(x, gaugeStart.y), ImVec2(x + 26.0f, gaugeStart.y + 18.0f), fill, 3.0f);
            drawList->AddRect(ImVec2(x, gaugeStart.y), ImVec2(x + 26.0f, gaugeStart.y + 18.0f), IM_COL32(255, 255, 255, 220), 3.0f);
        }

        char line[128]{};
        std::snprintf(line, sizeof(line), "Right: %s Chain: %s",
            player_->HasSpecialCancelRight() ? "YES" : "NO",
            player_->HasSpecialChainCancelRight() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 60.0f), accentColor, line);

        std::snprintf(line, sizeof(line), "Ready: %s  Used: %s",
            canCancel ? "YES" : "NO",
            player_->DidUseSpecialCancelThisAction() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 80.0f), IM_COL32(230, 230, 230, 255), line);

        std::snprintf(line, sizeof(line), "SpecialHit: %s",
            player_->HasSpecialHitDuringAction() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 145.0f, panelPos.y + 80.0f), IM_COL32(230, 230, 230, 255), line);

        if (cancelFlash) {
            drawList->AddText(ImVec2(panelPos.x + 165.0f, panelPos.y + 32.0f), IM_COL32(80, 255, 120, 255), "CANCEL!");
        }

        const bool uComboFlash = player_->GetUComboDebugFlashSec() > 0.0f;
        std::snprintf(line, sizeof(line), "U Combo: %d / 3  Accept: %s",
            player_->GetUComboStageDisplay(),
            player_->IsUComboAccepting() ? "YES" : "NO");
        drawList->AddText(
            ImVec2(panelPos.x + 10.0f, panelPos.y + 104.0f),
            player_->IsUComboAccepting() ? IM_COL32(80, 255, 120, 255) : IM_COL32(230, 230, 230, 255),
            line);

        std::snprintf(line, sizeof(line), "Buffer: %s  Reset: %.2f",
            player_->GetUComboBufferTimer() > 0.0f ? "ON" : "OFF",
            player_->GetUComboResetTimer());
        drawList->AddText(
            ImVec2(panelPos.x + 10.0f, panelPos.y + 124.0f),
            player_->GetUComboBufferTimer() > 0.0f ? IM_COL32(255, 220, 30, 255) : IM_COL32(190, 190, 190, 255),
            line);
        if (uComboFlash) {
            drawList->AddText(ImVec2(panelPos.x + 188.0f, panelPos.y + 104.0f), IM_COL32(255, 240, 70, 255), "NEXT U!");
        }

        drawList->PopClipRect();
    }

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
        ImGui::Text("Player HP: %d / %d", player_->GetHP(), player_->GetMaxHP());
        ImGui::Text("Player Damage: %.1f%%", player_->GetDamagePercent());
        ImGui::Text("Launched: %s", player_->IsLaunched() ? "true" : "false");

        int hp = player_->GetHP();
        if (ImGui::DragInt("Player HP", &hp, 1, 0, player_->GetMaxHP())) {
            player_->SetHP(hp);
        }
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

        if (enemyMgr_.Battle().useHpDamage) {
            player_->Damage(tuning.hpDamage);
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 1.0e-6f) {
                dir.x /= len;
                dir.y /= len;
                dir.z /= len;
            } else {
                dir = { 1.0f, 0.35f, 0.0f };
            }
            const float power = tuning.baseKnockback;
            player_->ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec);
            player_->TriggerHitFlash(0.25f);
        } else {
            if (tuning.useFixedKnockback) {
                player_->AddDamagePercent(tuning.damagePercent);
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 1.0e-6f) {
                    dir.x /= len;
                    dir.y /= len;
                    dir.z /= len;
                } else {
                    dir = { 1.0f, 0.35f, 0.0f };
                }
                const float power = tuning.baseKnockback;
                player_->ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec);
                player_->TriggerHitFlash(0.25f);
            } else {
                player_->ApplyBossHit(
                    tuning.damagePercent,
                    tuning.baseKnockback,
                    tuning.knockbackScale,
                    dir,
                    tuning.hitStunSec);
            }
        }
        const EnemyManager::HitStopTuning& hitStop = enemyMgr_.HitStop();
        if (hitStop.enabled) {
            hitStopTimer_ = std::max(hitStopTimer_, hitStop.bossAttackSec);
        }

        if (attackIndex == 3) { // DoubleMelee1
            boss.GetBossAIMutable().ForceChangeState(BossAI::State::Double_Melee_Rock);
        }
    };

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        Vector3 bossPos = boss->GetPos3D();
        if (ImGui::DragFloat3("Boss Pos", &bossPos.x, 0.1f)) {
            boss->SetPos(bossPos);
        }

        const char* stateStr = "Unknown";
        switch (boss->GetBossAI().GetState()) {
        case BossAI::State::Wander: stateStr = "Wander"; break;
        case BossAI::State::Drop_Windup: stateStr = "Drop_Windup"; break;
        case BossAI::State::Drop_Fall: stateStr = "Drop_Fall"; break;
        case BossAI::State::Drop_Land: stateStr = "Drop_Land"; break;
        case BossAI::State::Melee_Dash: stateStr = "Melee_Dash"; break;
        case BossAI::State::Melee_Attack: stateStr = "Melee_Attack"; break;
        case BossAI::State::Melee_Recover: stateStr = "Melee_Recover"; break;
        case BossAI::State::Double_Melee_Dash: stateStr = "Double_Melee_Dash"; break;
        case BossAI::State::Double_Melee_Attack_1: stateStr = "Double_Melee_Attack_1"; break;
        case BossAI::State::Double_Melee_Rock: stateStr = "Double_Melee_Rock"; break;
        case BossAI::State::Double_Melee_Attack_2: stateStr = "Double_Melee_Attack_2"; break;
        case BossAI::State::Rush_ToRight: stateStr = "Rush_ToRight"; break;
        case BossAI::State::Rush_Charge: stateStr = "Rush_Charge"; break;
        case BossAI::State::Rush_ExitLeft: stateStr = "Rush_ExitLeft"; break;
        case BossAI::State::Rush_Return: stateStr = "Rush_Return"; break;
        case BossAI::State::Super50: stateStr = "Super50"; break;
        case BossAI::State::Super25: stateStr = "Super25"; break;
        }
        ImGui::Text("Boss State: %s", stateStr);

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
        ImGui::SameLine();
        if (ImGui::Button("Boss Double Melee")) {
            // AIをダッシュから開始するだけ（ヒット判定はAIに任せる）
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Double_Melee_Dash);
        }
        // 個別ヒットテスト用ボタン（別行）
        if (ImGui::Button("Hit1 (Launch)")) {
            // 第1打：プレイヤーを打ち上げ、ボスをDouble_Melee_Rockへ移行
            triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::DoubleMelee1));
        }
        ImGui::SameLine();
        if (ImGui::Button("Hit2 (Slam)")) {
            // 第2打：プレイヤーを斜め下に叩き落とす
            triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::DoubleMelee2));
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

    if (ImGui::CollapsingHeader("Battle Rules", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyManager::BattleTuning& battle = enemyMgr_.Battle();
        ImGui::Checkbox("Use HP Damage For Boss Attacks", &battle.useHpDamage);
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
        ImGui::Checkbox("Draw Boundary Preview", &drawOutOfBoundsPreview_);
        ImGui::DragFloat("Left X", &outLeftX_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat("Right X", &outRightX_, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Bottom Y", &outBottomY_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat("Top Y", &outTopY_, 0.5f, -20.0f, 80.0f);
        ImGui::DragFloat("Preview Z Near", &outPreviewZNear_, 0.5f, -100.0f, 100.0f);
        ImGui::DragFloat("Preview Z Far", &outPreviewZFar_, 0.5f, -100.0f, 100.0f);
        ImGui::DragFloat("Preview Thickness", &outPreviewThickness_, 0.01f, 0.01f, 2.0f);
        ImGui::DragFloat3("Drop Respawn Pos", &dropRespawnPos_.x, 0.1f);
        ImGui::Checkbox("Reset Damage On Out", &resetDamageOnOutOfBounds_);
    }

    if (ImGui::CollapsingHeader("Stage Obj Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Ground AABB Enabled", &groundCollisionEnabled_);
        ImGui::Checkbox("Draw Ground AABB", &drawGroundCollisionPreview_);
        ImGui::Checkbox("Auto Fit From Ground OBJ", &autoFitGroundCollisionToObj_);
        ImGui::DragFloat("OBJ Fit Padding", &groundCollisionPadding_, 0.01f, 0.0f, 10.0f);
        if (ImGui::Button("Fit Ground AABB From OBJ") && ground_) {
            FitMeshAABBsToObject(*ground_, groundMeshes_, groundCollisionPadding_);
            groundCollisionPreviews_.clear();
            for (const auto& mesh : groundMeshes_) {
                groundCollisionPreviews_.push_back(CreateBoundaryPreview(app, { 0.1f, 1.0f, 0.35f, 0.35f }));
            }
        }

        if (!groundMeshes_.empty()) {
            ImGui::SeparatorText("Mesh Collision List");
            if (ImGui::Button("Enable All")) {
                for (auto& m : groundMeshes_) m.enabled = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable All")) {
                for (auto& m : groundMeshes_) m.enabled = false;
            }

            for (size_t i = 0; i < groundMeshes_.size(); ++i) {
                auto& m = groundMeshes_[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Checkbox(m.name.c_str(), &m.enabled);
                if (m.enabled) {
                    Vector3 center = {
                        (m.worldAABB.min.x + m.worldAABB.max.x) * 0.5f,
                        (m.worldAABB.min.y + m.worldAABB.max.y) * 0.5f,
                        (m.worldAABB.min.z + m.worldAABB.max.z) * 0.5f,
                    };
                    Vector3 half = {
                        (m.worldAABB.max.x - m.worldAABB.min.x) * 0.5f,
                        (m.worldAABB.max.y - m.worldAABB.min.y) * 0.5f,
                        (m.worldAABB.max.z - m.worldAABB.min.z) * 0.5f,
                    };
                    ImGui::Text("  Center: %.2f, %.2f, %.2f", center.x, center.y, center.z);
                    ImGui::Text("  Half:   %.2f, %.2f, %.2f", half.x, half.y, half.z);
                }
                ImGui::PopID();
            }
        }
    }

    if (ImGui::CollapsingHeader("Battle Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Dynamic Distance", &dynamicBattleCamera_);
        ImGui::DragFloat("Min Distance", &battleCameraMinDistance_, 0.5f, 5.0f, 120.0f);
        ImGui::DragFloat("Max Distance", &battleCameraMaxDistance_, 0.5f, 5.0f, 160.0f);
        ImGui::DragFloat("Distance Scale", &battleCameraDistanceScale_, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Height", &battleCameraHeight_, 0.5f, 2.0f, 80.0f);
        ImGui::DragFloat("Follow Lerp", &battleCameraFollowLerp_, 0.1f, 0.1f, 30.0f);
    }

    if (player_ && ImGui::CollapsingHeader("Player Launch Ease-Out Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
        float dragHigh = player_->GetLaunchXZDragHigh();
        float dragLow = player_->GetLaunchXZDragLow();
        float threshold = player_->GetLaunchDragThreshold();
        bool useTime = player_->GetLaunchDragUseTime();

        if (ImGui::DragFloat("Drag High (Fast phase)", &dragHigh, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetLaunchXZDragHigh(dragHigh);
        }
        ImGui::Text(" (1.0 = no deceleration. Closer to 1.0 makes fast phase longer)");
        
        if (ImGui::DragFloat("Drag Low (Slow phase)", &dragLow, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetLaunchXZDragLow(dragLow);
        }
        ImGui::Text(" (Deceleration rate after threshold. Smaller values decelerate faster)");

        if (ImGui::DragFloat("Transition Threshold", &threshold, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetLaunchDragThreshold(threshold);
        }
        ImGui::Text(" (1.0 -> 0.0. The point where physics switches from High to Low drag)");

        if (ImGui::Checkbox("Use Remaining Time For Threshold", &useTime)) {
            player_->SetLaunchDragUseTime(useTime);
        }
        ImGui::Text(" (If unchecked, uses remaining velocity ratio instead)");

        ImGui::SeparatorText("Bounce / Reflection Settings");
        float bRest = player_->GetLaunchBounceRestitution();
        float bFric = player_->GetLaunchBounceFriction();
        float bMinSpeed = player_->GetLaunchBounceMinSpeed();
        float keepSpeed = player_->GetLaunchKeepSpeedThreshold();
        float ffBounce = player_->GetFreeFallGroundBounceSpeed();
        float ffDamping = player_->GetFreeFallGroundBounceDamping();

        if (ImGui::DragFloat("Bounce Restitution", &bRest, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetLaunchBounceRestitution(bRest);
        }
        ImGui::Text(" (Bounciness of walls & floor. Default: 0.65)");

        if (ImGui::DragFloat("Bounce Friction", &bFric, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetLaunchBounceFriction(bFric);
        }
        ImGui::Text(" (Deceleration multiplier for other axes during bounce. Default: 0.90)");

        if (ImGui::DragFloat("Bounce Min Speed", &bMinSpeed, 0.1f, 0.0f, 40.0f, "%.1f")) {
            player_->SetLaunchBounceMinSpeed(bMinSpeed);
        }
        ImGui::Text(" (Minimum speed required to bounce. Below this, player slides or stops. Default: 4.0)");

        if (ImGui::DragFloat("Launch Keep Speed", &keepSpeed, 0.1f, 0.0f, 80.0f, "%.1f")) {
            player_->SetLaunchKeepSpeedThreshold(keepSpeed);
        }
        ImGui::Text(" (After a bounce, below this total speed switches to FreeFall. Default: 8.0)");

        if (ImGui::DragFloat("FreeFall Ground Bounce", &ffBounce, 0.1f, 0.0f, 20.0f, "%.1f")) {
            player_->SetFreeFallGroundBounceSpeed(ffBounce);
        }
        ImGui::Text(" (Small one-shot landing bounce while in FreeFall. Default: 3.5)");

        if (ImGui::DragFloat("FreeFall Bounce Damping", &ffDamping, 0.005f, 0.0f, 1.0f, "%.3f")) {
            player_->SetFreeFallGroundBounceDamping(ffDamping);
        }
        ImGui::Text(" (How much falling speed feeds the small FreeFall bounce. Default: 0.35)");
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Next Work", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("Add recovery special after launch");
        ImGui::BulletText("Promote test respawn into proper stock/KO flow");
        ImGui::BulletText("Add KO effect before drop respawn or result transition");
        ImGui::BulletText("Move tuned boss hit values into battle-side defaults");
    }

    ImGui::End();

    ImGui::Begin("Attack Tuning");

    if (gTestSceneAttackTuningTarget == 0) {
    if (ImGui::CollapsingHeader("Knockback Preview Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
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
                outBottomY_,
                outTopY_);

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
    }

    if (ImGui::CollapsingHeader("Boss Attacks Knockback Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        const float fixedBakePercent = (previewUsesPlayerPercent_ && player_)
            ? player_->GetDamagePercent()
            : previewPercent_;

        auto drawBossTuning = [fixedBakePercent](const char* label, EnemyManager::BossHitTuning& tuning) {
            ImGui::PushID(label);
            if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PopID();
                return;
            }
            ImGui::DragFloat("Damage Percent", &tuning.damagePercent, 0.5f, 0.0f, 100.0f);
            ImGui::DragInt("HP Damage", &tuning.hpDamage, 1, 0, 999);
            ImGui::DragFloat("Base Knockback", &tuning.baseKnockback, 0.1f, 0.0f, 80.0f);
            ImGui::DragFloat("Knockback Scale", &tuning.knockbackScale, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat3("Knockback Dir", &tuning.knockbackDir.x, 0.01f, -2.0f, 2.0f);
            ImGui::DragFloat("Hit Stun Sec", &tuning.hitStunSec, 0.01f, 0.0f, 3.0f);
            ImGui::SeparatorText("Fixed Knockback");
            ImGui::Checkbox("Use Fixed Knockback", &tuning.useFixedKnockback);
            const float currentScaledPower = tuning.baseKnockback + fixedBakePercent * tuning.knockbackScale;
            if (ImGui::Button("Set Fixed From Current %")) {
                tuning.baseKnockback = currentScaledPower;
                tuning.knockbackScale = 0.0f;
                tuning.useFixedKnockback = true;
            }
            ImGui::Text("Current %.1f%% power: %.2f", fixedBakePercent, currentScaledPower);
            ImGui::PopID();
        };

        if (previewAttackKind_ >= 0 && previewAttackKind_ < static_cast<int>(enemyMgr_.BossAttackCount())) {
            auto& attack = enemyMgr_.BossAttackAt(static_cast<size_t>(previewAttackKind_));
            drawBossTuning(attack.name.c_str(), attack.hit);
        }
    }

    if (ImGui::CollapsingHeader("Boss Attacks Hitbox Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        if (previewAttackKind_ >= 0 && previewAttackKind_ < static_cast<int>(enemyMgr_.BossAttackCount())) {
            auto& attack = enemyMgr_.BossAttackAt(static_cast<size_t>(previewAttackKind_));
            drawBossHitboxTuning(attack.name.c_str(), attack.hitbox);
        }
    }

    } else {
    if (!player_) {
        ImGui::TextUnformatted("Player is not available.");
    } else {
        ImGui::Text("Cancel Gauge: %d / %d", player_->GetCancelGauge(), player_->GetMaxCancelGauge());
        ImGui::Text("Special Cancel Right: %s", player_->HasSpecialCancelRight() ? "true" : "false");
        ImGui::Separator();
    if (ImGui::CollapsingHeader("Player U Attacks", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    }
    }

    ImGui::End();
#endif

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

    // ★dirは正規化しないと壊れやすいので、ボタンで正規化も入れる
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
}

std::unique_ptr<Object3d> TestScene::CreateBoundaryPreview(GameApp& app, const Vector4& color) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(app.ObjCom(), app.Dx());
    object->SetCamera(camera_.get());
    object->SetModel("cube/cube.obj");
    object->SetEnableLighting(0);
    object->SetMaterialColor(color);
    object->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
    return object;
}
