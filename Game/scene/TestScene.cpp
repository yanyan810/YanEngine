#include "TestScene.h"
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
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

constexpr float kRadToDeg = 57.29577951308232f;
using json = nlohmann::json;

MeleeKind PreviewKindFromIndex(int index) {
    if (index == 0) {
        return MeleeKind::Normal;
    }
    if (index == 1) {
        return MeleeKind::Land;
    }
    return MeleeKind::Rush;
}

float Length3(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
    const float len = Length3(v);
    if (len <= 1.0e-6f) {
        return fallback;
    }
    return { v.x / len, v.y / len, v.z / len };
}

json ToJson(const Vector3& v) {
    return json::array({ v.x, v.y, v.z });
}

Vector3 Vector3FromJson(const json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
    };
}

json ToJson(const EnemyManager::BossHitTuning& tuning) {
    return {
        { "damagePercent", tuning.damagePercent },
        { "baseKnockback", tuning.baseKnockback },
        { "knockbackScale", tuning.knockbackScale },
        { "knockbackDir", ToJson(tuning.knockbackDir) },
        { "hitStunSec", tuning.hitStunSec },
    };
}

void ApplyJsonToTuning(const json& value, EnemyManager::BossHitTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    tuning.damagePercent = value.value("damagePercent", tuning.damagePercent);
    tuning.baseKnockback = value.value("baseKnockback", tuning.baseKnockback);
    tuning.knockbackScale = value.value("knockbackScale", tuning.knockbackScale);
    if (value.contains("knockbackDir")) {
        tuning.knockbackDir = Vector3FromJson(value.at("knockbackDir"), tuning.knockbackDir);
    }
    tuning.hitStunSec = value.value("hitStunSec", tuning.hitStunSec);
}

void SetLineSegment(Object3d& line, const Vector3& start, const Vector3& end, float thickness, float dt) {
    const Vector3 v{
        end.x - start.x,
        end.y - start.y,
        end.z - start.z,
    };
    const float length = Length3(v);
    const Vector3 mid{
        start.x + v.x * 0.5f,
        start.y + v.y * 0.5f,
        start.z + v.z * 0.5f,
    };

    line.SetTranslate(mid);
    line.SetRotate({ 0.0f, 0.0f, std::atan2(v.y, v.x) });
    // cube.obj spans -1..1 on each axis, so half-scale keeps the segment endpoints exact.
    line.SetScale({ std::max(length * 0.5f, 0.001f), thickness, thickness });
    line.Update(dt);
}

float SolveTimeToY(float startY, float velocityY, float gravity, float targetY) {
    const float g = std::max(gravity, 0.0001f);
    const float height = startY - targetY;
    const float disc = velocityY * velocityY + 2.0f * g * height;
    if (disc < 0.0f) {
        return 0.0f;
    }
    return std::max(0.0f, (velocityY + std::sqrt(disc)) / g);
}

float SolvePositiveBoundaryTime(float start, float velocity, float boundary) {
    if (std::abs(velocity) <= 1.0e-6f) {
        return -1.0f;
    }
    const float t = (boundary - start) / velocity;
    return t > 0.0f ? t : -1.0f;
}

struct KnockbackPreviewMetrics {
    Vector3 direction{};
    Vector3 velocity{};
    Vector3 landingPos{};
    Vector3 outPos{};
    float power = 0.0f;
    float launchAngleDeg = 0.0f;
    float signedScreenAngleDeg = 0.0f;
    float airTimeSec = 0.0f;
    float travelX = 0.0f;
    float travelZ = 0.0f;
    float groundDistance = 0.0f;
    float straightDistance = 0.0f;
    float maxHeightY = 0.0f;
    float outTimeSec = 0.0f;
    float outDistance = 0.0f;
    bool reachesOutBeforeLanding = false;
};

