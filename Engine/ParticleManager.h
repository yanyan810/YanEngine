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

    // ★ ここを軽量データにする（本体 Particle クラスは入れない）
    std::list<Particle::ParticleData> particles;

    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
    void* mappedData = nullptr;

    uint32_t instanceCount = 0;
    uint32_t instancingSrvIndex = 0;

    ParticleCommon::BlendMode blendMode = ParticleCommon::BlendMode::kBlendModeNormal; 

};



class DirectXCommon;
class SrvManager;
class Camera;

class ParticleManager {
public:
    static ParticleManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, ParticleCommon* particleCommon); // ★追加
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

};
