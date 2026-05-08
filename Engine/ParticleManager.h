#pragma once
#include <vector>
#include <random>
#include <wrl.h>
#include <d3d12.h>
#include "Particle.h"

#include <unordered_map>
#include <list>
#include <string>
#include "TextureManager.h"

#include "ParticleCommon.h"

class Model;

// GPU Particle用構造体
struct Particles {
    Vector3 translate;
    Vector3 scale;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    Vector4 color;
};

struct PerView {
    Matrix4x4 viewProjection;
    Matrix4x4 billboardMatrix;
};

// GPU Particle用 PerFrame
struct PerFrame {
    float time;
    float deltaTime;
};

// Emitter用の構造体
struct EmitterSphere {
    Vector3 translate; // 位置
    float radius; // 射出半径
    uint32_t count; // 射出数
    float frequency; // 射出間隔
    float frequencyTime; // 射出間隔調整用時間
    uint32_t emit; // 射出許可
};

// ===============================
// 定数
// ===============================
static constexpr uint32_t kMaxInstance = 1024;
// 板ポリ1枚 = 2三角形 = 6頂点
static constexpr uint32_t kVertexCount = 6;

// ===============================
// パーティクルグループ
// ===============================
struct ParticleGroup {
    uint32_t textureSrvIndex = 0;
    Model* model = nullptr;

    // CPU用のパーティクル管理（後日不要になる可能性あり）
    std::list<Particle::ParticleData> particles;

    // UAV用リソース（DEFAULTヒープ）
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;

    uint32_t instancingSrvIndex = 0;
    uint32_t instancingUavIndex = 0; // ★追加

    // FreeListIndex用リソース (元freeCounter)
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource;
    uint32_t freeListIndexUavIndex = 0;

    // FreeList用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource;
    uint32_t freeListUavIndex = 0;

    // Emitter用リソース（UPLOADヒープ、CBV）
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource;
    EmitterSphere* mappedEmitter = nullptr;

    ParticleCommon::BlendMode blendMode = ParticleCommon::BlendMode::kBlendModeNormal; 

};



class DirectXCommon;
class SrvManager;
class Camera;

class ParticleManager {
public:
    static ParticleManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, ParticleCommon* particleCommon); // ★追加
    void Finalize();
    void SetGroupBlendMode(const std::string& groupName, ParticleCommon::BlendMode mode);            // ★追加
    void Update(float deltaTime, const Camera& camera);
    void Draw(ID3D12GraphicsCommandList* cmd);
    void DrawImGui(); // ★追加

    void CreateParticleGroup(
        const std::string& name,
        const std::string& texturePath);

    void CreateParticleGroup(
        const std::string& name,
        Model* model);

    void Emit(const std::string& groupName,
        const Vector3& pos,
        uint32_t count);

private:
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

private:
    // === 外部システム ===
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // === ランダム ===
    std::mt19937 randomEngine_;

    // === パーティクル頂点 ===
    struct ParticleVertex {
        float position[4]; // POSITION float4
        float uv[2];       // TEXCOORD float2
        float normal[3];   // NORMAL float3
    };
    std::vector<ParticleVertex> vertices_;

    std::unordered_map<std::string, ParticleGroup> particleGroups_;


    // === GPU リソース ===
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    uint32_t maxParticleCount_ = 1000;

    ParticleCommon* particleCommon_ = nullptr;

    // === GPU Particle用 PerView ===
    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
    PerView* mappedPerView_ = nullptr;

    // === 時間管理用 PerFrame ===
    float time_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_;
    PerFrame* mappedPerFrame_ = nullptr;

    // === ダミーマテリアル＆ライト ===
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dirLightResource_;

    // === ImGui・ファイル検索用 ===
    void ScanResources();
    std::vector<std::string> modelFiles_;
    std::vector<std::string> textureFiles_;
    bool isResourcesScanned_ = false;
};