KnockbackPreviewMetrics CalcKnockbackPreviewMetrics(
    const Player& player,
    const EnemyManager& enemyMgr,
    MeleeKind kind,
    float percent,
    bool outOfBoundsEnabled,
    float outLeftX,
    float outRightX,
    float outBottomY) {

    KnockbackPreviewMetrics m{};
    const EnemyManager::BossHitTuning& tuning = enemyMgr.BossTuning(kind);
    m.power = tuning.baseKnockback + percent * tuning.knockbackScale;

    Vector3 dir = tuning.knockbackDir;
    if (const Enemy* boss = enemyMgr.GetBoss()) {
        const float dirX = (player.GetX() >= boss->GetPos3D().x) ? 1.0f : -1.0f;
        dir.x = std::abs(dir.x) * dirX;
    }
    m.direction = NormalizeOr(dir, { 1.0f, 0.0f, 0.0f });
    m.velocity = {
        m.direction.x * m.power,
        m.direction.y * m.power,
        m.direction.z * m.power,
    };

    const Vector3 start = player.GetPos3D();
    const float gravity = player.GetGravity();
    m.airTimeSec = SolveTimeToY(start.y, m.velocity.y, gravity, 0.0f);
    m.travelX = m.velocity.x * m.airTimeSec;
    m.travelZ = m.velocity.z * m.airTimeSec;
    m.groundDistance = std::sqrt(m.travelX * m.travelX + m.travelZ * m.travelZ);
    m.straightDistance = std::sqrt(
        m.travelX * m.travelX +
        start.y * start.y +
        m.travelZ * m.travelZ);
    m.landingPos = {
        start.x + m.travelX,
        0.0f,
        start.z + m.travelZ,
    };

    const float horizontalSpeed = std::sqrt(m.velocity.x * m.velocity.x + m.velocity.z * m.velocity.z);
    m.launchAngleDeg = std::atan2(m.velocity.y, horizontalSpeed) * kRadToDeg;
    m.signedScreenAngleDeg = std::atan2(m.velocity.y, m.velocity.x) * kRadToDeg;
    if (m.velocity.y > 0.0f) {
        const float apexAdd = (m.velocity.y * m.velocity.y) / (2.0f * std::max(gravity, 0.0001f));
        m.maxHeightY = start.y + apexAdd;
    } else {
        m.maxHeightY = start.y;
    }

    if (outOfBoundsEnabled) {
        float firstOutTime = -1.0f;

        const float sideTime = SolvePositiveBoundaryTime(
            start.x,
            m.velocity.x,
            m.velocity.x >= 0.0f ? outRightX : outLeftX);
        if (sideTime >= 0.0f) {
            firstOutTime = sideTime;
        }

        const float bottomTime = SolveTimeToY(start.y, m.velocity.y, gravity, outBottomY);
        if (bottomTime > 0.0f && (firstOutTime < 0.0f || bottomTime < firstOutTime)) {
            firstOutTime = bottomTime;
        }

        if (firstOutTime >= 0.0f && firstOutTime < m.airTimeSec) {
            m.reachesOutBeforeLanding = true;
            m.outTimeSec = firstOutTime;
            m.outPos = {
                start.x + m.velocity.x * firstOutTime,
                start.y + m.velocity.y * firstOutTime - 0.5f * gravity * firstOutTime * firstOutTime,
                start.z + m.velocity.z * firstOutTime,
            };
            const float outDx = m.outPos.x - start.x;
            const float outDz = m.outPos.z - start.z;
            m.outDistance = std::sqrt(outDx * outDx + outDz * outDz);
        }
    }

    return m;
}

} // namespace

bool TestScene::SaveBossTuning_(const std::string& path) {
    try {
        json root;
        root["version"] = 1;
        root["bossHits"] = {
            { "normal", ToJson(enemyMgr_.BossTuning(MeleeKind::Normal)) },
            { "jumpSlash", ToJson(enemyMgr_.BossTuning(MeleeKind::Land)) },
            { "rush", ToJson(enemyMgr_.BossTuning(MeleeKind::Rush)) },
        };

        const std::filesystem::path filePath(path);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        if (!file) {
            bossTuningStatus_ = "Save failed: cannot open file";
            return false;
        }
        file << root.dump(4);
        bossTuningStatus_ = "Saved: " + path;
        return true;
    } catch (const std::exception& e) {
        bossTuningStatus_ = std::string("Save failed: ") + e.what();
        return false;
    }
}

bool TestScene::LoadBossTuning_(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            bossTuningStatus_ = "Load failed: cannot open file";
            return false;
        }

        json root;
        file >> root;
        const json& bossHits = root.contains("bossHits") ? root.at("bossHits") : root;

        if (bossHits.contains("normal")) {
            ApplyJsonToTuning(bossHits.at("normal"), enemyMgr_.BossTuning(MeleeKind::Normal));
        }
        if (bossHits.contains("jumpSlash")) {
            ApplyJsonToTuning(bossHits.at("jumpSlash"), enemyMgr_.BossTuning(MeleeKind::Land));
        }
        if (bossHits.contains("rush")) {
            ApplyJsonToTuning(bossHits.at("rush"), enemyMgr_.BossTuning(MeleeKind::Rush));
        }

        bossTuningStatus_ = "Loaded: " + path;
        previewLineWasLaunched_ = false;
        return true;
    } catch (const std::exception& e) {
        bossTuningStatus_ = std::string("Load failed: ") + e.what();
        return false;
    }
}

