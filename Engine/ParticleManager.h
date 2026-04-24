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

    // CPU用のパーティクル管理（後日不要になる可能性あり）
    std::list<Particle::ParticleData> particles;

    // UAV用リソース（DEFAULTヒープ）
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;

    uint32_t instancingSrvIndex = 0;
    uint32_t instancingUavIndex = 0; // ★追加

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

    void CreateParticleGroup(
        const std::string& name,
        const std::string& texturePath);

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
};
