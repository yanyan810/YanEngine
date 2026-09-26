#include "GameScene.h"
#include "GameApp.h"
#include "Object3dCommon.h"
#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "EnemySpawnDebug.h"
#include <string>
#include <numbers>
#ifdef USE_IMGUI
#include "imgui.h"
namespace { constexpr int kCapturedMouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange; }
#endif

namespace {
    constexpr float kNormalFovDegrees = 60.0f;

    constexpr float kDegreesToRadians =
        std::numbers::pi_v<float> / 180.0f;
}

void GameScene::OnEnter(GameApp& app) {
#ifdef _DEBUG
    debugHistory_.Clear(); debugFrame_=0; debugStep_=0; debugJson_=DebugJsonEditor{};
    debugPaused_=debugPauseOnEnter_; debugPauseOnEnter_=false;
#endif
    app.GetInput()->SetCameraToggleKeyEnabled(false);
#ifdef USE_IMGUI
    savedMouseFlags_ = ImGui::GetIO().ConfigFlags & kCapturedMouseFlags;
#endif
    camera_.SetFovY(
        kNormalFovDegrees * kDegreesToRadians);

    camera_.Update();
    app.ObjCom()->SetDefaultCamera(&camera_);
    player_.Initialize(app.ObjCom(), app.Dx(), &camera_);
    adsBlend_ = 0.0f;
    suppressFireUntilRelease_ = false;
    player_.SetLookSensitivityMultiplier(1.0f);

    enemyDefinitions_=EnemyDefinitions{};
    const bool enemiesLoaded=enemyDefinitions_.Load("resources/Data/enemies.json");
    if (!enemiesLoaded) OutputDebugStringA(("Enemy configuration error: " + enemyDefinitions_.Error() + "\n").c_str());
    StageLoader nextLevel;
    WeaponSystem nextWeapons;
    EnemySpawnSystem nextSpawns;
    StageProgress nextProgress;
    const std::string levelPath=showroom_ ? "resources/levels/showroom/showroom.json" : "resources/levels/stage01/stage01.json";
    stageLoaded_=enemiesLoaded && nextLevel.Load(levelPath) && nextLevel.ValidateAssets() &&
        nextWeapons.Load("resources/Data/weapons.json",levelPath,weaponSeedOverride_) &&
        nextSpawns.Load(levelPath,enemyDefinitions_) && nextProgress.LoadGoals(levelPath);
    if (stageLoaded_) {
        level_=std::move(nextLevel); weapons_=std::move(nextWeapons);
        spawnSystem_=std::move(nextSpawns); stage_=std::move(nextProgress);
    } else {
        OutputDebugStringA(("Stage fallback: " + nextLevel.Error()+" "+nextWeapons.Error()+" "+nextSpawns.Error()+" "+nextProgress.Error()+"\n").c_str());
        level_=StageLoader{}; weapons_=WeaponSystem{}; spawnSystem_=EnemySpawnSystem{}; stage_=StageProgress{};
        const std::string fallback="resources/levels/fps_spawns.json";
        if (!showroom_ && !weapons_.Load("resources/Data/weapons.json",fallback,weaponSeedOverride_)) OutputDebugStringA(weapons_.Error().c_str());
        if (!showroom_ && enemiesLoaded && !spawnSystem_.Load(fallback,enemyDefinitions_)) OutputDebugStringA(spawnSystem_.Error().c_str());
        if (!showroom_ && !stage_.LoadGoals(fallback)) OutputDebugStringA(stage_.Error().c_str());
    }
    player_.SetStage(&level_.collision,level_.playerPosition,level_.playerRotation);
    weaponVisuals_.clear();
    if (const auto* initial = weapons_.InitialWeapon()) player_.CurrentWeapon().Equip(*initial);
    pelletRandom_.seed(weapons_.ActualSeed() ^ 0x9e3779b9u); // independent of placement lottery
    gameHUD_.Initialize(app.SpriteCom(),app.Dx());
    for (const auto& pickup : weapons_.Pickups()) {
        auto visual = std::make_unique<Object3d>();
        visual->Initialize(app.ObjCom(),app.Dx());
        visual->SetCamera(&camera_);
        visual->SetModel("cube/cube.obj");
        visual->SetTexture("resources/white1x1.png");
        const auto& definition = *weapons_.Find(pickup.weaponId);
        visual->SetScale(definition.pickupScale);
        visual->SetTranslate(pickup.position);
        visual->SetRotate(pickup.rotation);
        visual->SetMaterialColor({definition.pickupColor.x,definition.pickupColor.y,definition.pickupColor.z,1});
        visual->SetEnableLighting(0);
        visual->Update(0);
        weaponVisuals_.push_back(std::move(visual));
    }
    enemies_.clear();
    enemyProjectiles_.Clear(); projectileVisuals_.clear();
    clearOverlay_.Initialize(app.SpriteCom(), app.Dx());
    selectedEnemy_ = 0; lastHitEnemy_ = -1;
    enemyAttackCount_ = 0; playerDamagedFlash_ = 0; lastEnemyDamage_ = 0;
    ground_.Initialize(app.ObjCom(), app.Dx());
    ground_.SetCamera(&camera_);
    if (stageLoaded_) {
        ground_.SetModel(level_.model); ground_.StopAnimation();
        ground_.SetScale({1,1,1}); ground_.SetTranslate({}); ground_.SetRotate({});
        ground_.SetMaterialColor({1,1,1,1});
    } else {
        ground_.SetModel("cube/cube.obj"); ground_.SetTexture("resources/white1x1.png");
        ground_.SetScale({32,.25f,60}); ground_.SetTranslate({0,-.25f,24});
        ground_.SetMaterialColor({.45f,.48f,.52f,1});
    }
    ground_.SetEnableLighting(1);
    ground_.SetDirection({0.3f, -1.0f, 0.5f});
    ground_.SetIntensity(1.0f);
    ground_.SetPointLightIntensity(0.0f);
    ground_.SetSpotLightIntensity(0.0f);
    for (Sprite* sprite : {&crosshairHorizontal_, &crosshairVertical_}) {
        sprite->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
        sprite->SetAnchorPoint({0.5f, 0.5f});
        sprite->SetPosition({WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f});
        sprite->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    }
    const auto& texture = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    const float width = static_cast<float>(texture.width);
    const float height = static_cast<float>(texture.height);
    crosshairHorizontal_.SetScale({12.0f / width, 2.0f / height, 1.0f});
    crosshairVertical_.SetScale({2.0f / width, 12.0f / height, 1.0f});
    if (showroom_) { freezeEnemies_=true; ResetShowroomEnemies(app); }
    Update(app, 0.0f);
#ifdef _DEBUG
    if (debugHistory_.Size()==0) debugHistory_.Push(CaptureDebug());
#endif
}
void GameScene::OnExit(GameApp& app) {
    app.GetInput()->SetCameraControlEnabled(false);
    app.GetInput()->SetMouseCaptureRect(nullptr);
    app.GetInput()->SetCameraToggleKeyEnabled(true);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags = (ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) | savedMouseFlags_;
#endif
    enemies_.clear();
    enemyProjectiles_.Clear(); projectileVisuals_.clear();
    app.ObjCom()->SetDefaultCamera(nullptr);
}
void GameScene::Update(GameApp& app, float dt) {
    // SceneManager consumes this request after Update, safely outside ImGui drawing.
    if (!NextScene().empty()) return;
    if (showroom_ && resetEnemiesPending_) {
        app.Dx()->WaitForGPU();
        ResetShowroomEnemies(app); resetEnemiesPending_=false;
#ifdef _DEBUG
        debugHistory_.Clear(); debugHistory_.Push(CaptureDebug());
#endif
    }
    dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
    Input& input = *app.GetInput();
    const bool wasCaptured = input.IsCameraControlEnabled();
    bool viewReady = true;
    bool clickedView = false;
#ifdef USE_IMGUI
    RECT sceneRect{};
    viewReady = app.ImGui()->GetSceneImageRect(sceneRect);
    if (viewReady) input.SetMouseCaptureRect(&sceneRect);
    clickedView = viewReady && app.ImGui()->IsSceneImageHovered() && input.IsLeftMouseTrigger();
#else
    input.SetMouseCaptureRect(nullptr);
    POINT cursor{};
    RECT client{};
    GetCursorPos(&cursor);
    ScreenToClient(app.Win()->GetHwnd(), &cursor);
    GetClientRect(app.Win()->GetHwnd(), &client);
    clickedView = PtInRect(&client, cursor) && input.IsLeftMouseTrigger();
#endif
#ifdef _DEBUG
    bool typing=false;
#ifdef USE_IMGUI
    typing=ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
#endif
    if (input.HasFocus() && input.IsKeyTrigger(DIK_F1)) {
        debugPaused_=!debugPaused_;
        initialCapturePending_=!debugPaused_;
        suppressFireUntilRelease_=true;
    }
    if (debugPaused_) {
        input.SetCameraControlEnabled(false); initialCapturePending_=false;
#ifdef USE_IMGUI
        ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) | savedMouseFlags_;
#endif
        if (input.HasFocus() && !typing) {
            if (input.IsKeyTrigger(DIK_COMMA)) debugStep_=-1;
            else if (input.IsKeyTrigger(DIK_PERIOD)) debugStep_=1;
        }
        const int step=debugStep_; debugStep_=0;
        if (step<0) {
            if (const auto* state=debugHistory_.Back()) RestoreDebug(app,*state);
            return;
        }
        if (step>0) {
            if (const auto* state=debugHistory_.Forward()) { RestoreDebug(app,*state); return; }
            dt=1.0f/60.0f;
        } else {
            player_.RefreshDebug(); ground_.Update(0);
            for (auto& visual : weaponVisuals_) visual->Update(0);
            for (auto& enemy : enemies_) enemy->UpdateVisuals(0);
            SyncProjectileVisuals(app);
            return;
        }
    }
