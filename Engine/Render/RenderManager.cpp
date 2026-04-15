#include "RenderManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <cassert>

void RenderManager::Initialize(DirectXCommon* dx, SrvManager* srv)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;

    offscreen_ = std::make_unique<OffscreenPass>();

   // Vector4 clearColor = { 0.1f, 0.25f, 0.5f, 1.0f };

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

	// ★オフスクリーンからバックバッファへコピーするためのルートシグネチャとパイプラインステートを作成
    CreateCopyImageRootSignature();
    CreateCopyImagePipelineState();

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

// ★オフスクリーンからバックバッファへコピーするためのルートシグネチャを作成
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

// ★オフスクリーンからバックバッファへコピーするためのパイプラインステートを作成
void RenderManager::CreateCopyImagePipelineState()
{
    auto vs = dx_->CompileShader(L"resources/shaders/CopyImage.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompileShader(L"resources/shaders/CopyImage.PS.hlsl", L"ps_6_0");

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
        IID_PPV_ARGS(&copyImagePipelineState_));
    assert(SUCCEEDED(hr));
}

//Copy実行関数
void RenderManager::DrawOffscreenToBackBuffer()
{
    assert(offscreen_);

    auto* cmd = dx_->GetCommandList();

    // RenderTexture を読む状態へ
    dx_->TransitionResource(
        offscreen_->GetResource(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(copyImagePipelineState_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootDescriptorTable(
        0,
        srv_->GetGPUDescriptionHandle(offscreen_->GetSrvIndex())
    );

    cmd->DrawInstanced(3, 1, 0, 0);

    // 次フレームまた RenderTarget として使うので戻す
    dx_->TransitionResource(
        offscreen_->GetResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
}