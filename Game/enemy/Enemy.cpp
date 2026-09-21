#include "Enemy.h"
#include "Raycast.h"
#include <nlohmann/json.hpp>
Enemy::~Enemy() {
    std::erase(fragmentOwners_, this);
}
void Enemy::MakeFragmentRoom() {
    size_t total = 0;
    Enemy* oldest = nullptr;
    for (auto* owner : fragmentOwners_) {
        total += owner->detachedParts_.size();
        if (!owner->detachedParts_.empty() && (!oldest ||
            owner->detachedParts_.front().spawnOrder < oldest->detachedParts_.front().spawnOrder)) oldest = owner;
    }
    if (total >= kMaxFragments && oldest) oldest->detachedParts_.erase(oldest->detachedParts_.begin());
}
void Enemy::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera, bool useSplitAssets) {
    ai_ = {};
    attackCount_ = 0; lastAttackDamage_ = 0; attackFlash_ = 0;
    detachedParts_.clear();
    faceShards_.clear();
    for (auto& faces : faceData_) faces.clear();
    if (std::find(fragmentOwners_.begin(), fragmentOwners_.end(), this) == fragmentOwners_.end()) fragmentOwners_.push_back(this);
    common_ = common; dx_ = dx; camera_ = camera;
    for (auto& files : fragmentFiles_) files.clear();
    object_.Initialize(common, dx);
    object_.SetCamera(camera);
    object_.SetModel("enemy/boss/boss.gltf");
    object_.StopAnimation();
    object_.SetRotate(rotation_);
    object_.SetScale({2.0f, 2.0f, 2.0f});
    object_.SetTranslate(position_);
    object_.SetEnableLighting(1);
    object_.SetDirection({0.3f, -1.0f, 0.5f});
    object_.SetIntensity(1.0f);
    object_.SetPointLightIntensity(0.0f);
    object_.SetSpotLightIntensity(0.0f);
    AABB bounds{};
    hasHitBox_ = object_.GetModel() && object_.GetModel()->GetLocalAABB(bounds);
    if (hasHitBox_) parts_ = MakeEnemyParts(bounds);
    // Opt-in: exported static parts must share the original Boss local coordinates.
    const std::array<const char*, 6> files{{
        "enemy/boss/parts/boss_head.gltf", "enemy/boss/parts/boss_body.gltf",
        "enemy/boss/parts/boss_left_arm.gltf", "enemy/boss/parts/boss_right_arm.gltf",
        "enemy/boss/parts/boss_left_leg.gltf", "enemy/boss/parts/boss_right_leg.gltf"}};
    splitVisuals_ = useSplitAssets && hasHitBox_;
    for (const auto* file : files) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::path("resources") / file, error))
            splitVisuals_ = false;
    }
    for (size_t i = 0; i < visuals_.size(); ++i) {
        auto& visual = visuals_[i];
        visual.type = parts_[i].type;
        visual.visible = true;
        visual.object.reset();
        if (!splitVisuals_) continue;
        visual.object = std::make_unique<Object3d>();
        auto& obj = *visual.object;
        obj.Initialize(common, dx);
        obj.SetCamera(camera);
        obj.SetModel(files[i]);
        obj.StopAnimation();
        obj.SetEnableLighting(1);
        obj.SetDirection({0.3f, -1.0f, 0.5f});
        obj.SetIntensity(1.0f);
        obj.SetPointLightIntensity(0.0f);
        obj.SetSpotLightIntensity(0.0f);
    }
    // Missing manifest, glTF or external buffer disables fragments only for that part.
    const std::array<const char*,6> keys{{"head","body","left_arm","right_arm","left_leg","right_leg"}};
    if (splitVisuals_) {
        try {
            std::ifstream input("resources/enemy/boss/fragments/manifest.json");
            const auto manifest = nlohmann::json::parse(input);
            for (size_t i = 0; i < keys.size(); ++i) {
                try {
                    auto files = manifest.at(keys[i]).get<std::vector<std::string>>();
                    bool valid = !files.empty() && files.size() <= 32;
                    for (const auto& file : files) {
                        const auto path = std::filesystem::path("resources") / file;
                        std::ifstream asset(path);
                        const auto data = nlohmann::json::parse(asset);
                        valid = valid && data.contains("meshes") && !data["meshes"].empty();
                        for (const auto& buffer : data.at("buffers")) {
                            const auto uri = buffer.at("uri").get<std::string>();
                            valid = valid && (uri.starts_with("data:") || std::filesystem::is_regular_file(path.parent_path()/uri));
                        }
                    }
                    if (valid) {
                        for (const auto& file : files) ModelManager::GetInstance()->LoadModel(file);
                        fragmentFiles_[i] = std::move(files);
                    }
                } catch (const std::exception&) { /* This part keeps its whole-part fallback. */ }
            }
        } catch (const std::exception&) { /* All parts keep their whole-part fallback. */ }
    }
    if (splitVisuals_) {
        try {
            std::ifstream input("resources/enemy/boss/faces/faces.json");
            const auto data = nlohmann::json::parse(input);
            if (data.at("version") != 1 || data.at("coordinate_system") != "gltf") throw std::runtime_error("Face format");
            for (size_t i=0; i<keys.size(); ++i) {
                try {
                    std::vector<std::array<Vector3,3>> faces;
                    const auto& list = data.at("parts").at(keys[i]);
                    if (list.empty() || list.size()>10000) continue;
                    for (const auto& triangle : list) {
                        if (triangle.size()!=3) throw std::runtime_error("Face size");
                        std::array<Vector3,3> vertices{};
                        for (size_t v=0;v<3;++v) {
                            if (triangle[v].size()!=3) throw std::runtime_error("Vertex size");
                            vertices[v]={-triangle[v][0].get<float>(),triangle[v][1].get<float>(),triangle[v][2].get<float>()};
                            if (!std::isfinite(vertices[v].x)||!std::isfinite(vertices[v].y)||!std::isfinite(vertices[v].z)) throw std::runtime_error("Vertex finite");
                        }
                        faces.push_back(vertices);
                    }
                    faceData_[i]=std::move(faces);
                } catch (const std::exception&) { }
            }
        } catch (const std::exception&) { /* Face -> Chunk -> whole part. */ }
        Model::ModelData geometry;
        geometry.materials.push_back({"resources/white1x1.png"});
        Model::MeshData mesh;
        mesh.vertices.resize(kFaceCapacity*6);
        for (auto& vertex : mesh.vertices) vertex={{0,0,0,1},{0,0},{0,1,0}};
        mesh.indexCount=static_cast<uint32_t>(mesh.vertices.size());
        geometry.indices.resize(mesh.vertices.size());
        for (uint32_t v=0;v<geometry.indices.size();++v) geometry.indices[v]=v;
        geometry.meshes.push_back(std::move(mesh));
        geometry.rootNode.meshIndices.push_back(0);
        faceModelCommon_.Initialize(dx);
        faceModel_=std::make_unique<Model>();
        faceModel_->InitializeFromModelData(&faceModelCommon_,geometry);
        faceBatch_=std::make_unique<Object3d>();
        faceBatch_->Initialize(common,dx);
        faceBatch_->SetCamera(camera);
        faceBatch_->SetModel(faceModel_.get());
        faceBatch_->SetEnableLighting(0);
        faceBatch_->SetMaterialColor({.65f,.025f,.025f,1});
        faceBatch_->Update(0);
    }
    if (useSplitAssets && !splitVisuals_)
        OutputDebugStringA("Enemy: split assets incomplete; using original Boss.\n");
}