#endif
    if (!stage_.IsPlaying() || !viewReady || input.IsKeyTrigger(DIK_ESCAPE)) {
        input.SetCameraControlEnabled(false);
        if (input.IsKeyTrigger(DIK_ESCAPE)) initialCapturePending_ = false;
    }
    else if (initialCapturePending_ || clickedView) {
#ifdef _DEBUG
        if (!debugPaused_)
#endif
        {

        input.SetCameraControlEnabled(true);

        if (clickedView && !wasCaptured) {
            suppressFireUntilRelease_ = true;
        }

        initialCapturePending_ = false;
        }
    }
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags = (ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) |
        (input.IsCameraControlEnabled() ? kCapturedMouseFlags : savedMouseFlags_);
#endif

    if (!input.IsLeftMousePressed()) {
        suppressFireUntilRelease_ = false;
    }

    if (stage_.IsPlaying()) {

        for (const auto& enemy : enemies_) {
            stage_.ObserveEnemy(
                enemy->GetSpawnId(),
                enemy->IsDead());
        }

        UpdateADS(input, dt);

        player_.Update(input, dt);

        if (!showroom_ && stage_.Update(
            dt,
            player_.GetTransform().translate)) {

            OnStageClear(app);
        }
    }

    if (stage_.IsPlaying()) UpdateCombat(app, dt, wasCaptured);
    else for (auto& enemy : enemies_) enemy->UpdateVisuals(dt);
    ground_.Update(dt);

    for (auto& visual : weaponVisuals_) {
        visual->Update(dt);
    }
#ifdef _DEBUG
    if (dt>0) { ++debugFrame_; debugHistory_.Push(CaptureDebug()); }
#endif
}

