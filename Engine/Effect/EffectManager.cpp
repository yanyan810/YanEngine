#include "EffectManager.h"
#include "ParticleManager.h"
#include "Object3d.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "GeometryGenerator.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

namespace {
constexpr const char* kGeometryNames[] = {
    "Ring",
    "Sphere",
    "Box",
    "Plane",
    "Torus",
    "Cylinder",
    "Cone",
    "Triangle",
    "Capsule",
    "Star",
    "Diamond",
};
constexpr int kGeometryCount = static_cast<int>(sizeof(kGeometryNames) / sizeof(kGeometryNames[0]));

std::vector<Model::VertexData> MakeEffectGeometryVertices(int typeIndex)
{
    switch (typeIndex) {
    case 0: return GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f);
    case 1: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    case 2: return GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f);
    case 3: return GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f);
    case 4: return GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f);
    case 5: return GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f);
    case 6: return GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f);
    case 7: return GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f);
    case 8: return GeometryGenerator::GenerateCapsuleTriList(32, 8, 0.65f, 1.4f);
    case 9: return GeometryGenerator::GenerateStarTriListXY(1.1f, 0.48f, 5);
    case 10: return GeometryGenerator::GenerateDiamondTriListXY(1.6f, 2.2f);
    default: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    }
}

Model::ModelData MakeEffectGeometryModelData(const std::vector<Model::VertexData>& vertices)
{
    Model::ModelData modelData{};
    modelData.materials.push_back({ "" });

    Model::MeshData mesh{};
    mesh.materialIndex = 0;
    mesh.vertices = vertices;
    mesh.skinned = false;
    mesh.startVertex = 0;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.startIndex = 0;
    mesh.indexCount = static_cast<uint32_t>(vertices.size());
    modelData.meshes.push_back(std::move(mesh));

    modelData.indices.resize(vertices.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
        modelData.indices[i] = i;
    }

    modelData.rootNode.name = "EffectGeometryRoot";
    modelData.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
    modelData.rootNode.meshIndices.push_back(0);
    return modelData;
}

Model* GetOrCreateEffectGeometryModel(int typeIndex)
{
    typeIndex = std::clamp(typeIndex, 0, kGeometryCount - 1);
    const std::string key = "EffectGeometry_" + std::to_string(typeIndex);
    if (Model* model = ModelManager::GetInstance()->FindModel(key)) {
        return model;
    }
    auto vertices = MakeEffectGeometryVertices(typeIndex);
    auto modelData = MakeEffectGeometryModelData(vertices);
    return ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
}

Vector3 EffectLerp(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

Vector4 EffectLerp(const Vector4& a, const Vector4& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    };
}
}

EffectManager* EffectManager::GetInstance() {
    static EffectManager instance;
    return &instance;
}

void EffectManager::Initialize() {
    templates_.clear();
    activeEffects_.clear();
    objCommon_ = nullptr;
    dxCommon_ = nullptr;
    camera_ = nullptr;
}

void EffectManager::Finalize() {
    templates_.clear();
    activeEffects_.clear();
    objCommon_ = nullptr;
    dxCommon_ = nullptr;
    camera_ = nullptr;
}

void EffectManager::SetGraphicsResources(Object3dCommon* objCommon, DirectXCommon* dxCommon, Camera* camera) {
    objCommon_ = objCommon;
    dxCommon_ = dxCommon;
    camera_ = camera;
}

