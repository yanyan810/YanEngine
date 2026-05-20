#include "RenderManager.h"

#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"

#include <cassert>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

static const char* kEffectNames[] = {
    "FullScreen (No Effect)",
    "Grayscale",
    "Vignette",
    "BoxFilter (Smoothing)",
    "GaussianBlur (Linear)",
};

static const wchar_t* kEffectPSPaths[] = {
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Grayscale.PS.hlsl",
    L"resources/shaders/Vignette.PS.hlsl",
    L"resources/shaders/BoxFilter.PS.hlsl",
    L"resources/shaders/GaussianBlur.PS.hlsl",
};

void RenderManager::Initialize(DirectXCommon* dx, SrvManager* srv)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;

    offscreen_ = std::make_unique<OffscreenPass>();
    postBuffers_[0] = std::make_unique<OffscreenPass>();
    postBuffers_[1] = std::make_unique<OffscreenPass>();

    Vector4 clearColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    offscreen_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        clearColor,
        2
    );

    Vector4 postClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    postBuffers_[0]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        3
    );
    postBuffers_[1]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        4
    );

    CreateCopyImageRootSignature();

    for (int i = 0; i < kEffectCount; ++i) {
        CreatePipelineState(kEffectPSPaths[i], pipelineStates_[i]);
    }
}

void RenderManager::SetMode(PostEffectMode mode)
{
    currentMode_ = mode;
    ClearEffects();

    if (mode != PostEffectMode::FullScreen) {
        enabledEffects_[static_cast<int>(mode)] = true;
    }
}

void RenderManager::SetEffectEnabled(PostEffectMode mode, bool enabled)
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return;
    }

    enabledEffects_[index] = enabled;
    currentMode_ = PostEffectMode::FullScreen;
    for (int i = 1; i < kEffectCount; ++i) {
        if (enabledEffects_[i]) {
            currentMode_ = static_cast<PostEffectMode>(i);
            break;
        }
    }
}

bool RenderManager::IsEffectEnabled(PostEffectMode mode) const
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return false;
    }
    return enabledEffects_[index];
}

void RenderManager::ClearEffects()
{
    enabledEffects_.fill(false);
}

void RenderManager::BeginOffscreen()
{
    assert(offscreen_);
    offscreen_->Begin();
}

void RenderManager::EndOffscreen()
{
    assert(offscreen_);
    offscreen_->End();
}

uint32_t RenderManager::GetOffscreenSrvIndex() const
{
    assert(offscreen_);
    return offscreen_->GetSrvIndex();
}

void RenderManager::BeginBackBuffer()
{
    dx_->PreDraw();
}

void RenderManager::CreateCopyImageRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[1]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &range;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(rootParams);
    desc.pParameters = rootParams;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob,
        &errBlob);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0,
        sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&copyImageRootSignature_));
    assert(SUCCEEDED(hr));
}

void RenderManager::CreatePipelineState(
    const wchar_t* psPath,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO)
{
    auto vs = dx_->CompileShader(L"resources/shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompileShader(psPath, L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = copyImageRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.InputLayout.pInputElementDescs = nullptr;
    desc.InputLayout.NumElements = 0;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&outPSO));
    assert(SUCCEEDED(hr));
}

void RenderManager::DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex)
{
    auto* cmd = dx_->GetCommandList();

    const int modeIndex = static_cast<int>(mode);
    assert(modeIndex >= 0 && modeIndex < kEffectCount);

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(pipelineStates_[modeIndex].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(srcSrvIndex));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void RenderManager::DrawFullscreenPassToBackBuffer(
    PostEffectMode mode,
    uint32_t srcSrvIndex,
    ID3D12Resource* srcResource)
{
    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    dx_->SetBackBufferRenderTarget();
    DrawFullscreenPass(mode, srcSrvIndex);

    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
}

void RenderManager::DrawFullscreenPassToBuffer(
    PostEffectMode mode,
    uint32_t srcSrvIndex,
    ID3D12Resource* srcResource,
    OffscreenPass& dst)
{
    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    dst.Begin();
    DrawFullscreenPass(mode, srcSrvIndex);

    dx_->TransitionResource(
        srcResource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
}

void RenderManager::DrawOffscreenToBackBuffer()
{
    assert(offscreen_);
    assert(postBuffers_[0]);
    assert(postBuffers_[1]);

    auto* cmd = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);

    int lastEffect = -1;
    for (int i = 1; i < kEffectCount; ++i) {
        if (enabledEffects_[i]) {
            lastEffect = i;
        }
    }

    if (lastEffect < 0) {
        DrawFullscreenPassToBackBuffer(
            PostEffectMode::FullScreen,
            offscreen_->GetSrvIndex(),
            offscreen_->GetResource()
        );
        return;
    }

    ID3D12Resource* srcResource = offscreen_->GetResource();
    uint32_t srcSrvIndex = offscreen_->GetSrvIndex();
    int bufferIndex = 0;

    for (int i = 1; i < kEffectCount; ++i) {
        if (!enabledEffects_[i]) {
            continue;
        }

        const PostEffectMode mode = static_cast<PostEffectMode>(i);
        if (i == lastEffect) {
            DrawFullscreenPassToBackBuffer(mode, srcSrvIndex, srcResource);
        } else {
            OffscreenPass& dst = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(mode, srcSrvIndex, srcResource, dst);
            srcResource = dst.GetResource();
            srcSrvIndex = dst.GetSrvIndex();
            bufferIndex = 1 - bufferIndex;
        }
    }
}

void RenderManager::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Begin("Post Effect");

    if (ImGui::Button("Clear Effects")) {
        SetMode(PostEffectMode::FullScreen);
    }

    ImGui::Separator();
    for (int i = 1; i < kEffectCount; ++i) {
        bool enabled = enabledEffects_[i];
        if (ImGui::Checkbox(kEffectNames[i], &enabled)) {
            SetEffectEnabled(static_cast<PostEffectMode>(i), enabled);
        }
    }

    ImGui::Separator();
    ImGui::Text("Current Chain:");
    bool any = false;
    for (int i = 1; i < kEffectCount; ++i) {
        if (enabledEffects_[i]) {
            ImGui::BulletText("%s", kEffectNames[i]);
            any = true;
        }
    }
    if (!any) {
        ImGui::BulletText("%s", kEffectNames[0]);
    }

    ImGui::End();
#endif
}
