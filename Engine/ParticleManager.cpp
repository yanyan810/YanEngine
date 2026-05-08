#include "ParticleManager.h"
#include "DirectXCommon.h"
#include "Camera.h" 
#include "Model.h"
#include "ModelManager.h"
#include "GeometryGenerator.h"
#include <filesystem>
#include <algorithm>
#include <imgui.h>

ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager instance;
    return &instance;
}

namespace {
    Model::ModelData MakeParticlePrimitiveModelData(const std::vector<Model::VertexData>& vertices) {
        Model::ModelData md{};
        md.materials.push_back({ "" });

        Model::MeshData mesh{};
        mesh.materialIndex = 0;
        mesh.vertices = vertices;
        mesh.skinned = false;
        mesh.startVertex = 0;
        mesh.vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.startIndex = 0;
        mesh.indexCount = static_cast<uint32_t>(vertices.size());

        md.meshes.push_back(std::move(mesh));

        md.indices.resize(vertices.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
            md.indices[i] = i;
        }

        md.rootNode.name = "PrimitiveRoot";
        md.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
        md.rootNode.meshIndices.push_back(0);

        return md;
    }

    Model* GetOrMakeParticlePrimitiveModel(int typeIndex) {
        std::string key = "ParticlePrimitive_" + std::to_string(typeIndex);
        Model* m = ModelManager::GetInstance()->FindModel(key);
        if (m) return m;

        std::vector<Model::VertexData> vertices;
        switch (typeIndex) {
        case 0: vertices = GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f); break;
        case 1: vertices = GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f); break;
        case 2: vertices = GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f); break;
        case 3: vertices = GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f); break;
        case 4: vertices = GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f); break;
        case 5: vertices = GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f); break;
        case 6: vertices = GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f); break;
        case 7: vertices = GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f); break;
        default: return nullptr;
        }

        auto modelData = MakeParticlePrimitiveModelData(vertices);
        return ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
    }
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

    // PerFrame リソース作成
    perFrameResource_ = dxCommon_->CreateBufferResource(sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedPerFrame_));
    mappedPerFrame_->time = 0.0f;
    mappedPerFrame_->deltaTime = 0.0f;
    time_ = 0.0f;

    // --- ダミーマテリアル＆ライト ---
    // HLSLの struct Material { float4 color; int enableLighting; float4x4 uvTransform; } (サイズ: 16+4+64 = 84 -> アライメント考慮で256の倍数)
    materialResource_ = dxCommon_->CreateBufferResource(256);
    struct DummyMaterial { Vector4 color; int enableLighting; float padding[3]; Matrix4x4 uvTransform; };
    DummyMaterial* mappedMat = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedMat));
    mappedMat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mappedMat->enableLighting = 0;
    mappedMat->uvTransform = Matrix4x4::MakeIdentity4x4();

    // HLSLの struct DirectionalLight { float4 color; float3 direction; float intensity; }
    dirLightResource_ = dxCommon_->CreateBufferResource(256);
    struct DummyLight { Vector4 color; Vector3 direction; float intensity; };
    DummyLight* mappedLight = nullptr;
    dirLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedLight));
    mappedLight->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mappedLight->direction = { 0.0f, -1.0f, 0.0f };
    mappedLight->intensity = 1.0f;
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

    // PerFrame の更新
    time_ += dt;
    if (mappedPerFrame_) {
        mappedPerFrame_->time = time_;
        mappedPerFrame_->deltaTime = dt;
    }

    // Emitterの更新
    for (auto& [name, group] : particleGroups_) {
        if (group.mappedEmitter) {
            group.mappedEmitter->frequencyTime += dt; // δタイムを加算
            // 射出間隔を上回ったら射出許可を出して時間を調整
            if (group.mappedEmitter->frequency <= group.mappedEmitter->frequencyTime) {
                group.mappedEmitter->frequencyTime -= group.mappedEmitter->frequency;
                group.mappedEmitter->emit = 1;
            // 射出間隔を上回っていないので、射出許可は出せない
            } else {
                group.mappedEmitter->emit = 0;
            }
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

    // --- FreeListIndex buffer ---
    D3D12_RESOURCE_DESC counterDesc = resDesc;
    counterDesc.Width = sizeof(int32_t); // 1 element
    
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &counterDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.freeListIndexResource));
    assert(SUCCEEDED(hr));

    group.freeListIndexUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListIndexUavIndex,
        group.freeListIndexResource.Get(),
        1,
        sizeof(int32_t)
    );

    // --- FreeList buffer ---
    D3D12_RESOURCE_DESC listDesc = resDesc;
    listDesc.Width = sizeof(uint32_t) * kMaxInstance;
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &listDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&group.freeListResource));
    assert(SUCCEEDED(hr));

    group.freeListUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListUavIndex,
        group.freeListResource.Get(),
        kMaxInstance,
        sizeof(uint32_t)
    );

    // --- Compute Shaderで初期化 (Dispatch) ---
    auto* cmd = dxCommon_->GetCommandList();

    // barrier: COMMON -> UAV
    D3D12_RESOURCE_BARRIER barriers[3]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = group.instancingResource.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = group.freeListIndexResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = group.freeListResource.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(3, barriers);

    particleCommon_->SetComputePipelineState();
    
    // ★追加: DescriptorHeapをコマンドリストにセットする
    srvManager_->PreDraw();

    // UAVセット (Register u0, u1, u2)
    cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
    cmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
    cmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

    cmd->Dispatch(kMaxInstance, 1, 1);

    // barrier: UAV -> SRV (NON_PIXEL_SHADER_RESOURCE)
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // --- Emitter Buffer ---
    group.emitterResource = dxCommon_->CreateBufferResource(sizeof(EmitterSphere));
    group.emitterResource->Map(0, nullptr, reinterpret_cast<void**>(&group.mappedEmitter));
    group.mappedEmitter->count = 10;
    group.mappedEmitter->frequency = 0.5f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->translate = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->radius = 5.0f;
    group.mappedEmitter->emit = 0;

    particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::CreateParticleGroup(
    const std::string& name,
    Model* model)
{
    assert(!particleGroups_.contains(name));
    assert(model != nullptr);

    ParticleGroup group{};
    group.model = model;

    // --- texture ---
    const auto& materials = model->GetMaterials();
    if (!materials.empty() && !materials[0].textureFilePath.empty()) {
        const std::string& texPath = materials[0].textureFilePath;
        if (!TextureManager::GetInstance()->HasTexture(texPath)) {
            TextureManager::GetInstance()->LoadTexture(texPath);
        }
        group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texPath);
    } else {
        // 空パスのときは TextureManager に白テクスチャ等があるならそれを使うか 0 番にする
        group.textureSrvIndex = 0; 
    }

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

    // --- FreeListIndex buffer ---
    D3D12_RESOURCE_DESC counterDesc = resDesc;
    counterDesc.Width = sizeof(int32_t); // 1 element
    
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &counterDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.freeListIndexResource));
    assert(SUCCEEDED(hr));

    group.freeListIndexUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListIndexUavIndex,
        group.freeListIndexResource.Get(),
        1,
        sizeof(int32_t)
    );

    // --- FreeList buffer ---
    D3D12_RESOURCE_DESC listDesc = resDesc;
    listDesc.Width = sizeof(uint32_t) * kMaxInstance;
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &listDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&group.freeListResource));
    assert(SUCCEEDED(hr));

    group.freeListUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListUavIndex,
        group.freeListResource.Get(),
        kMaxInstance,
        sizeof(uint32_t)
    );

    // --- Compute Shaderで初期化 (Dispatch) ---
    auto* cmd = dxCommon_->GetCommandList();

    D3D12_RESOURCE_BARRIER barriers[3]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = group.instancingResource.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = group.freeListIndexResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = group.freeListResource.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(3, barriers);

    particleCommon_->SetComputePipelineState();
    
    srvManager_->PreDraw();

    cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
    cmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
    cmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

    cmd->Dispatch(kMaxInstance, 1, 1);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // --- Emitter Buffer ---
    group.emitterResource = dxCommon_->CreateBufferResource(sizeof(EmitterSphere));
    group.emitterResource->Map(0, nullptr, reinterpret_cast<void**>(&group.mappedEmitter));
    group.mappedEmitter->count = 10;
    group.mappedEmitter->frequency = 0.5f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->translate = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->radius = 5.0f;
    group.mappedEmitter->emit = 0;

    particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::SetGroupBlendMode(const std::string& groupName, ParticleCommon::BlendMode mode) {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;
    it->second.blendMode = mode;
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* cmd) {
    for (auto& [name, group] : particleGroups_) {

        // --- Compute Shader による Emit ---
        if (particleCommon_) {
            particleCommon_->SetEmitComputePipelineState();
        }

        // バリア: SRV -> UAV
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = group.instancingResource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);

        // SRVヒープをセット
        srvManager_->PreDraw();

        // b0 に Emitter (CBV)
        cmd->SetComputeRootConstantBufferView(3, group.emitterResource->GetGPUVirtualAddress());
        // b1 に PerFrame (CBV) ★追加
        cmd->SetComputeRootConstantBufferView(4, perFrameResource_->GetGPUVirtualAddress());
        // u0 に Particles (UAV)
        cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
        // u1 に FreeListIndex (UAV)
        cmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
        // u2 に FreeList (UAV)
        cmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

        // 実行 (Emit)
        cmd->Dispatch(1, 1, 1);

        // --- UAV Barrier ---
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        uavBarrier.UAV.pResource = group.instancingResource.Get();
        cmd->ResourceBarrier(1, &uavBarrier);

        // --- Compute Shader による Update ---
        if (particleCommon_) {
            particleCommon_->SetUpdateComputePipelineState();
        }

        // Update用のパラメータをセット (u0のみ使用、b1のPerFrameは既にセット済みなので流用可能だが念のため再セットするかどうか。同じRootSigなので維持される)
        // cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
        // cmd->SetComputeRootConstantBufferView(3, perFrameResource_->GetGPUVirtualAddress());

        // 実行 (Update)
        cmd->Dispatch(kMaxInstance / 1024 > 0 ? kMaxInstance / 1024 : 1, 1, 1);

        // バリア: UAV -> SRV
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmd->ResourceBarrier(1, &barrier);

        // --- Graphics による描画 ---
        // ★ ブレンド切替（PSO切替）
        if (particleCommon_) {
            particleCommon_->SetBlendMode(group.blendMode);
            particleCommon_->SetGraphicsPipelineState();
        }

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // RootParameter 0 (b0) に Material
        cmd->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
        // RootParameter 3 (b1) に DirectionalLight
        cmd->SetGraphicsRootConstantBufferView(3, dirLightResource_->GetGPUVirtualAddress());
        // PerView(CBV) を RootParameter 4 にセット (b0)
        cmd->SetGraphicsRootConstantBufferView(4, perViewResource_->GetGPUVirtualAddress());

        srvManager_->SetGraphicsDescriptorTable(2, group.textureSrvIndex);
        srvManager_->SetGraphicsDescriptorTable(1, group.instancingSrvIndex);

        if (group.model) {
            auto vbv = group.model->GetVBV();
            // モデルの読み込みに失敗している等の理由で StrideInBytes が 0 の場合はゼロ除算になるためスキップ
            if (vbv.StrideInBytes == 0) continue;

            cmd->IASetVertexBuffers(0, 1, &vbv);
            
            if (group.model->HasIndexBuffer()) {
                auto ibv = group.model->GetIBV();
                cmd->IASetIndexBuffer(&ibv);
                uint32_t indexCount = ibv.SizeInBytes / sizeof(uint32_t);
                if (indexCount > 0) {
                    cmd->DrawIndexedInstanced(indexCount, kMaxInstance, 0, 0, 0);
                }
            } else {
                uint32_t vertexCount = vbv.SizeInBytes / vbv.StrideInBytes;
                if (vertexCount > 0) {
                    cmd->DrawInstanced(vertexCount, kMaxInstance, 0, 0);
                }
            }
        } else {
            cmd->IASetVertexBuffers(0, 1, &vbView_);
            if (kVertexCount > 0) {
                // 1024個すべて描画
                cmd->DrawInstanced(kVertexCount, kMaxInstance, 0, 0);
            }
        }
    }
}