void GameScene::OnStageClear(GameApp& app) {
    enemyProjectiles_.Clear(); projectileVisuals_.clear();
    initialCapturePending_ = false;
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags = (ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) | savedMouseFlags_;
#endif

    adsBlend_ = 0.0f;

    player_.SetLookSensitivityMultiplier(1.0f);

    camera_.SetFovY(
        kNormalFovDegrees *
        kDegreesToRadians);

    camera_.Update();

    clearOverlay_.SetResult(stage_, static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    const auto time = StageProgress::FormatTime(stage_.Time());
    const std::wstring title = L"STAGE CLEAR | Time: " + std::wstring(time.begin(),time.end()) +
        L" | Enemies Defeated: " + std::to_wstring(stage_.DefeatedCount());
    SetWindowTextW(app.Win()->GetHwnd(), title.c_str());
}


void GameScene::UpdateADS(
    const Input& input,
    float dt)
{
    const auto& definition =
        player_.CurrentWeapon().Definition();

    const bool controls =
        input.IsCameraControlEnabled() &&
        input.HasFocus() &&
        stage_.IsPlaying();

    const bool wantsADS =
        controls &&
        input.IsRightMousePressed() &&
        !definition.id.empty();

    const float target =
        wantsADS ? 1.0f : 0.0f;

    const float transitionTime =
        std::max(
            0.01f,
            definition.adsTransitionTime);

    const float step =
        dt / transitionTime;

    if (target > adsBlend_) {
        adsBlend_ =
            std::min(target, adsBlend_ + step);
    }
    else if (target < adsBlend_) {
        adsBlend_ =
            std::max(target, adsBlend_ - step);
    }

    const float adsFov =
        definition.id.empty()
        ? kNormalFovDegrees
        : definition.adsFovDegrees;

    const float currentFovDegrees =
        kNormalFovDegrees +
        (adsFov - kNormalFovDegrees) *
        adsBlend_;

    camera_.SetFovY(
        currentFovDegrees *
        kDegreesToRadians);

    const float adsSensitivity =
        definition.id.empty()
        ? 1.0f
        : definition.adsSensitivityMultiplier;

    const float sensitivityMultiplier =
        1.0f +
        (adsSensitivity - 1.0f) *
        adsBlend_;

    player_.SetLookSensitivityMultiplier(
        sensitivityMultiplier);
}

void GameScene::UpdateCombat(GameApp& app, float dt, bool wasCaptured) {
    Input& input = *app.GetInput();
    auto& weapon = player_.CurrentWeapon();
    const bool controls = wasCaptured && input.IsCameraControlEnabled() && input.HasFocus();
    const bool pickedUp = controls && input.IsKeyTrigger(DIK_E) && weapons_.TryPickup(player_.GetTransform().translate,weapon);
    const bool allowFire = controls && !pickedUp && !suppressFireUntilRelease_;
#ifdef _DEBUG
    if (!debugPaused_ && !allowFire) weapon.CancelBurst();
#else
    if (!allowFire) weapon.CancelBurst();
#endif
    const int weaponShots = weapon.Step(pickedUp ? 0.0f : dt,
        allowFire && input.IsLeftMouseTrigger(), allowFire && input.IsLeftMousePressed(),
        controls && input.IsKeyTrigger(DIK_R));
    if (showroom_ && pickedUp) weapons_.ResetPickups();
    if (!showroom_) spawnSystem_.Update(dt, player_.GetTransform().translate,
        [&](const EnemySpawnPoint& point, const std::string& trigger) {
            auto enemy = std::make_unique<Enemy>();
            const uint64_t id = nextEnemyId_++;
            enemy->SetSpawnIdentity(id, trigger);
            enemy->SetPosition(point.position);
            enemy->SetRotation(point.rotation);
            enemy->Initialize(app.ObjCom(), app.Dx(), &camera_, true);
            enemy->ApplyDefinition(*enemyDefinitions_.Find(spawnSystem_.SelectEnemyId(point)));
            enemies_.push_back(std::move(enemy));
            return id;
        },
        [&](uint64_t id) {
            for (const auto& enemy : enemies_)
                if (enemy->GetSpawnId() == id) return !enemy->IsDead();
            return false;
        });
    playerDamagedFlash_ = std::max(0.0f, playerDamagedFlash_-dt);
    const float projectileDamage=stageLoaded_ ? UpdateStageProjectiles(enemyProjectiles_,dt,player_.GetTransform().translate,level_.collision) :
        enemyProjectiles_.Update(dt,player_.GetTransform().translate);
    if (projectileDamage>0) {
        const float before=player_.GetHP(); player_.ApplyDamage(projectileDamage);
        lastEnemyDamage_=before-player_.GetHP(); playerDamagedFlash_=.35f;
    }
    // Symmetric XZ separation from a snapshot; dead bodies do not push living enemies.
    std::vector<Vector3> correction(enemies_.size());
    if (!showroom_ || !freezeEnemies_) for (size_t i=0;i<enemies_.size();++i) for(size_t j=i+1;j<enemies_.size();++j) {
        if (enemies_[i]->IsDead() || enemies_[j]->IsDead()) continue;
        const auto offset=EnemySeparationOffset(enemies_[i]->GetPosition(),enemies_[i]->Definition().collisionRadius,
            enemies_[j]->GetPosition(),enemies_[j]->Definition().collisionRadius,dt);
        correction[i]=correction[i]+offset; correction[j]=correction[j]-offset;
    }
    std::vector<float> enemyDamage(enemies_.size());
    for(size_t i=0;i<enemies_.size();++i) {
        if (showroom_ && freezeEnemies_) { enemies_[i]->UpdateVisuals(dt); continue; }
        const auto previous=enemies_[i]->GetPosition();
        enemies_[i]->SetPosition(previous+correction[i]);
        enemyDamage[i]=enemies_[i]->Update(dt,player_.GetTransform().translate);
        if (!enemies_[i]->IsDead()) {
            const auto& definition=enemies_[i]->Definition();
            enemies_[i]->SetPosition(level_.collision.Move(previous,enemies_[i]->GetPosition(),definition.collisionRadius,definition.collisionHeight));
            enemies_[i]->UpdateVisuals(0);
        }
    }
    // A click used to acquire FPS control is consumed, never a shot.
    for (int shot=0; shot<weaponShots; ++shot)
    {
        ++shotCount_;
        const auto& definition = weapon.Definition();
        const auto& world = camera_.GetWorldMatrix();
        const Vector3 forward{world.m[2][0], world.m[2][1], world.m[2][2]};
        const Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]};
        const Vector3 up{world.m[1][0],world.m[1][1],world.m[1][2]};
        for (int pellet=0; pellet<definition.pelletCount; ++pellet) {
            const float currentSpread =
                definition.hipSpreadDegrees +
                (
                    definition.adsSpreadDegrees -
                    definition.hipSpreadDegrees
                    ) * adsBlend_;

            const auto direction =
                WeaponPelletDirection(
                    forward,
                    right,
                    up,
                    currentSpread,
                    pelletRandom_);

        struct SceneHit { Enemy* enemy=nullptr; Enemy::RaycastHit hit{}; int index=-1; } closest;
        float range=definition.range;
        StageHit wallHit;
        const bool wall=level_.collision.Raycast(camera_.GetTranslate(),direction,range,wallHit);
        if (wall) range=std::max(0.0f,wallHit.distance-.001f);
        for(size_t i=0;i<enemies_.size();++i) {
            Enemy::RaycastHit candidate;
            if (enemies_[i]->Raycast(camera_.GetTranslate(),direction,range,candidate) &&
                (!closest.enemy || candidate.distance<range)) {
                range=candidate.distance;
                closest={enemies_[i].get(),candidate,static_cast<int>(i)};
            }
        }
        if (closest.enemy) {
            ++hitCount_;
            lastHitPart_=closest.hit.part;
            lastHitEnemy_=closest.index;
            lastDamage_=closest.enemy->ApplyDamage(closest.hit.part,definition.damage,direction);
            closest.enemy->ShowHitFeedback(closest.hit.part);
        }
        }

    }
    for(size_t i=0;i<enemies_.size();++i) {
        if (enemies_[i]->IsDead() || (showroom_ && freezeEnemies_)) continue;
        if (enemies_[i]->Definition().IsRanged() && enemies_[i]->PendingAttackCount()>0) {
            enemyProjectiles_.projectiles.push_back(MakeEnemyProjectile(enemies_[i]->Definition(),
                enemies_[i]->GetPosition()+Vector3{0,1.2f,0},player_.GetTransform().translate));
            enemies_[i]->ConfirmAttack(0);
            enemyAttackCount_+=enemies_[i]->PendingAttackCount();
        }
        if (enemyDamage[i]<=0) continue;
        const float before=player_.GetHP();
        player_.ApplyDamage(enemyDamage[i]);
        lastEnemyDamage_=before-player_.GetHP();
        enemies_[i]->ConfirmAttack(lastEnemyDamage_);
        enemyAttackCount_+=enemies_[i]->PendingAttackCount();
        playerDamagedFlash_=.35f;
    }
    for (const auto& enemy : enemies_) stage_.ObserveEnemy(enemy->GetSpawnId(), enemy->IsDead());
    SyncProjectileVisuals(app);
    const std::string selectedState = enemies_.empty() ? "No Enemy" : EnemyStateName(enemies_[static_cast<size_t>(selectedEnemy_)]->GetState());
    const auto status = std::wstring(L"FPS Foundation | Player HP: ") + std::to_wstring(static_cast<int>(player_.GetHP())) +
        (player_.IsDead() ? L" (Player Dead)" : L"") + L" | Enemy: " +
        std::wstring(selectedState.begin(), selectedState.end()) +
        L" | Shots: " + std::to_wstring(shotCount_) + L" | Hits: " + std::to_wstring(hitCount_);
    const std::string partName = EnemyPartName(lastHitPart_);
    const auto fullStatus = status + L" | Last Hit Part: " + std::wstring(partName.begin(), partName.end()) +
        L" | Last Damage: " + std::to_wstring(static_cast<int>(lastDamage_)) +
        L" | Enemy Attacks: " + std::to_wstring(enemyAttackCount_);
    SetWindowTextW(app.Win()->GetHwnd(), fullStatus.c_str());
}