void TestScene::OnEnter(GameApp& app) {
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

    enemyMgr_.Spawn(EnemyType::Boss, bossSpawnPos_);
    
    // ★凍結（GetEnemies() はデバッグ確認にも使うので存在してる前提）
    auto& enemies = enemyMgr_.GetEnemies();
    if (!enemies.empty()) {
        enemies.back().SetInvincible(true); // 死なない
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


    // どこかで（OnEnterの中）
    //auto* mgr = ModelManager::GetInstance();
    //mgr->LoadModel("ground/ground.obj");   // resources/ground/ground.obj を想定

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
    // Groundは pointだけ使う
    groundLight_ = light_;              // とりあえず既存をコピーしてもOK
    groundLight_.dirIntensity = 0.0f;   // ★Directional 無効
    groundLight_.spotIntensity = 0.0f;  // ★Spot 無効

    groundLight_.pointIntensity = 16.0f;
    groundLight_.pointPos = { 0.0f, -42.0f, -1.0f };
    groundLight_.pointRadius = 200.0f;
    groundLight_.pointDecay = 1.0f;
    groundLight_.pointColor = { 1.0f, 1.0f, 1.0f }; // Vector3想定

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

    // 角度（degree管理してる前提）
    groundLight_.spotAngleDeg = 25.0f;
    groundLight_.spotFalloffStartDeg = 15.0f;
    groundLight_.spotColor = { 1.0f, 1.0f, 1.0f };

    // --- Spot マーカー ---
    spotMarker_ = std::make_unique<Object3d>();
    spotMarker_->Initialize(app.ObjCom(), app.Dx());
    spotMarker_->SetCamera(camera_.get());
    spotMarker_->SetModel("cube/cube.obj");
    spotMarker_->SetEnableLighting(0);
    spotMarker_->SetMaterialColor({ 0, 1, 1, 1 }); // シアン
    spotMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    // PointLightマーカー
    pointMarker_ = std::make_unique<Object3d>();
    pointMarker_->Initialize(app.ObjCom(), app.Dx());
    pointMarker_->SetCamera(camera_.get());
    pointMarker_->SetModel("cube/cube.obj");

    // 見た目
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
    skyDome_->SetShininess(1.0f);                // 影響しないけど保険
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    knockbackPreviewLine_ = std::make_unique<Object3d>();
    knockbackPreviewLine_->Initialize(app.ObjCom(), app.Dx());
    knockbackPreviewLine_->SetCamera(camera_.get());
    knockbackPreviewLine_->SetModel("cube/cube.obj");
    knockbackPreviewLine_->SetEnableLighting(0);
    knockbackPreviewLine_->SetMaterialColor({ 1.0f, 0.15f, 0.05f, 1.0f });
    knockbackPreviewLine_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

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
    }

    camera_->Update();

    ground_->Update(dt);
    skyDome_->Update(dt);

    // LevelLoader で読み込んだオブジェクトの更新
    for (auto& obj : levelObjects_) {
        obj->Update(dt);
    }

    if (player_) {
        player_->Update(dt, *input_, enemyMgr_);
    }

    Vector2 playerPos2D = player_->GetPos2D();
    float playerZ = player_->GetZ();

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        bool isAttacking = boss->GetBossAI().GetState() != BossAI::State::Wander;
        boss->SetAIDisabled(!bossAIEnabled_ && !isAttacking);
    }

    enemyMgr_.Update(dt, playerPos2D, playerZ, *player_);

    // ===============================
    // ★ クランプ到達チェック
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
        const MeleeKind previewKind = PreviewKindFromIndex(previewAttackKind_);
        const float percent = previewUsesPlayerPercent_ ? player_->GetDamagePercent() : previewPercent_;
        const KnockbackPreviewMetrics metrics = CalcKnockbackPreviewMetrics(
            *player_,
            enemyMgr_,
            previewKind,
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

        SetLineSegment(*knockbackPreviewLine_, previewStart, previewEnd, previewLineThickness_, dt);
        }

        previewLineWasLaunched_ = launched;
    }

    player_->SetLighting(light_);
    enemyMgr_.SetLighting(light_);

    for (const auto& event : enemyMgr_.ConsumeBossAttackEffectEvents()) {
        if (event.kind == MeleeKind::Land) {
            EffectManager::GetInstance()->Play("fallAttak", event.position);
        }
    }

    EffectManager::GetInstance()->Update(dt);
    ParticleManager::GetInstance()->Update(dt, *camera_);
   
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
    if (drawKnockbackPreview_ && knockbackPreviewLine_) knockbackPreviewLine_->Draw();

#ifdef _DEBUG
    player_->DrawDebugHitBoxes(enemyMgr_);
