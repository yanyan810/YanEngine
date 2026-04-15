#include "SkinningCommon.h"
#include <cassert>

void SkinningCommon::Initialize(DirectXCommon* dxCommon)
{
    dx_ = dxCommon;
    CreateGraphicsPipelineState();
}

void SkinningCommon::CreateRootSignature()
{
    // ==== DescriptorRange: Palette (VS t0) ====
    D3D12_DESCRIPTOR_RANGE rangePalette{};
    rangePalette.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangePalette.NumDescriptors = 1;
    rangePalette.BaseShaderRegister = 0; // t0
    rangePalette.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ==== DescriptorRange: Texture (PS t1) ====
    D3D12_DESCRIPTOR_RANGE rangeTexture{};
    rangeTexture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeTexture.NumDescriptors = 1;
    rangeTexture.BaseShaderRegister = 1; // ★t1 にする（PS側も t1 に修正済みが前提）
    rangeTexture.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[8]{};

    // (0) Pixel: CBV b0 Material
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    // (1) Vertex: CBV b0 Transform
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;

    // (2) Vertex: SRV Table t0 Palette
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &rangePalette;

    // (3) Pixel: SRV Table t1 Texture
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &rangeTexture;

    // (4) Pixel: CBV b1 DirectionalLight
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].Descriptor.ShaderRegister = 1;

    // (5) Pixel: CBV b2 Camera
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].Descriptor.ShaderRegister = 2;

    // (6) Pixel: CBV b3 PointLight
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[6].Descriptor.ShaderRegister = 3;

    // (7) Pixel: CBV b4 SpotLight
    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[7].Descriptor.ShaderRegister = 4;

    // ==== Static Sampler s0 ====
    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &samp;

    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
    if (sig) sig->Release();
    if (err) err->Release();
}

void SkinningCommon::CreateGraphicsPipelineState()
{
    CreateRootSignature();

    auto vs = dx_->CompilesSharder(L"resources/shaders/SkinnedObject.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompilesSharder(L"resources/shaders/Object3D.PS.hlsl", L"ps_6_0");

    // Blend/Rasterizer/Depth は Object3dCommon と同じでOK（省略してもいいが、ここはあなたのをコピペでOK）
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // ...（Object3dCommon の switch(blendMode_) を丸ごとコピペ）

    D3D12_RASTERIZER_DESC rast{};
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = inputLayout_;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState = blend;
    psoDesc.RasterizerState = rast;
    psoDesc.DepthStencilState = ds;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    assert(SUCCEEDED(hr));
}

void SkinningCommon::SetGraphicsPipelineState()
{
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