bool Enemy::Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hit) const {
    hit = {};
    return hasHitBox_ && RaycastEnemyParts(parts_,object_.GetWorldMatrix(),origin,direction,maxDistance,hit);
}
float Enemy::ApplyDamage(EnemyPartType type, float damage, const Vector3& shotDirection) {
    const float lost = DamageEnemyPart(parts_, type, damage);
    if (lost <= 0 || !splitVisuals_) return lost;
    for (size_t i = 0; i < parts_.size(); ++i) {
        if (parts_[i].type != type || parts_[i].DamageState() != EnemyPartDamageState::Destroyed) continue;
        auto& visual = visuals_[i];
        if (!visual.object) break;
        if (breakMode_ == FragmentMode::Face && SpawnFaces(i, shotDirection)) { visual.object.reset(); break; }
        AABB bounds{};
        if (!visual.object->GetModel()->GetLocalAABB(bounds)) break;
        std::uniform_real_distribution<float> magnitude(2.0f,6.0f);
        std::uniform_real_distribution<float> jitter(-1.0f,1.0f);
        std::bernoulli_distribution sign;
        const auto spin = [&]() { return magnitude(random_)*(sign(random_) ? 1.0f : -1.0f); };
        const auto translation = visual.object->GetTranslate();
        const auto rotation = visual.object->GetRotate();
        const auto scale = visual.object->GetScale();
        const auto center = EnemyPartTransformPoint((bounds.min+bounds.max)*.5f,
            Matrix4x4::MakeAffineMatrix(scale,rotation,translation));
        const bool fragments = !fragmentFiles_[i].empty();
        const size_t count = fragments ? fragmentFiles_[i].size() : 1;
        for (size_t j = 0; j < count; ++j) {
            DetachedEnemyFragment detached;
            AABB pieceBounds = bounds;
            if (fragments) {
                detached.object = std::make_unique<Object3d>();
                detached.object->Initialize(common_, dx_);
                detached.object->SetCamera(camera_);
                detached.object->SetModel(fragmentFiles_[i][j]);
                detached.object->StopAnimation();
                detached.object->GetModel()->GetLocalAABB(pieceBounds);
                detached.object->SetEnableLighting(1);
                detached.object->SetDirection({.3f,-1,.5f});
                detached.object->SetIntensity(1);
                detached.object->SetPointLightIntensity(0);
                detached.object->SetSpotLightIntensity(0);
            } else {
                detached.object = std::move(visual.object);
            }
            detached.motion.Initialize(pieceBounds, translation, rotation, scale, shotDirection,
                type, {spin(),spin(),spin()}, detachedSettings_);
            if (fragments) {
                const auto outward = detached.motion.position-center;
                const float length = std::hypot(outward.x,outward.y,outward.z);
                detached.motion.velocity = detached.motion.velocity + Vector3{jitter(random_),jitter(random_),jitter(random_)}*spreadPower_;
                if (length > 1e-5f) detached.motion.velocity = detached.motion.velocity + outward*(outwardPower_/length);
            }
            detached.object->SetTranslate(translation);
            detached.object->SetRotate(rotation);
            detached.object->SetScale(scale);
            detached.object->SetMaterialColor({.59f,.06f,.06f,1});
            detached.object->Update(0);
            MakeFragmentRoom();
            detached.spawnOrder = nextSpawnOrder_++;
            detachedParts_.push_back(std::move(detached));
        }
        if (fragments) visual.object.reset();
        break;
    }
    return lost;
}
void Enemy::ShowHitFeedback(EnemyPartType type) {
    for (auto& part:parts_) if (part.type==type) part.flashRemaining=0.2f;
}
float Enemy::Update(float dt, const Vector3& playerPosition) {
    const float attackDamage = ai_.Update(position_, rotation_, playerPosition, dt, IsDead());
    UpdateVisuals(dt);
    return attackDamage;
}
void Enemy::UpdateVisuals(float dt) {
    attackFlash_ = std::max(0.0f, attackFlash_-dt);
    for (auto& face : faceShards_) face.motion.Update(dt);
    std::erase_if(faceShards_, [](const auto& face) { return !face.motion.Active(); });
    TrimFacePool(0);
    for (auto& detached : detachedParts_) {
        detached.motion.Update(dt);
        detached.object->SetTranslate(detached.motion.Translation());
        detached.object->SetRotate(detached.motion.rotation);
        detached.object->Update(dt);
    }
    std::erase_if(detachedParts_, [](const auto& part) { return !part.motion.Active(); });
    for (auto& part:parts_) part.flashRemaining=std::max(0.0f,part.flashRemaining-std::max(0.0f,dt));
    object_.SetTranslate(position_);
    object_.SetRotate(rotation_);
    object_.SetScale(scale_);
    object_.Update(dt);
    for (size_t i = 0; i < visuals_.size(); ++i) {
        if (!visuals_[i].object) continue;
        auto& obj = *visuals_[i].object;
        obj.SetTranslate(position_);
        obj.SetRotate(rotation_);
        obj.SetScale(scale_);
        Vector4 color{1,1,1,1};
        switch (parts_[i].DamageState()) {
        case EnemyPartDamageState::LightDamage: color = {1,.67f,.67f,1}; break;
        case EnemyPartDamageState::HeavyDamage: color = {1,.18f,.18f,1}; break;
        case EnemyPartDamageState::Critical: color = {.59f,.06f,.06f,1}; break;
        default: break;
        }
        obj.SetMaterialColor(color);
        obj.Update(dt);
    }
}

