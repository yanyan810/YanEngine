#include "ParticleManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Camera.h" 

ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager instance;
    return &instance;
}

void ParticleManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, ParticleCommon* particleCommon) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    particleCommon_ = particleCommon;
    // ランダム初期化
    std::random_device rd;
    randomEngine_ = std::mt19937(rd());

    // 頂点配列確保
    vertices_.resize(kVertexCount);

    // 左上(0) 右上(1) 右下(2) / 左上(0) 右下(2) 左下(3)
    auto setV = [&](int i, float x, float y, float u, float v) {
        vertices_[i].position[0] = x;
        vertices_[i].position[1] = y;
        vertices_[i].position[2] = 0.0f;
        vertices_[i].position[3] = 1.0f;
        vertices_[i].uv[0] = u;
        vertices_[i].uv[1] = v;
        vertices_[i].normal[0] = 0.0f;
        vertices_[i].normal[1] = 0.0f;
        vertices_[i].normal[2] = -1.0f;
        };

    // サイズは好み。まず見える確認なら大きめでOK
    const float s = 0.5f;
    setV(0, -s, +s, 0.0f, 0.0f);
    setV(1, +s, +s, 1.0f, 0.0f);
    setV(2, +s, -s, 1.0f, 1.0f);
    setV(3, -s, +s, 0.0f, 0.0f);
    setV(4, +s, -s, 1.0f, 1.0f);
    setV(5, -s, -s, 0.0f, 1.0f);

    // bufferSize も kVertexCount 分にする
    const UINT bufferSize = sizeof(ParticleVertex) * kVertexCount;


    vertexBuffer_ = dxCommon_->CreateBufferResource(bufferSize);

    // VBV
    vbView_.BufferLocation =
        vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = bufferSize;
    vbView_.StrideInBytes = sizeof(ParticleVertex);

    // GPUへ書き込み
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, vertices_.data(), bufferSize);
    vertexBuffer_->Unmap(0, nullptr);
}

void ParticleManager::Finalize() {
    particleGroups_.clear();
    vertexBuffer_.Reset();
}

void ParticleManager::Update(float dt, const Camera& camera)
{
    // ★ 実カメラから取得
    const Matrix4x4& vp = camera.GetViewProjectionMatrix();
    const Matrix4x4& cameraMatrix = camera.GetWorldMatrix();

    // billboard（今のやり方をそのまま）
  /*  Matrix4x4 backToFrontMatrix = Matrix4x4::RotateY(std::numbers::pi_v<float> *0.5f);
    Matrix4x4 billboardMatrix = Matrix4x4::Multiply(backToFrontMatrix, cameraMatrix);
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;*/
    Matrix4x4 billboardMatrix = cameraMatrix;
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;


    for (auto& [name, group] : particleGroups_) {

        group.instanceCount = 0;

        auto* gpu = reinterpret_cast<Particle::ParticleForGPU*>(group.mappedData);
        if (!gpu) { continue; }

        for (auto it = group.particles.begin();
            it != group.particles.end() && group.instanceCount < kMaxInstance; )
        {
            auto& p = *it;

            if (p.lifeTime <= p.currentTime) {
                it = group.particles.erase(it);
                continue;
            }

            p.currentTime += dt;
            p.transform.translate += p.velocity * dt;

            float alpha = 1.0f - (p.currentTime / p.lifeTime);

            Matrix4x4 scaleM = Matrix4x4::MakeScaleMatrix(p.transform.scale);
            Matrix4x4 translateM = Matrix4x4::Translation(p.transform.translate);

            Matrix4x4 world =
                Matrix4x4::Multiply(
                    Matrix4x4::Multiply(scaleM, billboardMatrix),
                    translateM);

            Matrix4x4 wvp = Matrix4x4::Multiply(world, vp);

            gpu[group.instanceCount].World = world;
            gpu[group.instanceCount].WVP = wvp;
            gpu[group.instanceCount].color = p.color;
            gpu[group.instanceCount].color.w = alpha;

            group.instanceCount++;
            ++it;
        }
    }
}


void ParticleManager::Emit(const std::string& groupName,
    const Vector3& pos,
    uint32_t count)
{
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;

    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::uniform_real_distribution<float> c(0.0f, 1.0f);
    std::uniform_real_distribution<float> t(1.0f, 3.0f);

    for (uint32_t i = 0; i < count; ++i) {
        Particle::ParticleData p{};
        p.transform.scale = { 1,1,1 };
        p.transform.rotate = { 0,0,0 };
        p.transform.translate = {
            pos.x + d(randomEngine_),
            pos.y + d(randomEngine_),
            pos.z + d(randomEngine_)
        };
        p.velocity = { d(randomEngine_), d(randomEngine_), d(randomEngine_) };
        p.color = { c(randomEngine_), c(randomEngine_), c(randomEngine_), 1.0f };
        p.lifeTime = t(randomEngine_);
        p.currentTime = 0.0f;

        it->second.particles.push_back(p);
    }
}

void ParticleManager::CreateParticleGroup(
    const std::string& name,
    const std::string& texturePath)
{
    assert(!particleGroups_.contains(name));

    ParticleGroup group{};

    // --- texture ---
    TextureManager::GetInstance()->LoadTexture(texturePath);
    group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texturePath);

    // --- instancing buffer ---
    group.instancingResource =
        dxCommon_->CreateBufferResource(sizeof(Particle::ParticleForGPU) * kMaxInstance);
    group.instancingResource->Map(0, nullptr, &group.mappedData);

    // --- instancing SRV ---
    group.instancingSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        group.instancingSrvIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particle::ParticleForGPU)
    );

    particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::SetGroupBlendMode(const std::string& groupName, ParticleCommon::BlendMode mode) {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;
    it->second.blendMode = mode;
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* cmd) {
    for (auto& [name, group] : particleGroups_) {

        // ★ ブレンド切替（PSO切替）
        if (particleCommon_) {
            particleCommon_->SetBlendMode(group.blendMode);
            particleCommon_->SetGraphicsPipelineState();
        }

        cmd->IASetVertexBuffers(0, 1, &vbView_);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        srvManager_->SetGraphicsDescriptorTable(2, group.textureSrvIndex);
        srvManager_->SetGraphicsDescriptorTable(1, group.instancingSrvIndex);

        cmd->DrawInstanced(kVertexCount, group.instanceCount, 0, 0);
    }
}