void EffectManager::LoadEffect(const std::string& effectName, const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;

    EffectTemplate temp;
    temp.name = effectName;
    temp.duration = root.value("timeline", json::object()).value("duration", 1.0f);

    for (const auto& nodeSource : root.value("particleNodes", json::array())) {
        EffectParticleNode node;
        node.name = nodeSource.value("name", "ParticleNode");
        if (nodeSource.contains("particleFileName")) {
            node.particleFileName = nodeSource.value("particleFileName", "");
        } else {
            node.particleFileName = nodeSource.value("particleGroup", "");
        }
        node.startTime = nodeSource.value("startTime", 0.0f);
        node.endTime = nodeSource.value("endTime", 1.0f);
        auto p = nodeSource.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = nodeSource.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = nodeSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        node.position = { p[0], p[1], p[2] };
        node.rotation = { r[0], r[1], r[2] };
        node.scale = { s[0], s[1], s[2] };
        node.emitCount = nodeSource.value("emitCount", 10);
        node.presetDuration = nodeSource.value("presetDuration", 1.0f);
        temp.particleNodes.push_back(std::move(node));
    }

    for (const auto& objSource : root.value("objects", json::array())) {
        EffectObjectNode node;
        node.id = objSource.value("id", 0);
        node.name = objSource.value("name", "");
        node.modelPath = objSource.value("modelPath", "");
        node.texturePath = objSource.value("texturePath", "");
        node.geometryType = objSource.value("geometryType", -1);
        
        auto p = objSource.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = objSource.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = objSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        auto c = objSource.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }));
        node.position = { p[0], p[1], p[2] };
        node.rotation = { r[0], r[1], r[2] };
        node.scale = { s[0], s[1], s[2] };
        node.color = { c[0], c[1], c[2], c[3] };
        
        node.blendMode = objSource.value("blendMode", 0);
        node.billboard = objSource.value("billboard", false);
        node.bloomPostEffect = objSource.value("bloomPostEffect", false);
        node.outlineBloomPostEffect = objSource.value("outlineBloomPostEffect", false);
        
        auto bc = objSource.value("bloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
        node.bloomColor = { bc[0], bc[1], bc[2], bc[3] };
        
        auto obc = objSource.value("outlineBloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
        node.outlineBloomColor = { obc[0], obc[1], obc[2], obc[3] };
        
        for (const auto& kfSource : objSource.value("keyframes", json::array())) {
            EffectObjectKeyframe kf;
            kf.time = kfSource.value("time", 0.0f);
            auto kp = kfSource.value("position", p);
            auto kr = kfSource.value("rotation", r);
            auto ks = kfSource.value("scale", s);
            auto kc = kfSource.value("color", c);
            kf.position = { kp[0], kp[1], kp[2] };
            kf.rotation = { kr[0], kr[1], kr[2] };
            kf.scale = { ks[0], ks[1], ks[2] };
            kf.color = { kc[0], kc[1], kc[2], kc[3] };
            
            kf.bloomPostEffect = kfSource.value("bloomPostEffect", node.bloomPostEffect);
            kf.outlineBloomPostEffect = kfSource.value("outlineBloomPostEffect", node.outlineBloomPostEffect);
            
            auto kbc = kfSource.value("bloomColor", bc);
            kf.bloomColor = { kbc[0], kbc[1], kbc[2], kbc[3] };
            
            auto kobc = kfSource.value("outlineBloomColor", obc);
            kf.outlineBloomColor = { kobc[0], kobc[1], kobc[2], kobc[3] };
            
            node.keyframes.push_back(kf);
        }
        
        std::sort(node.keyframes.begin(), node.keyframes.end(), [](const EffectObjectKeyframe& a, const EffectObjectKeyframe& b) {
            return a.time < b.time;
        });

        // vertexOffsetsの読み込み
        for (const auto& offsetSource : objSource.value("vertexOffsets", json::array())) {
            uint32_t index = offsetSource.value("index", 0);
            auto offsetVal = offsetSource.value("offset", json::array({ 0.0f, 0.0f, 0.0f }));
            node.vertexOffsets[index] = { offsetVal[0], offsetVal[1], offsetVal[2] };
        }
        
        temp.objectNodes.push_back(std::move(node));
    }

    templates_[effectName] = std::move(temp);
}

void EffectManager::Play(const std::string& effectName, const Vector3& worldPosition) {
    auto it = templates_.find(effectName);
    if (it == templates_.end()) {
        return;
    }

    const auto& temp = it->second;
    ActiveEffect active;
    active.templateName = effectName;
    active.worldPosition = worldPosition;
    active.currentTime = 0.0f;
    active.duration = temp.duration;
    active.hasEmitted.resize(temp.particleNodes.size(), false);

    // 3Dオブジェクトのインスタンス化
    for (const auto& node : temp.objectNodes) {
        ActiveEffectObject actObj;
        actObj.id = node.id;
        actObj.geometryType = node.geometryType;
        actObj.modelPath = node.modelPath;
        actObj.texturePath = node.texturePath;
        actObj.blendMode = node.blendMode;
        actObj.billboard = node.billboard;
        actObj.bloomPostEffect = node.bloomPostEffect;
        actObj.outlineBloomPostEffect = node.outlineBloomPostEffect;
        actObj.bloomColor = node.bloomColor;
        actObj.outlineBloomColor = node.outlineBloomColor;
        actObj.keyframes = node.keyframes;

        actObj.object = std::make_unique<Object3d>();
        actObj.object->Initialize(objCommon_, dxCommon_);
        actObj.object->SetCamera(camera_);
        actObj.object->SetIsVisible(true);

        // モデル設定
        if (node.geometryType >= 0) {
            Model* model = GetOrCreateEffectGeometryModel(node.geometryType);
            actObj.object->SetModel(model);
        } else if (!node.modelPath.empty()) {
            actObj.object->SetModel(node.modelPath);
        }

        // テクスチャ設定
        if (!node.texturePath.empty()) {
            actObj.object->SetTexture(node.texturePath);
        }

        // ブレンドモード設定
        Object3dCommon::BlendMode bMode = Object3dCommon::BlendMode::kBlendModeNormal;
        if (node.blendMode >= 0 && node.blendMode < static_cast<int>(Object3dCommon::BlendMode::kCountOfBlendMode)) {
            bMode = static_cast<Object3dCommon::BlendMode>(node.blendMode);
        }
        actObj.object->SetBlendMode(bMode);

        // 初期トランスフォーム
        actObj.object->SetTranslate(worldPosition + node.position);
        actObj.object->SetRotate(node.rotation);
        actObj.object->SetScale(node.scale);
        actObj.object->SetMaterialColor(node.color);

        actObj.vertexOffsets = node.vertexOffsets;
        if (!node.vertexOffsets.empty() && actObj.object->GetModel()) {
            Model* originalModel = actObj.object->GetModel();
            static uint32_t sUniqueCounter = 0;
            std::string uniqueKey = "UniquePlayModel_" + effectName + "_" + node.name + "_" + std::to_string(node.id) + "_" + std::to_string(sUniqueCounter++);
            Model* uniqueModel = ModelManager::GetInstance()->CreatePrimitiveModel(uniqueKey, originalModel->GetModelData());
            actObj.object->SetModel(uniqueModel);

            for (const auto& [idx, pos] : node.vertexOffsets) {
                uniqueModel->UpdateVertexPosition(idx, pos);
            }
        }

        active.objects.push_back(std::move(actObj));
    }

    activeEffects_.push_back(std::move(active));
}