void Enemy::SetPartVisible(EnemyPartType type, bool visible) {
    for (auto& visual : visuals_) if (visual.type == type) visual.visible = visible;
}
void Enemy::Draw() {
    DrawFaces();
    for (auto& detached : detachedParts_) detached.object->Draw();
    if (!splitVisuals_) { object_.Draw(); return; }
    for (size_t i = 0; i < visuals_.size(); ++i) {
        const auto& visual = visuals_[i];
        if (visual.object && visual.visible && parts_[i].DamageState() != EnemyPartDamageState::Destroyed)
            visual.object->Draw();
    }
}

#ifdef USE_IMGUI
#include "imgui.h"
#endif
void Enemy::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text("Enemy ID: %s | Definition ID: %s | Type: %s", id_.c_str(), definition_.id.c_str(), EnemyTypeName(definition_.type));
    ImGui::Text("HP Multiplier: %.2f", definition_.hpMultiplier);
    if (definition_.IsRanged()) {
        ImGui::Text("Min Range: %.2f | Preferred Range: %.2f | Max Range: %.2f", definition_.minRange, definition_.preferredRange, definition_.maxRange);
        ImGui::Text("Projectile Speed: %.2f", definition_.projectileSpeed);
    }
    if (definition_.type==EnemyType::Bomber)
        ImGui::Text("Explosion Radius: %.2f | Explosion Damage: %.2f | Fuse Time: %.2f", definition_.explosionRadius, definition_.explosionDamage, definition_.fuseTime);
    ImGui::Text("Enemy State: %s | Distance: %.2f", EnemyStateName(GetState()), ai_.distance);
    ImGui::Text("Last Enemy Attack: %s | Attack Count: %llu", attackFlash_ > 0 ? "HIT" : "-", attackCount_);
    ImGui::Text("Last applied damage: %.0f | Attack Cooldown: %.2f", lastAttackDamage_, ai_.cooldown);
    ImGui::SliderFloat("Detection Range", &ai_.settings.detectionRange, 1, 50);
    ImGui::SliderFloat("Attack Range", &ai_.settings.attackRange, .2f, 30);
    ImGui::SliderFloat("Enemy Move Speed", &ai_.settings.moveSpeed, 0, 10);
    ImGui::SliderFloat("Attack Damage", &ai_.settings.attackDamage, 0, 50);
    ImGui::SliderFloat("Attack Interval", &ai_.settings.attackInterval, .1f, 5);
    ImGui::TextUnformatted(splitVisuals_ ? "Visuals: six separate assets" : "Visuals: original Boss (parts unavailable)");
    if (splitVisuals_) {
        for (auto& visual : visuals_) {
            ImGui::PushID(static_cast<int>(visual.type));
            ImGui::Checkbox(EnemyPartName(visual.type), &visual.visible);
            ImGui::PopID();
        }
    }
    int mode=static_cast<int>(breakMode_);
    if (ImGui::Combo("Break Mode", &mode, "Chunk\0Face\0")) breakMode_=static_cast<FragmentMode>(mode);
    ImGui::SliderInt("Max Active Face Shards", &maxActiveFaces_, 1, static_cast<int>(kFaceCapacity));
    ImGui::SliderInt("Max Face Shards Per Break", &maxFacesPerBreak_, 1, static_cast<int>(kFaceCapacity));
    ImGui::SliderFloat("Face Shard Lifetime", &faceLifetime_, .1f, 15);
    ImGui::Text("Active Faces (this Enemy): %zu", faceShards_.size());
    if (ImGui::TreeNode("Detached Part")) {
        ImGui::Text("Active: %zu (settings apply to new parts)", detachedParts_.size());
        ImGui::Text("Max active across all Enemies: %zu", kMaxFragments);
        ImGui::SliderFloat("Fragment Spread", &spreadPower_, 0, 6);
        ImGui::SliderFloat("Fragment Outward", &outwardPower_, 0, 6);
        ImGui::SliderFloat("Launch Power", &detachedSettings_.launchPower, 0, 20);
        ImGui::SliderFloat("Upward Power", &detachedSettings_.upwardPower, 0, 10);
        ImGui::SliderFloat("Gravity", &detachedSettings_.gravity, -30, -1);
        ImGui::SliderFloat("Life Time", &detachedSettings_.lifeTime, .1f, 15);
        ImGui::SliderFloat("Bounce", &detachedSettings_.bounce, 0, .8f);
        ImGui::SliderFloat("Angular Velocity Scale", &detachedSettings_.angularVelocityScale, 0, 3);
        ImGui::TreePop();
    }
    ImGui::TextUnformatted("Enemy Parts");
    for (const auto& part : parts_) {
        ImGui::Text("%-8s %.0f / %.0f | %s | Damage %.0f%%", EnemyPartName(part.type),
            part.hp, part.maxHp, EnemyPartDamageStateName(part.DamageState()), part.DamageRate() * 100.0f);
    }
    ImGui::Checkbox("Show Enemy Part Colliders", &showPartColliders_);
    if (ImGui::TreeNode("Enemy Transform (part alignment test)")) {
        ImGui::DragFloat3("Enemy Position",&position_.x,0.05f);
        ImGui::DragFloat3("Enemy Rotation (radians)",&rotation_.x,0.01f);
        ImGui::SliderFloat3("Enemy Scale",&scale_.x,0.1f,5.0f);
        ImGui::TextUnformatted("Left/Right are the enemy's own sides. Wireframe color shows damage; hit flashes thicker.");
        ImGui::TreePop();
    }
