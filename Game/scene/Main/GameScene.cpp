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

    if (!weapons_.Load("resources/Data/weapons.json","resources/levels/fps_spawns.json",weaponSeedOverride_))
        OutputDebugStringA(("Weapon configuration error: " + weapons_.Error() + "\n").c_str());
    if (const auto* initial = weapons_.InitialWeapon()) player_.CurrentWeapon().Equip(*initial);
    pelletRandom_.seed(weapons_.ActualSeed() ^ 0x9e3779b9u); // independent of placement lottery
    weaponHUD_.Initialize(app.SpriteCom(),app.Dx());
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
    enemyDefinitions_=EnemyDefinitions{};
    const bool enemiesLoaded=enemyDefinitions_.Load("resources/Data/enemies.json");
    if (!enemiesLoaded) OutputDebugStringA(("Enemy configuration error: " + enemyDefinitions_.Error() + "\n").c_str());
    spawnSystem_ = EnemySpawnSystem{};
    if (enemiesLoaded && !spawnSystem_.Load("resources/levels/fps_spawns.json",enemyDefinitions_))
        OutputDebugStringA(("Spawn configuration error: " + spawnSystem_.Error() + "\n").c_str());
    if (!stage_.LoadGoals("resources/levels/fps_spawns.json"))
        OutputDebugStringA(("Goal configuration error: " + stage_.Error() + "\n").c_str());
    clearOverlay_.Initialize(app.SpriteCom(), app.Dx());
    selectedEnemy_ = 0; lastHitEnemy_ = -1;
    enemyAttackCount_ = 0; playerDamagedFlash_ = 0; lastEnemyDamage_ = 0;
    ground_.Initialize(app.ObjCom(), app.Dx());
    ground_.SetCamera(&camera_);
    ground_.SetModel("cube/cube.obj");
    ground_.SetTexture("resources/white1x1.png");
    ground_.SetScale({32.0f, 0.25f, 60.0f});
    ground_.SetTranslate({0.0f, -0.25f, 24.0f});
    ground_.SetMaterialColor({0.45f, 0.48f, 0.52f, 1.0f});
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
    Update(app, 0.0f);
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
    if (!stage_.IsPlaying() || !viewReady || input.IsKeyTrigger(DIK_ESCAPE)) {
        input.SetCameraControlEnabled(false);
        if (input.IsKeyTrigger(DIK_ESCAPE)) initialCapturePending_ = false;
    }
    else if (initialCapturePending_ || clickedView) {

        input.SetCameraControlEnabled(true);

        if (clickedView && !wasCaptured) {
            suppressFireUntilRelease_ = true;
        }

        initialCapturePending_ = false;
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

        if (stage_.Update(
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
    if (!allowFire) weapon.CancelBurst();
    const int weaponShots = weapon.Step(pickedUp ? 0.0f : dt,
        allowFire && input.IsLeftMouseTrigger(), allowFire && input.IsLeftMousePressed(),
        controls && input.IsKeyTrigger(DIK_R));
    spawnSystem_.Update(dt, player_.GetTransform().translate,
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
    const float projectileDamage=enemyProjectiles_.Update(dt,player_.GetTransform().translate);
    if (projectileDamage>0) {
        const float before=player_.GetHP(); player_.ApplyDamage(projectileDamage);
        lastEnemyDamage_=before-player_.GetHP(); playerDamagedFlash_=.35f;
    }
    // Symmetric XZ separation from a snapshot; dead bodies do not push living enemies.
    std::vector<Vector3> correction(enemies_.size());
    for (size_t i=0;i<enemies_.size();++i) for(size_t j=i+1;j<enemies_.size();++j) {
        if (enemies_[i]->IsDead() || enemies_[j]->IsDead()) continue;
        auto delta=enemies_[i]->GetPosition()-enemies_[j]->GetPosition(); delta.y=0;
        const float distance=std::hypot(delta.x,delta.z);
        if (distance>=1.1f) continue;
        const auto direction=distance>1e-5f ? delta*(1.0f/distance) : Vector3{1,0,0};
        const auto offset=direction*(std::min((1.1f-distance)*.5f,std::max(dt,0.0f)*.6f));
        correction[i]=correction[i]+offset; correction[j]=correction[j]-offset;
    }
    std::vector<float> enemyDamage(enemies_.size());
    for(size_t i=0;i<enemies_.size();++i) {
        enemies_[i]->SetPosition(enemies_[i]->GetPosition()+correction[i]);
        enemyDamage[i]=enemies_[i]->Update(dt,player_.GetTransform().translate);
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
        if (enemies_[i]->IsDead()) continue;
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
    for(auto& enemy : enemies_) enemy->Draw();
    for(auto& visual : projectileVisuals_) visual->Draw();
}


void GameScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("FPS Controls");
    if (!spawnSystem_.Error().empty()) ImGui::TextWrapped("Spawn configuration error: %s", spawnSystem_.Error().c_str());
    const bool captured = app.GetInput()->IsCameraControlEnabled();
    ImGui::TextUnformatted(!stage_.IsPlaying() ? "Stage cleared. Gameplay stopped; debug controls remain available." :
        captured ? "WASD: walk | Mouse: look | LMB: fire | ESC: release" : "Click the Scene image to resume FPS controls.");
    ImGui::Text("Shot Count: %llu | Hit Count: %llu", shotCount_, hitCount_);
    ImGui::Text("Last Hit Enemy: %d | Last Hit Part: %s", lastHitEnemy_, EnemyPartName(lastHitPart_));
    ImGui::Text("Last Damage: %.0f (actual HP lost)", lastDamage_);
    ImGui::Text("Player HP: %.0f / 100 %s", player_.GetHP(), player_.IsDead() ? "Player Dead" : "");
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
    if(!enemies_.empty() && app.ImGui()->GetSceneImageRect(sceneRect)) enemies_[static_cast<size_t>(selectedEnemy_)]->DrawPartDebug(camera_.GetViewProjectionMatrix(),
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
    ImGui::End();
    if ((showSpawnDebug_ || showGoalDebug_) && app.ImGui()->GetSceneImageRect(sceneRect))
        DrawEnemySpawnDebug(spawnSystem_, camera_.GetViewProjectionMatrix(),
            {static_cast<float>(sceneRect.left),static_cast<float>(sceneRect.top)},
            {static_cast<float>(sceneRect.right),static_cast<float>(sceneRect.bottom)},
            showSpawnDebug_, showGoalDebug_ ? &stage_.Goals() : nullptr);
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
    const auto nearest = weapons_.Nearest(player_.GetTransform().translate);
    const auto* nearby = nearest ? weapons_.Find(weapons_.Pickups()[*nearest].weaponId) : nullptr;
    weaponHUD_.Draw(player_.CurrentWeapon(),nearby,!weapons_.Error().empty(),
        static_cast<float>(WinApp::kClientWidth),static_cast<float>(WinApp::kClientHeight),view,projection);
}