void EffectManager::Update(float dt) {
    auto* pm = ParticleManager::GetInstance();
    for (auto it = activeEffects_.begin(); it != activeEffects_.end(); ) {
        auto& active = *it;
        const auto& temp = templates_[active.templateName];

        active.currentTime += dt;

        for (size_t i = 0; i < temp.particleNodes.size(); ++i) {
            if (!active.hasEmitted[i]) {
                const auto& node = temp.particleNodes[i];
                if (active.currentTime >= node.startTime) {
                    active.hasEmitted[i] = true;

                    float duration = std::max(0.001f, node.endTime - node.startTime);
                    float timeScale = node.presetDuration / duration;

                    Vector3 spawnPos = active.worldPosition + node.position;

                    std::vector<std::string> groupNames = pm->GetGroupNamesLoadedFromFile(node.particleFileName);
                    for (const auto& groupName : groupNames) {
                        pm->EmitConfigured(groupName, spawnPos, timeScale);
                    }
                }
            }
        }

        // 3Dオブジェクトのキーフレームアニメーション更新
        for (auto& obj : active.objects) {
            if (!obj.keyframes.empty()) {
                if (active.currentTime < obj.keyframes.front().time) {
                    obj.object->SetIsVisible(false);
                } else {
                    obj.object->SetIsVisible(true);
                }

                Vector3 pos = obj.keyframes.front().position;
                Vector3 rot = obj.keyframes.front().rotation;
                Vector3 scl = obj.keyframes.front().scale;
                Vector4 col = obj.keyframes.front().color;

                if (active.currentTime <= obj.keyframes.front().time) {
                    pos = obj.keyframes.front().position;
                    rot = obj.keyframes.front().rotation;
                    scl = obj.keyframes.front().scale;
                    col = obj.keyframes.front().color;
                } else if (active.currentTime >= obj.keyframes.back().time) {
                    pos = obj.keyframes.back().position;
                    rot = obj.keyframes.back().rotation;
                    scl = obj.keyframes.back().scale;
                    col = obj.keyframes.back().color;
                } else {
                    for (size_t k = 0; k < obj.keyframes.size() - 1; ++k) {
                        const auto& kf0 = obj.keyframes[k];
                        const auto& kf1 = obj.keyframes[k + 1];
                        if (active.currentTime >= kf0.time && active.currentTime <= kf1.time) {
                            float t = (active.currentTime - kf0.time) / (kf1.time - kf0.time);
                            pos = EffectLerp(kf0.position, kf1.position, t);
                            rot = EffectLerp(kf0.rotation, kf1.rotation, t);
                            scl = EffectLerp(kf0.scale, kf1.scale, t);
                            col = EffectLerp(kf0.color, kf1.color, t);
                            break;
                        }
                    }
                }

                obj.object->SetTranslate(active.worldPosition + pos);
                obj.object->SetRotate(rot);
                obj.object->SetScale(scl);
                obj.object->SetMaterialColor(col);
            } else {
                // キーフレームが無い場合、テンプレートから初期座標を取得し、worldPositionを考慮して配置
                const EffectObjectNode* nodeTemplate = nullptr;
                for (const auto& tn : temp.objectNodes) {
                    if (tn.id == obj.id) {
                        nodeTemplate = &tn;
                        break;
                    }
                }
                if (nodeTemplate) {
                    obj.object->SetTranslate(active.worldPosition + nodeTemplate->position);
                    obj.object->SetRotate(nodeTemplate->rotation);
                    obj.object->SetScale(nodeTemplate->scale);
                    obj.object->SetMaterialColor(nodeTemplate->color);
                }
            }

            obj.object->Update(dt);
        }

        if (active.currentTime >= active.duration) {
            it = activeEffects_.erase(it);
        } else {
            ++it;
        }
    }
}

void EffectManager::Draw() {
    if (!objCommon_) return;
    for (auto& active : activeEffects_) {
        for (auto& obj : active.objects) {
            if (obj.object) {
                obj.object->Draw();
            }
        }
    }
}