#endif
}
void Enemy::DrawPartDebug(const Matrix4x4& vp,const Vector2& screenMin,const Vector2& screenMax) const {
#ifdef USE_IMGUI
    if (!hasHitBox_) return;
    auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({screenMin.x,screenMin.y},{screenMax.x,screenMax.y},true);
    const auto matrix=Matrix4x4::Multiply(object_.GetWorldMatrix(),vp);
    for (const auto& part:parts_) {
        if (part.DamageState() == EnemyPartDamageState::Destroyed) continue;
        if (!showPartColliders_ && part.flashRemaining<=0) continue;
        struct Clip { float x,y,z,w; } corners[8];
        for (int i=0;i<8;++i) {
            const Vector3 p{(i&1)?part.bounds.max.x:part.bounds.min.x,
                (i&2)?part.bounds.max.y:part.bounds.min.y,(i&4)?part.bounds.max.z:part.bounds.min.z};
            corners[i]={p.x*matrix.m[0][0]+p.y*matrix.m[1][0]+p.z*matrix.m[2][0]+matrix.m[3][0],
                p.x*matrix.m[0][1]+p.y*matrix.m[1][1]+p.z*matrix.m[2][1]+matrix.m[3][1],
                p.x*matrix.m[0][2]+p.y*matrix.m[1][2]+p.z*matrix.m[2][2]+matrix.m[3][2],
                p.x*matrix.m[0][3]+p.y*matrix.m[1][3]+p.z*matrix.m[2][3]+matrix.m[3][3]};
        }
        ImU32 color = IM_COL32(255,255,255,255);
        switch (part.DamageState()) {
        case EnemyPartDamageState::LightDamage: color = IM_COL32(255,170,170,255); break;
        case EnemyPartDamageState::HeavyDamage: color = IM_COL32(255,45,45,255); break;
        case EnemyPartDamageState::Critical: color = IM_COL32(150,15,15,255); break;
        case EnemyPartDamageState::Destroyed: color = IM_COL32(160,80,255,255); break;
        default: break;
        }
        auto project=[&](Clip p) {return ImVec2{screenMin.x+(p.x/p.w+1)*.5f*(screenMax.x-screenMin.x),
            screenMin.y+(1-p.y/p.w)*.5f*(screenMax.y-screenMin.y)};};
        for(int i=0;i<8;++i) for(int bit:{1,2,4}) {
            if (i&bit) continue;
            Clip a=corners[i], b=corners[i|bit];
            // Clip crossing edges against the near plane before perspective division.
            if (a.z<0 && b.z<0) continue;
            if ((a.z<0)!=(b.z<0)) {
                const float t=a.z/(a.z-b.z);
                const Clip intersection{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,a.w+(b.w-a.w)*t};
                if(a.z<0)a=intersection;else b=intersection;
            }
            if(a.w>1e-5f && b.w>1e-5f) draw->AddLine(project(a),project(b),color,part.flashRemaining>0?3.0f:1.0f);
        }
        if(showPartColliders_ && corners[7].z>=0 && corners[7].w>1e-5f)
            draw->AddText(project(corners[7]),color,EnemyPartName(part.type));
    }
    draw->PopClipRect();
#else
    (void)vp; (void)screenMin; (void)screenMax;
#endif
}