void GameScene::DrawRender(GameApp&) {
    ground_.Draw();
    for (size_t i=0; i<weaponVisuals_.size(); ++i)
        if (weapons_.Pickups()[i].visible) weaponVisuals_[i]->Draw();
    for(auto& enemy : enemies_) enemy->Draw(!showroom_ || showEnemyMarkers_);
    for(auto& visual : projectileVisuals_) visual->Draw();
}


void GameScene::DrawImGui(GameApp& app) {
#if defined(_DEBUG) && defined(USE_IMGUI)
    DrawDebugTools(app);
    if (showroom_) DrawShowroomTools(app);
#endif
#ifdef USE_IMGUI
    ImGui::Begin("FPS Controls");
#ifdef _DEBUG
    if (ImGui::Button(showroom_ ? "Return to Game" : "Open Showroom")) RequestChangeScene_(showroom_ ? "Game" : "Showroom");
#endif
    if (!spawnSystem_.Error().empty()) ImGui::TextWrapped("Spawn configuration error: %s", spawnSystem_.Error().c_str());
    const bool captured = app.GetInput()->IsCameraControlEnabled();
    ImGui::TextUnformatted(!stage_.IsPlaying() ? "Stage cleared. Gameplay stopped; debug controls remain available." :
        captured ? "WASD: walk | Mouse: look | LMB: fire | ESC: release" : "Click the Scene image to resume FPS controls.");
    ImGui::Text("Shot Count: %llu | Hit Count: %llu", shotCount_, hitCount_);
    ImGui::Text("Last Hit Enemy: %d | Last Hit Part: %s", lastHitEnemy_, EnemyPartName(lastHitPart_));
    ImGui::Text("Last Damage: %.0f (actual HP lost)", lastDamage_);

    const auto alive=std::count_if(enemies_.begin(),enemies_.end(),[](const auto& e){return !e->IsDead();});
    ImGui::Text("Enemy Count: %zu | Alive: %d", enemies_.size(), static_cast<int>(alive));
    ImGui::Text("Last Enemy Attack: %s | Attack Count: %llu | Last Damage: %.0f",
        playerDamagedFlash_>0 ? "HIT / Player Damaged!" : "-", enemyAttackCount_, lastEnemyDamage_);
    const auto& transform = player_.GetTransform();
    ImGui::Text("Position: %.2f, %.2f, %.2f", transform.translate.x, transform.translate.y, transform.translate.z);
    ImGui::Text("Yaw / Pitch: %.1f / %.1f deg", transform.rotate.y * 57.2957795f, transform.rotate.x * 57.2957795f);
    ImGui::BeginDisabled(captured);
    if (ImGui::Button("Reset Player HP (Debug)")) player_.ResetHPForDebug();
    auto& settings = player_.Settings();
    ImGui::SliderFloat("Eye height", &settings.cameraHeight, 0.5f, 2.5f);
    ImGui::SliderFloat("Move speed", &settings.moveSpeed, 0.5f, 15.0f);
    ImGui::SliderFloat("Mouse sensitivity", &settings.mouseSensitivity, 0.0005f, 0.01f, "%.4f");
    for (const auto& enemy : enemies_)
        ImGui::Text("%s | %s | %s", enemy->GetId().c_str(), EnemyTypeName(enemy->Definition().type), enemy->GetSpawnTriggerId().c_str());
    ImGui::Text("Enemy Projectiles: %zu", enemyProjectiles_.projectiles.size());
    const auto selectedLabel=enemies_.empty() ? std::string("No Enemy") : enemies_[static_cast<size_t>(selectedEnemy_)]->GetId();
    if (ImGui::BeginCombo("Selected Enemy", selectedLabel.c_str())) {
        for(int i=0;i<static_cast<int>(enemies_.size());++i) {
            const auto& label=enemies_[static_cast<size_t>(i)]->GetId();
            if (ImGui::Selectable(label.c_str(), selectedEnemy_==i)) selectedEnemy_=i;
        }
        ImGui::EndCombo();
    }
    if (!enemies_.empty()) {
        ImGui::Text("Source: %s", enemies_[static_cast<size_t>(selectedEnemy_)]->GetSpawnTriggerId().c_str());
        enemies_[static_cast<size_t>(selectedEnemy_)]->DrawImGui();
    }
    ImGui::EndDisabled();
    ImGui::End();
    RECT sceneRect{};
    if(!showroom_ && !enemies_.empty() && app.ImGui()->GetSceneImageRect(sceneRect)) enemies_[static_cast<size_t>(selectedEnemy_)]->DrawPartDebug(camera_.GetViewProjectionMatrix(),
        {static_cast<float>(sceneRect.left),static_cast<float>(sceneRect.top)},
        {static_cast<float>(sceneRect.right),static_cast<float>(sceneRect.bottom)});
#ifdef _DEBUG
    ImGui::Begin("Weapon System");
    auto& weapon = player_.CurrentWeapon();
    ImGui::BeginDisabled(captured || !stage_.IsPlaying() || weapons_.Definitions().empty());
    const auto& equipped = weapon.Definition();
    if (ImGui::BeginCombo("Equip Weapon (Debug)", equipped.id.empty() ? "No weapon" : equipped.displayName.c_str())) {
        for (const auto& candidate : weapons_.Definitions()) {
            const bool selected = candidate.id == weapon.Definition().id;
            const auto label = candidate.displayName + " (" + candidate.id + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                weapon.Equip(candidate);
                suppressFireUntilRelease_ = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextUnformatted("ESC to select. Equipping resets ammo, reload, burst and cooldown.");
    const auto& definition = weapon.Definition();
    ImGui::Text("Current: %s (%s)",definition.displayName.c_str(),definition.id.c_str());
    ImGui::Text("Magazine: %d / %d | Reserve: %d / %d",weapon.Magazine(),definition.magazineSize,weapon.Reserve(),definition.maxReserveAmmo);
    ImGui::Text("Damage/pellet: %.1f | Range: %.1f | Interval: %.2f",definition.damage,definition.range,definition.fireInterval);
  
    const float rpm =
        definition.fireInterval > 0.0f
        ? 60.0f / definition.fireInterval
        : 0.0f;

    ImGui::Text(
        "Fire Mode: %s | RPM: %.0f",
        WeaponFireModeName(definition.fireMode),
        rpm);

    ImGui::Text(
        "ADS Blend: %.2f | ADS FOV: %.1f",
        adsBlend_,
        definition.adsFovDegrees);

    ImGui::Text(
        "Spread Hip / ADS: %.2f / %.2f",
        definition.hipSpreadDegrees,
        definition.adsSpreadDegrees);
    
    ImGui::Text(
        "Pellets: %d | Cooldown: %.2f",
        definition.pelletCount,
        weapon.Cooldown());

    ImGui::Text("Reload: %s | Remaining: %.2f / %.2f",weapon.Reloading()?"yes":"no",weapon.ReloadRemaining(),definition.reloadTime);
    ImGui::Text("Reload Mode: %s | State: %s",WeaponReloadModeName(definition.reloadMode),WeaponReloadStateName(weapon.ReloadState()));
    ImGui::Text("Reload start / round / end: %.2f / %.2f / %.2f | Interrupt: %s",
        definition.reloadStartTime,definition.reloadPerRoundTime,definition.reloadEndTime,definition.reloadCanInterrupt?"yes":"no");
    ImGui::Text("Ammo per shot: %d | Burst remaining: %d | Next burst shot: %.2f",definition.ammoPerShot,weapon.BurstRemaining(),weapon.BurstTimer());
    ImGui::Text("Burst count / interval: %d / %.2f",definition.burstCount,definition.burstInterval);
    ImGui::TextUnformatted("LMB: weapon fire mode | RMB: ADS | E: nearest pickup | R: reload");
    if (!weapons_.Error().empty()) ImGui::TextWrapped("Config error: %s",weapons_.Error().c_str());
    auto randomSettings = weaponSeedOverride_.value_or(weapons_.Settings());
    bool seedChanged = ImGui::Checkbox("Use Fixed Seed (next restart)",&randomSettings.useFixedSeed);
    seedChanged |= ImGui::InputScalar("Seed (next restart)",ImGuiDataType_U32,&randomSettings.seed);
    if (seedChanged) weaponSeedOverride_ = randomSettings;
    if (ImGui::Button("Use JSON seed settings next restart")) weaponSeedOverride_.reset();
    ImGui::Text("This stage seed: %u",weapons_.ActualSeed());
    const auto nearest = weapons_.Nearest(player_.GetTransform().translate);
    for (size_t i=0; i<weapons_.Points().size(); ++i) {
        const auto& point = weapons_.Points()[i];
        if (!ImGui::TreeNode(point.id.c_str())) continue;
        ImGui::Text("Position: %.1f, %.1f, %.1f",point.position.x,point.position.y,point.position.z);
        for (const auto& candidate : point.weaponPool) ImGui::BulletText("%s (weight %.1f)",candidate.id.c_str(),candidate.weight);
        const auto& pickup = weapons_.Pickups()[i];
        ImGui::Text("Selected: %s | %s %s",pickup.weaponId.c_str(),pickup.pickedUp?"Picked up":"Available",
            nearest && *nearest==i ? "[nearest]":"");
        ImGui::TreePop();
    }
    ImGui::End();
    if (!showroom_) {
    ImGui::Begin("Stage Progress");
    ImGui::Text("Stage State: %s", stage_.IsPlaying() ? "Playing" : "Cleared");
    ImGui::Text("%s: %s", stage_.IsPlaying() ? "Elapsed Time" : "Clear Time", StageProgress::FormatTime(stage_.Time()).c_str());
    ImGui::Text("Defeated: %zu", stage_.DefeatedCount());
    if (!stage_.Error().empty()) ImGui::TextWrapped("Goal configuration error: %s", stage_.Error().c_str());
    if (stage_.Goals().empty()) ImGui::TextUnformatted("No goals configured (spawn-only level).");
    ImGui::Checkbox("Show Goal boxes", &showGoalDebug_);
    for (const auto& goal : stage_.Goals()) {
        const auto delta = player_.GetTransform().translate - goal.position;
        ImGui::Text("GOAL: %s | Distance: %.1f | Activated: %s", goal.id.c_str(),
            std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z), goal.activated ? "true" : "false");
    }
    if (ImGui::Button("Restart Stage")) RequestChangeScene_("Game");
    ImGui::BeginDisabled(!stage_.IsPlaying() || captured);
    if (ImGui::TreeNode("Test positions (debug teleport)")) {
        const auto moveTo = [&](const std::string& id, const Vector3& position) {
            if (ImGui::Button(("Move to " + id).c_str()))
                player_.SetPositionForDebug({position.x,player_.GetTransform().translate.y,position.z});
        };
        for (const auto& trigger : spawnSystem_.Triggers()) moveTo(trigger.id, trigger.position);
        for (const auto& goal : stage_.Goals()) moveTo(goal.id, goal.position);
        for (const auto& point : weapons_.Points()) moveTo(point.id, point.position);
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    ImGui::End();
    ImGui::Begin("Spawn System");
    if (!stage_.IsPlaying()) ImGui::TextUnformatted("Stage cleared: all spawn schedules are frozen.");
    ImGui::Checkbox("Show trigger boxes / points / links", &showSpawnDebug_);
    ImGui::TextUnformatted("Map: resources/levels/fps_spawns.json (restart to reload)");
    for (const auto& trigger : spawnSystem_.Triggers()) {
        if (!ImGui::TreeNode(trigger.id.c_str())) continue;
        size_t alive = 0;
        for (const auto& enemy : enemies_)
            if (enemy->GetSpawnTriggerId() == trigger.id && !enemy->IsDead()) ++alive;
        ImGui::Text("State: %s", trigger.active ? "Active" : trigger.activated ? "Completed" : "Inactive");
        ImGui::Text("Spawned: %d / %d | Alive: %zu / %d", trigger.spawned, trigger.spawnCount, alive, trigger.maxAlive);
        ImGui::Text("Next Spawn: %.2f sec %s", std::max(0.0, trigger.nextSpawn),
            trigger.active && alive >= static_cast<size_t>(trigger.maxAlive) ? "(waiting for capacity)" : "");
        ImGui::Text("Interval: %.2f | InitialDelay: %.2f", trigger.spawnInterval, trigger.initialDelay);
        ImGui::Text("OneShot: %s | Selection: %s", trigger.oneShot ? "true" : "false",
            trigger.selection == SpawnPointSelection::Random ? "Random" : "RoundRobin");
        for (const auto& id : trigger.spawnPointIds) ImGui::BulletText("%s", id.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Spawn Points")) {
        for (const auto& point : spawnSystem_.Points()) ImGui::Text("%s: (%.1f, %.1f, %.1f)",
            point.id.c_str(), point.position.x, point.position.y, point.position.z);
        ImGui::TreePop();
    }
    ImGui::Checkbox("Show Stage Colliders", &showStageColliders_);
    ImGui::End();
    }
    if ((showSpawnDebug_ || showGoalDebug_ || showStageColliders_) && app.ImGui()->GetSceneImageRect(sceneRect))
        DrawEnemySpawnDebug(spawnSystem_, camera_.GetViewProjectionMatrix(),
            {static_cast<float>(sceneRect.left),static_cast<float>(sceneRect.top)},
            {static_cast<float>(sceneRect.right),static_cast<float>(sceneRect.bottom)},
            !showroom_ && showSpawnDebug_, !showroom_ && showGoalDebug_ ? &stage_.Goals() : nullptr, showStageColliders_ ? &level_.collision.colliders : nullptr);
#endif
#else
    (void)app;
#endif
}

void GameScene::DrawOverlay2D(GameApp&) {
    const auto view = Matrix4x4::MakeIdentity4x4();
    const auto projection = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight), 0.0f, 100.0f);
    if (!stage_.IsPlaying()) {
        clearOverlay_.Draw(view, projection);
        return;
    }
    crosshairHorizontal_.Update(view, projection);
    crosshairVertical_.Update(view, projection);
    crosshairHorizontal_.Draw();
    crosshairVertical_.Draw();
    // Refresh here so debug rewind, HP reset and weapon changes appear even while paused.
    gameHUD_.Update(player_, weapons_, static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight));
    gameHUD_.Draw(view, projection);
}