#endif

    enemyMgr_.Draw();

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
    ImGui::Begin("Boss Knockback Test");

    ImGui::Checkbox("Boss AI Enabled", &bossAIEnabled_);
    ImGui::Checkbox("Enable Edge Transition", &enableEdgeTransition_);
    ImGui::Checkbox("Apply Hit Immediately", &applyBossHitImmediately_);

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

    auto triggerBossTestHit = [&](Enemy& boss, MeleeKind kind) {
        boss.RequestMelee(kind);

        if (!applyBossHitImmediately_ || !player_) {
            return;
        }

        EnemyManager::BossHitTuning tuning = enemyMgr_.BossTuning(kind);
        Vector3 dir = tuning.knockbackDir;
        const float dirX = (player_->GetX() >= boss.GetPos3D().x) ? 1.0f : -1.0f;
        dir.x = std::abs(dir.x) * dirX;

        player_->ApplyBossHit(
            tuning.damagePercent,
            tuning.baseKnockback,
            tuning.knockbackScale,
            dir,
            tuning.hitStunSec);
    };

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        Vector3 bossPos = boss->GetPos3D();
        if (ImGui::DragFloat3("Boss Pos", &bossPos.x, 0.1f)) {
            boss->SetPos(bossPos);
        }

        if (ImGui::Button("Boss Normal")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Melee_Dash);
            triggerBossTestHit(*boss, MeleeKind::Normal);
        }
        ImGui::SameLine();
        if (ImGui::Button("Boss Jump Slash")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Drop_Windup);
            triggerBossTestHit(*boss, MeleeKind::Land);
        }
        ImGui::SameLine();
        if (ImGui::Button("Boss Rush")) {
            boss->GetBossAIMutable().ForceChangeState(BossAI::State::Rush_ToRight);
            triggerBossTestHit(*boss, MeleeKind::Rush);
        }
    } else {
        ImGui::TextUnformatted("Boss: none");
    }

    if (ImGui::Button("Reset Fighter Positions")) {
        resetFightersRequested_ = true;
    }

    if (ImGui::CollapsingHeader("Boss Tuning Preset", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Tuning Path", bossTuningPath_, IM_ARRAYSIZE(bossTuningPath_));
        if (ImGui::Button("Save Boss Tuning")) {
            SaveBossTuning_(bossTuningPath_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Boss Tuning")) {
            LoadBossTuning_(bossTuningPath_);
        }
        if (!bossTuningStatus_.empty()) {
            ImGui::TextUnformatted(bossTuningStatus_.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Knockback Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* attackLabels[] = { "Normal / Combo", "Jump Slash / Land", "Rush" };
        const char* lineModeLabels[] = { "Actual Distance", "Launch Velocity" };
        ImGui::Checkbox("Draw Preview Line", &drawKnockbackPreview_);
        ImGui::Checkbox("Freeze Line While Launched", &freezeKnockbackPreviewWhileLaunched_);
        ImGui::Combo("Preview Line Mode", &previewLineMode_, lineModeLabels, 2);
        ImGui::Combo("Preview Attack", &previewAttackKind_, attackLabels, 3);
        ImGui::Checkbox("Use Player Damage", &previewUsesPlayerPercent_);
        if (!previewUsesPlayerPercent_) {
            ImGui::DragFloat("Preview Percent", &previewPercent_, 1.0f, 0.0f, 999.0f);
        }
        const char* scaleLabel = previewLineMode_ == 0 ? "Distance Line Scale (1 = actual)" : "Velocity Line Scale";
        ImGui::DragFloat(scaleLabel, &previewLineScale_, 0.01f, 0.01f, 5.0f);
        ImGui::DragFloat("Line Thickness", &previewLineThickness_, 0.01f, 0.01f, 2.0f);

        const MeleeKind previewKind = PreviewKindFromIndex(previewAttackKind_);
        const float percent = previewUsesPlayerPercent_ && player_ ? player_->GetDamagePercent() : previewPercent_;
        if (player_) {
            const KnockbackPreviewMetrics metrics = CalcKnockbackPreviewMetrics(
                *player_,
                enemyMgr_,
                previewKind,
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
    }

    if (ImGui::CollapsingHeader("Out Of Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &outOfBoundsEnabled_);
        ImGui::DragFloat("Left X", &outLeftX_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat("Right X", &outRightX_, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Bottom Y", &outBottomY_, 0.5f, -200.0f, 0.0f);
        ImGui::DragFloat3("Drop Respawn Pos", &dropRespawnPos_.x, 0.1f);
        ImGui::Checkbox("Reset Damage On Out", &resetDamageOnOutOfBounds_);
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

    drawBossTuning("Normal / Combo", enemyMgr_.BossTuning(MeleeKind::Normal));
    drawBossTuning("Jump Slash / Land", enemyMgr_.BossTuning(MeleeKind::Land));
    drawBossTuning("Rush", enemyMgr_.BossTuning(MeleeKind::Rush));

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Next Work", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("Add recovery special after launch");
        ImGui::BulletText("Promote test respawn into proper stock/KO flow");
        ImGui::BulletText("Add KO effect before drop respawn or result transition");
        ImGui::BulletText("Move tuned boss hit values into battle-side defaults");
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

    // ★Dirは正規化しないと壊れやすいので、ボタンで正規化も入れる
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