void Enemy::TrimFacePool(size_t reserve) {
    while (true) {
        size_t total=reserve;
        Enemy* oldest=nullptr;
        for (auto* owner : fragmentOwners_) {
            total+=owner->faceShards_.size();
            if (!owner->faceShards_.empty() && (!oldest || owner->faceShards_.front().spawnOrder<oldest->faceShards_.front().spawnOrder)) oldest=owner;
        }
        if (total<=static_cast<size_t>(maxActiveFaces_) || !oldest) break;
        oldest->faceShards_.erase(oldest->faceShards_.begin());
    }
}
bool Enemy::SpawnFaces(size_t part, const Vector3& direction) {
    const auto& source=faceData_[part];
    if (source.empty() || !faceBatch_) return false;
    const auto& visual=*visuals_[part].object;
    const auto world=Matrix4x4::MakeAffineMatrix(visual.GetScale(),visual.GetRotate(),visual.GetTranslate());
    AABB partBounds{};
    if (!visual.GetModel()->GetLocalAABB(partBounds)) return false;
    const auto partCenter=EnemyPartTransformPoint((partBounds.min+partBounds.max)*.5f,world);
    const size_t count=std::min({source.size(),static_cast<size_t>(maxFacesPerBreak_),static_cast<size_t>(maxActiveFaces_)});
    std::uniform_real_distribution<float> random(-1,1);
    std::uniform_real_distribution<float> spin(2,6);
    auto config=detachedSettings_; config.lifeTime=faceLifetime_;
    for (size_t i=0;i<count;++i) {
        FaceShard shard;
        const auto& triangle=source[i*source.size()/count]; // even selection, no duplicates
        std::array<Vector3,3> points;
        for (size_t v=0;v<3;++v) points[v]=EnemyPartTransformPoint(triangle[v],world);
        const auto center=(points[0]+points[1]+points[2])*(1.0f/3);
        AABB bounds{points[0]-center,points[0]-center};
        for (size_t v=0;v<3;++v) {
            shard.vertices[v]=points[v]-center;
            const auto& p=shard.vertices[v];
            bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
            bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};
        }
        const auto angular=[&]() {return spin(random_)*(random(random_)<0?-1.0f:1.0f);};
        shard.motion.Initialize(bounds,center,{}, {1,1,1},direction,parts_[part].type,{angular(),angular(),angular()},config);
        shard.motion.pivot={}; shard.motion.position=center;
        const auto outward=center-partCenter;
        const float length=std::hypot(outward.x,outward.y,outward.z);
        shard.motion.velocity=shard.motion.velocity+Vector3{random(random_),random(random_),random(random_)}*spreadPower_;
        if (length>1e-5f) shard.motion.velocity=shard.motion.velocity+outward*(outwardPower_/length);
        TrimFacePool(1);
        shard.spawnOrder=nextSpawnOrder_++;
        faceShards_.push_back(shard);
    }
    return true;
}
void Enemy::DrawFaces() {
    if (faceShards_.empty() || !faceBatch_) return;
    uint32_t index=0;
    for (const auto& shard : faceShards_) {
        const auto matrix=Matrix4x4::MakeAffineMatrix({1,1,1},shard.motion.rotation,shard.motion.position);
        for (const int v : {0,1,2,2,1,0}) faceModel_->UpdateVertexPosition(index++,EnemyPartTransformPoint(shard.vertices[v],matrix));
    }
    for (;index<kFaceCapacity*6;++index) faceModel_->UpdateVertexPosition(index,{});
    // Vertices are world-space, but WVP must follow the current FPS camera every frame.
    faceBatch_->Update(0);
    faceBatch_->Draw();
}