void GameScene::SyncProjectileVisuals(GameApp& app) {
    const auto& projectiles=enemyProjectiles_.projectiles;
    while (projectileVisuals_.size()<projectiles.size()) {
        auto visual=std::make_unique<Object3d>();
        visual->Initialize(app.ObjCom(),app.Dx()); visual->SetCamera(&camera_);
        visual->SetModel("cube/cube.obj"); visual->SetTexture("resources/white1x1.png");
        visual->SetEnableLighting(0); projectileVisuals_.push_back(std::move(visual));
    }
    projectileVisuals_.resize(projectiles.size());
    for (size_t i=0;i<projectiles.size();++i) {
        const auto& p=projectiles[i]; auto& visual=*projectileVisuals_[i];
        visual.SetTranslate(p.position); visual.SetScale({p.radius,p.radius,p.radius});
        visual.SetMaterialColor(p.type==EnemyProjectileType::Bomb ? Vector4{1,.3f,.05f,1} : Vector4{0,1,1,1});
        visual.Update(0);
    }
}

#ifdef _DEBUG
GameScene::DebugFrame GameScene::CaptureDebug() const {
    DebugFrame state;
    state.freezeEnemies=freezeEnemies_;
    state.player=player_.CaptureDebug();
    for (const auto& enemy : enemies_) state.enemies.push_back(enemy->CaptureDebug());
    state.projectiles=enemyProjectiles_; state.spawns=spawnSystem_; state.weapons=weapons_; state.stage=stage_;
    state.pelletRandom=pelletRandom_; state.nextEnemy=nextEnemyId_; state.frame=debugFrame_;
    state.shots=shotCount_; state.hits=hitCount_; state.attacks=enemyAttackCount_;
    state.selectedEnemy=selectedEnemy_; state.lastHitEnemy=lastHitEnemy_; state.lastHitPart=lastHitPart_;
    state.ads=adsBlend_; state.fov=camera_.GetFovY(); state.lastDamage=lastDamage_;
    state.lastEnemyDamage=lastEnemyDamage_; state.playerFlash=playerDamagedFlash_;
    return state;
}
void GameScene::RestoreDebug(GameApp& app,const DebugFrame& state) {
    freezeEnemies_=state.freezeEnemies;
    while (enemies_.size()>state.enemies.size()) enemies_.pop_back();
    while (enemies_.size()<state.enemies.size()) {
        auto enemy=std::make_unique<Enemy>(); enemy->Initialize(app.ObjCom(),app.Dx(),&camera_,true);
        enemies_.push_back(std::move(enemy));
    }
    for (size_t i=0;i<enemies_.size();++i) enemies_[i]->RestoreDebug(state.enemies[i]);
    player_.RestoreDebug(state.player); camera_.SetFovY(state.fov); camera_.Update();
    enemyProjectiles_=state.projectiles; spawnSystem_=state.spawns; weapons_=state.weapons; stage_=state.stage;
    pelletRandom_=state.pelletRandom; nextEnemyId_=state.nextEnemy; debugFrame_=state.frame;
    shotCount_=state.shots; hitCount_=state.hits; enemyAttackCount_=state.attacks;
    selectedEnemy_=state.selectedEnemy; lastHitEnemy_=state.lastHitEnemy; lastHitPart_=state.lastHitPart;
    adsBlend_=state.ads; lastDamage_=state.lastDamage; lastEnemyDamage_=state.lastEnemyDamage; playerDamagedFlash_=state.playerFlash;
    for (auto& enemy : enemies_) enemy->UpdateVisuals(0);
    for (auto& visual : weaponVisuals_) visual->Update(0);
    SyncProjectileVisuals(app);
    if (!stage_.IsPlaying()) clearOverlay_.SetResult(stage_,static_cast<float>(WinApp::kClientWidth),static_cast<float>(WinApp::kClientHeight));
    suppressFireUntilRelease_=true;
    SetWindowTextW(app.Win()->GetHwnd(),L"FPS Debug | Paused / Rewound");
}
#endif

