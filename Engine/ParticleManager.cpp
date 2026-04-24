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

    // PerView リソース作成
    perViewResource_ = dxCommon_->CreateBufferResource(sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedPerView_));
    // 初期値
    mappedPerView_->viewProjection = Matrix4x4::MakeIdentity4x4();
    mappedPerView_->billboardMatrix = Matrix4x4::MakeIdentity4x4();
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

    Matrix4x4 billboardMatrix = cameraMatrix;
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;

    // PerView の更新
    if (mappedPerView_) {
        mappedPerView_->viewProjection = vp;
        mappedPerView_->billboardMatrix = billboardMatrix;
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

    // --- instancing buffer (DEFAULT heap + UAV) ---
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = sizeof(Particles) * kMaxInstance;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.instancingResource));
    assert(SUCCEEDED(hr));

    // --- instancing SRV & UAV ---
    group.instancingSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        group.instancingSrvIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    group.instancingUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.instancingUavIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    // --- Compute Shaderで初期化 (Dispatch) ---
    auto* cmd = dxCommon_->GetCommandList();

    // barrier: COMMON -> UAV
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    particleCommon_->SetComputePipelineState();
    
    // ★追加: DescriptorHeapをコマンドリストにセットする
    srvManager_->PreDraw();

    // UAVセット (Register u0)
    cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));

    cmd->Dispatch(kMaxInstance, 1, 1);

    // barrier: UAV -> SRV (NON_PIXEL_SHADER_RESOURCE)
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &barrier);

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

        // PerView(CBV) を RootParameter 4 にセット (b0)
        cmd->SetGraphicsRootConstantBufferView(4, perViewResource_->GetGPUVirtualAddress());

        srvManager_->SetGraphicsDescriptorTable(2, group.textureSrvIndex);
        srvManager_->SetGraphicsDescriptorTable(1, group.instancingSrvIndex);

        // 1024個すべて描画
        cmd->DrawInstanced(kVertexCount, kMaxInstance, 0, 0);
    }
}


