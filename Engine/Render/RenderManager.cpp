#include "RenderManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <cassert>

// ImGui
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// ポストエフェクト名（ImGui表示用・英語のみ）
static const char* kEffectNames[] = {
    "FullScreen (No Effect)",
    "Grayscale",
    "Vignette",
};

// 各エフェクトのPSパス
static const wchar_t* kEffectPSPaths[] = {
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Grayscale.PS.hlsl",
    L"resources/shaders/Vignette.PS.hlsl",
};

void RenderManager::Initialize(DirectXCommon* dx, SrvManager* srv)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;

    offscreen_ = std::make_unique<OffscreenPass>();

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

    // ルートシグネチャ（全エフェクト共有）
    CreateCopyImageRootSignature();

    // 各エフェクトのPSOを作成
    for (int i = 0; i < kEffectCount; ++i) {
        CreatePipelineState(kEffectPSPaths[i], pipelineStates_[i]);
    }
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

// ★ ルートシグネチャ作成（共有）
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

// ★ 各エフェクト用PSOを作成するヘルパー
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

// ★ オフスクリーンをバックバッファへ描画（現在選択中のエフェクトを適用）
void RenderManager::DrawOffscreenToBackBuffer()
{
    assert(offscreen_);

    auto* cmd = dx_->GetCommandList();

    // SRVヒープを明示的に再セット
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);

    // RenderTexture を読む状態へ
    dx_->TransitionResource(
        offscreen_->GetResource(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    // 現在のモードに対応するPSOを選択
    int modeIndex = static_cast<int>(currentMode_);
    assert(modeIndex >= 0 && modeIndex < kEffectCount);

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(pipelineStates_[modeIndex].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootDescriptorTable(
        0,
        srv_->GetGPUDescriptionHandle(offscreen_->GetSrvIndex())
    );

    cmd->DrawInstanced(3, 1, 0, 0);

    dx_->TransitionResource(
        offscreen_->GetResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
}

// ★ ImGui でポストエフェクトを切り替える
void RenderManager::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Begin("Post Effect");

    int current = static_cast<int>(currentMode_);
    if (ImGui::Combo("Effect##posteffect", &current, kEffectNames, kEffectCount)) {
        currentMode_ = static_cast<PostEffectMode>(current);
    }

    ImGui::Separator();
    ImGui::Text("Current: %s", kEffectNames[current]);

    ImGui::End();
#endif
}