#if defined(_DEBUG) && defined(USE_IMGUI)
namespace {
bool DrawJsonNumbers(nlohmann::json& value,const std::string& label) {
    bool changed=false;
    ImGui::PushID(label.c_str());
    if (value.is_object() || value.is_array()) {
        std::string title=label;
        if (value.is_object() && value.contains("id") && value["id"].is_string()) title+=" : "+value["id"].get<std::string>();
        if (ImGui::TreeNode(title.c_str())) {
            for (auto it=value.begin();it!=value.end();++it) {
                const auto key=value.is_object() ? it.key() : std::to_string(std::distance(value.begin(),it));
                changed=DrawJsonNumbers(it.value(),key)||changed;
            }
            ImGui::TreePop();
        }
    } else if (value.is_boolean()) {
        bool number=value.get<bool>();
        if (ImGui::Checkbox(label.c_str(),&number)) { value=number; changed=true; }
    } else if (value.is_number()) {
        auto number=value.get<double>();
        if (ImGui::InputDouble(label.c_str(),&number,0,0,"%.6f") && std::isfinite(number)) {
            DebugJsonEditor::SetNumber(value,number); changed=true;
        }
    } else if (value.is_string()) ImGui::TextWrapped("%s: %s",label.c_str(),value.get_ref<const std::string&>().c_str());
    ImGui::PopID();
    return changed;
}
}
void GameScene::DrawDebugTools(GameApp& app) {
    ImGui::Begin("FPS Debug Tools");
    if (ImGui::Button(debugPaused_ ? "Resume (F1)" : "Pause (F1)")) {
        debugPaused_=!debugPaused_; initialCapturePending_=!debugPaused_; suppressFireUntilRelease_=true;
        if (debugPaused_) {
            app.GetInput()->SetCameraControlEnabled(false);
            ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) | savedMouseFlags_;
        }
    }
    ImGui::Text("Frame: %llu | History: %zu / %zu",static_cast<unsigned long long>(debugFrame_),
        debugHistory_.Size() ? debugHistory_.Cursor()+1 : 0,debugHistory_.Size());
    ImGui::TextUnformatted("F1: pause/resume + mouse | ,: back | .: forward (1/60 sec at newest)");
    ImGui::BeginDisabled(!debugPaused_);
    if (ImGui::Button("Back (,)")) debugStep_=-1;
    ImGui::SameLine();
    if (ImGui::Button("Forward (.)")) debugStep_=1;
    ImGui::TextUnformatted("Last 300 frames. Resume after rewind starts a new branch.");
    ImGui::Separator();
    const std::string levelPath=LevelPath();
    const std::array<std::string,3> files{"resources/Data/enemies.json","resources/Data/weapons.json",levelPath};
    if (debugJson_.path.empty()) debugJson_.Open(files[static_cast<size_t>(debugJsonSelection_)]);
    ImGui::BeginDisabled(debugJson_.dirty);
    int selection=debugJsonSelection_;
    if (ImGui::Combo("JSON File",&selection,"Enemies\0Weapons\0Stage\0"))
        if (debugJson_.Open(files[static_cast<size_t>(selection)])) debugJsonSelection_=selection;
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s%s",debugJson_.path.c_str(),debugJson_.dirty ? " (unsaved)" : "");
    ImGui::TextWrapped("Numeric/boolean fields only. Save validates, backs up the file, then restarts paused. Blender Export overwrites Stage JSON edits.");
    if (ImGui::Button("Reload JSON / Discard edits")) debugJson_.Open(files[static_cast<size_t>(debugJsonSelection_)]);
    ImGui::SameLine();
    if (ImGui::Button("Validate, Save & Restart")) {
        const auto validate=[&](const std::string& temporary) -> std::string {
            const auto enemyPath=debugJsonSelection_==0 ? temporary : files[0];
            const auto weaponPath=debugJsonSelection_==1 ? temporary : files[1];
            const auto stagePath=debugJsonSelection_==2 ? temporary : files[2];
            EnemyDefinitions definitions; if (!definitions.Load(enemyPath)) return definitions.Error();
            EnemySpawnSystem spawns; if (!spawns.Load(stagePath,definitions)) return spawns.Error();
            WeaponSystem weapons; if (!weapons.Load(weaponPath,stagePath,weaponSeedOverride_)) return weapons.Error();
            StageProgress progress; if (!progress.LoadGoals(stagePath)) return progress.Error();
            if (stageLoaded_) {
                StageLoader level; if (!level.Load(stagePath) || !level.ValidateAssets()) return level.Error();
            }
            return {};
        };
        if (debugJson_.Save(validate)) { debugPauseOnEnter_=true; RequestChangeScene_(SceneName()); }
    }
    if (!debugJson_.error.empty()) ImGui::TextWrapped("Error: %s",debugJson_.error.c_str());
    if (ImGui::BeginChild("Numeric JSON",ImVec2(0,300),true))
        debugJson_.dirty=DrawJsonNumbers(debugJson_.document,"Document")||debugJson_.dirty;
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::End();
}
#elif defined(_DEBUG)
void GameScene::DrawDebugTools(GameApp&) {}
#endif