void ParticleManager::ScanResources() {
    modelFiles_.clear();
    textureFiles_.clear();
    
    std::string targetDir = "Resources";
    if (!std::filesystem::exists(targetDir)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(targetDir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        std::string path = entry.path().string();
        std::replace(path.begin(), path.end(), '\\', '/'); 
        
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
            // モデル読み込み時は内部で "resources/" が付与されるため、先頭の "resources/" を削除する
            std::string lowerPath = path;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
            if (lowerPath.starts_with("resources/")) {
                path = path.substr(10);
            }
            modelFiles_.push_back(path);
        } else if (ext == ".png" || ext == ".jpg" || ext == ".dds") {
            textureFiles_.push_back(path);
        }
    }
    isResourcesScanned_ = true;
}

void ParticleManager::DrawImGui() {
#ifdef _DEBUG
    ImGui::Begin("Particle Manager");

    if (ImGui::Button("Scan Resources")) {
        ScanResources();
    }
    if (!isResourcesScanned_) {
        ScanResources();
    }

    for (auto& [name, group] : particleGroups_) {
        if (ImGui::TreeNode(name.c_str())) {
            
            // ブレンドモード
            const char* blendModes[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
            int currentBlend = static_cast<int>(group.blendMode);
            if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                group.blendMode = static_cast<ParticleCommon::BlendMode>(currentBlend);
            }

            // モデル選択
            std::string currentModelName = group.model ? "Custom Model / Primitive" : "None (Board Polygon)";
            if (ImGui::BeginCombo("Model", currentModelName.c_str())) {
                bool isSelected = (group.model == nullptr);
                if (ImGui::Selectable("None (Board Polygon)", isSelected)) {
                    group.model = nullptr;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();

                ImGui::Separator();
                ImGui::Text("Primitives");
                const char* primNames[] = { "Ring", "Sphere", "Box", "Plane", "Torus", "Cylinder", "Cone", "Triangle" };
                for (int i = 0; i < 8; ++i) {
                    if (ImGui::Selectable(primNames[i], false)) {
                        group.model = GetOrMakeParticlePrimitiveModel(i);
                    }
                }
                ImGui::Separator();
                ImGui::Text("Files");

                for (size_t i = 0; i < modelFiles_.size(); ++i) {
                    isSelected = false; 
                    if (ImGui::Selectable(modelFiles_[i].c_str(), isSelected)) {
                        std::string path = modelFiles_[i];
                        ModelManager::GetInstance()->LoadModel(path);
                        group.model = ModelManager::GetInstance()->FindModel(path);
                    }
                }
                ImGui::EndCombo();
            }

            // テクスチャ選択
            std::string currentTexName = "Select Texture...";
            if (ImGui::BeginCombo("Texture", currentTexName.c_str())) {
                for (size_t i = 0; i < textureFiles_.size(); ++i) {
                    bool isSelected = false;
                    if (ImGui::Selectable(textureFiles_[i].c_str(), isSelected)) {
                        std::string path = textureFiles_[i];
                        TextureManager::GetInstance()->LoadTexture(path);
                        group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(path);
                    }
                }
                ImGui::EndCombo();
            }

            // Emitterパラメータ
            if (group.mappedEmitter) {
                ImGui::DragFloat3("Translate", &group.mappedEmitter->translate.x, 0.1f);
                ImGui::DragFloat("Radius", &group.mappedEmitter->radius, 0.1f);
                ImGui::DragFloat("Frequency", &group.mappedEmitter->frequency, 0.01f, 0.01f, 5.0f);
            }

            ImGui::TreePop();
        }
    }

    ImGui::End();
#endif
}