void GameScene::ResetShowroomEnemies(GameApp& app) {
    enemies_.clear(); enemyProjectiles_.Clear(); projectileVisuals_.clear();
    nextEnemyId_=0; selectedEnemy_=0; lastHitEnemy_=-1; lastHitPart_=EnemyPartType::None;
    enemyAttackCount_=0; playerDamagedFlash_=0; lastEnemyDamage_=0;
    player_.ResetHPForDebug();
    // Showroom points are placed immediately, independent of trigger activation/timing.
    for (const auto& point : spawnSystem_.Points()) {
        if (point.enemyPool.empty()) continue;
        const auto* definition=enemyDefinitions_.Find(point.enemyPool.front().id);
        if (!definition) continue;
        auto enemy=std::make_unique<Enemy>();
        enemy->SetSpawnIdentity(nextEnemyId_++,point.id);
        enemy->SetPosition(point.position); enemy->SetRotation(point.rotation);
        enemy->Initialize(app.ObjCom(),app.Dx(),&camera_,true);
        enemy->ApplyDefinition(*definition);
        enemies_.push_back(std::move(enemy));
    }
}
void GameScene::DrawShowroomTools(GameApp& app) {
#if defined(_DEBUG) && defined(USE_IMGUI)
    ImGui::Begin("Showroom");
    ImGui::TextUnformatted("F1: pause + mouse | ESC: mouse only | E: reusable pickup | R: reload | RMB: ADS");
    ImGui::TextUnformatted("AI starts frozen. Damage / fragments still work. Fired projectiles continue while AI is frozen.");
    if (!stageLoaded_) ImGui::TextUnformatted("Showroom assets failed to load. Check debugger output and restart.");
    ImGui::BeginDisabled(app.GetInput()->IsCameraControlEnabled());
    ImGui::Checkbox("Freeze Enemies",&freezeEnemies_);
    ImGui::SameLine();
    if (ImGui::Button("Enable Enemy AI")) freezeEnemies_=false;
    if (ImGui::Button("Reset Enemies")) resetEnemiesPending_=true;
    ImGui::SameLine();
    if (ImGui::Button("Reset Pickups")) weapons_.ResetPickups();
    if (ImGui::Button("Restart Scene")) RequestChangeScene_(SceneName());
    ImGui::SameLine();
    if (ImGui::Button("Return to Game##showroom")) RequestChangeScene_("Game");
    const auto teleport=[&](const char* label,const Vector3& position,const Vector3& rotation) {
        if (ImGui::Button(label)) {
            player_.SetStage(&level_.collision,position,rotation); player_.RefreshDebug();
            suppressFireUntilRelease_=true;
        }
    };
    teleport("Teleport to Start",level_.playerPosition,level_.playerRotation);
    if (!weapons_.Points().empty()) {
        const auto p=weapons_.Points().front().position;
        teleport("Teleport to Weapon Area",{p.x,0,p.z-2},{});
    }
    if (!spawnSystem_.Points().empty()) {
        const auto p=spawnSystem_.Points().front().position;
        teleport("Teleport to Enemy Area",{p.x,0,p.z-4},{});
        teleport("Teleport to Firing Line (15m)",{p.x,0,p.z-15},{});
    }
    ImGui::Checkbox("Show Enemy Labels",&showEnemyLabels_);
    ImGui::Checkbox("Show Enemy Markers",&showEnemyMarkers_);
    ImGui::Checkbox("Show Enemy Collision",&showEnemyCollision_);
    ImGui::Checkbox("Show Enemy Part Hit Boxes",&showEnemyParts_);
    ImGui::Checkbox("Show Stage Colliders##showroom",&showStageColliders_);
    ImGui::Checkbox("Show Weapon Pickup Labels",&showWeaponLabels_);
    ImGui::EndDisabled();
    const float eye=camera_.GetTranslate().y;
    ImGui::Text("Player eye Y: %.2f | Eye offset: %.2f",eye,player_.Settings().cameraHeight);
    ImGui::TextUnformatted("Enemy lineup (left to right). Head Y uses the actual transformed head part center.");
    for (const auto& enemy : enemies_) {
        const auto& d=enemy->Definition(); const auto v=d.visualScaleMultiplier;
        ImGui::Text("%s / %s (%s) | %s",enemy->GetId().c_str(),d.id.c_str(),EnemyTypeName(d.type),enemy->IsDead()?"Dead":"Alive");
        ImGui::Text("Visual Scale: %.2f %.2f %.2f | Collision Radius: %.2f | Collision Height: %.2f",
            v.x,v.y,v.z,d.collisionRadius,d.collisionHeight);
        const float head=enemy->HeadCenterForDebug().y;
        ImGui::Text("Head Y: %.2f | Head minus eye: %+.2f",head,head-eye);
    }
    ImGui::Separator(); ImGui::TextUnformatted("Weapon lineup: E nearby or use Equip Weapon (Debug) in Weapon System.");
    for (const auto& pickup : weapons_.Pickups()) {
        const auto& p=pickup.position;
        ImGui::Text("%s: (%.1f, %.1f, %.1f)",pickup.weaponId.c_str(),p.x,p.y,p.z);
        ImGui::PushID(pickup.spawnPointId.c_str());
        ImGui::BeginDisabled(app.GetInput()->IsCameraControlEnabled());
        if (ImGui::Button("Equip")) {
            if (const auto* d=weapons_.Find(pickup.weaponId)) player_.CurrentWeapon().Equip(*d);
            suppressFireUntilRelease_=true;
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    ImGui::End();
    RECT rect{};
    if (!app.ImGui()->GetSceneImageRect(rect)) return;
    const Vector2 lo{static_cast<float>(rect.left),static_cast<float>(rect.top)};
    const Vector2 hi{static_cast<float>(rect.right),static_cast<float>(rect.bottom)};
    const auto vp=camera_.GetViewProjectionMatrix();
    auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({lo.x,lo.y},{hi.x,hi.y},true);
    const auto label=[&](const Vector3& p,const std::string& text) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const float z=p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2];
        if (w<=1e-5f || z<0) return;
        const float x=p.x*vp.m[0][0]+p.y*vp.m[1][0]+p.z*vp.m[2][0]+vp.m[3][0];
        const float y=p.x*vp.m[0][1]+p.y*vp.m[1][1]+p.z*vp.m[2][1]+vp.m[3][1];
        const ImVec2 point{lo.x+(x/w+1)*.5f*(hi.x-lo.x),lo.y+(1-y/w)*.5f*(hi.y-lo.y)};
        const auto size=ImGui::CalcTextSize(text.c_str());
        draw->AddRectFilled({point.x-3,point.y-2},{point.x+size.x+3,point.y+size.y+2},IM_COL32(20,20,20,210));
        draw->AddText(point,IM_COL32(255,255,255,255),text.c_str());
    };
    if (showWeaponLabels_) for (const auto& p : weapons_.Pickups()) label(p.position+Vector3{0,.5f,0},p.weaponId);
    for (const auto& enemy : enemies_) {
        if (showEnemyLabels_) label(enemy->GetPosition()+Vector3{0,enemy->Definition().collisionHeight+.6f,0},
            enemy->Definition().id+" / "+enemy->GetId()+(enemy->IsDead()?" [Dead]":""));
        enemy->DrawPartDebug(vp,lo,hi,showEnemyParts_,showEnemyCollision_);
    }
    draw->PopClipRect();
#else
    (void)app;
#endif
}
