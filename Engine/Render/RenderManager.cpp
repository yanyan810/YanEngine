#include "RenderManager.h"

#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "TextureManager.h"

#include <cassert>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

static const char* kEffectNames[] = {
    "FullScreen (No Effect)",
    "Grayscale",
    "Vignette",
    "BoxFilter (Smoothing)",
    "GaussianBlurX (Horizontal)",
    "GaussianBlurY (Vertical)",
    "GaussianBlur (Linear)",
    "Outline (Depth & Normal)",
    "RadialBlur",
    "Dissolve",
    "Random",
};

static const wchar_t* kEffectPSPaths[] = {
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Grayscale.PS.hlsl",
    L"resources/shaders/Vignette.PS.hlsl",
    L"resources/shaders/BoxFilter.PS.hlsl",
    L"resources/shaders/GaussianBlurX.PS.hlsl",
    L"resources/shaders/GaussianBlurY.PS.hlsl",
    L"resources/shaders/Fullscreen.PS.hlsl", // GaussianBlur髢ｾ・ｪ闖ｴ阮吶・郢昶ぎ郢晄ｺ倥・
    L"resources/shaders/Outline.PS.hlsl",
    L"resources/shaders/RadialBlur.PS.hlsl",
    L"resources/shaders/Dissolve.PS.hlsl",
    L"resources/shaders/Random.PS.hlsl",
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

    // GaussianFilter騾包ｽｨ邵ｺ・ｮ陞ｳ螢ｽ辟夂ｹ晁・繝｣郢晁ｼ斐＜郢ｧ蜑・ｽｽ諛医・
    gaussianFilterCB_ = dx_->CreateBufferResource((sizeof(GaussianFilterParameter) + 0xff) & ~0xff);
    gaussianFilterCB_->Map(0, nullptr, reinterpret_cast<void**>(&gaussianFilterCBData_));
    gaussianFilterCBData_->sigma = sigma_;

    // Outline騾包ｽｨ邵ｺ・ｮ陞ｳ螢ｽ辟夂ｹ晁・繝｣郢晁ｼ斐＜郢ｧ蜑・ｽｽ諛医・
    outlineCB_ = dx_->CreateBufferResource((sizeof(OutlineParameter) + 0xff) & ~0xff);
    outlineCB_->Map(0, nullptr, reinterpret_cast<void**>(&outlineCBData_));
    outlineCBData_->color = outlineColor_;
    outlineCBData_->thickness = outlineThickness_;
    outlineCBData_->threshold = outlineThreshold_;

    // RadialBlur逕ｨ縺ｮ螳壽焚繝舌ャ繝輔ぃ繧剃ｽ懈・
    radialBlurCB_ = dx_->CreateBufferResource((sizeof(RadialBlurParameter) + 0xff) & ~0xff);
    radialBlurCB_->Map(0, nullptr, reinterpret_cast<void**>(&radialBlurCBData_));
    radialBlurCBData_->center = radialBlurCenter_;
    radialBlurCBData_->numSamples = radialBlurNumSamples_;
    radialBlurCBData_->blurWidth = radialBlurWidth_;

    // Dissolve逕ｨ縺ｮ螳壽焚繝舌ャ繝輔ぃ繧剃ｽ懈・
    dissolveCB_ = dx_->CreateBufferResource((sizeof(DissolveParameter) + 0xff) & ~0xff);
    dissolveCB_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveCBData_));
    dissolveCBData_->edgeColor = dissolveEdgeColor_;
    dissolveCBData_->threshold = dissolveThreshold_;
    dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
    dissolveCBData_->backgroundColor = dissolveBackgroundColor_;

    // Random用の定数バッファを作成
    randomCB_ = dx_->CreateBufferResource((sizeof(RandomParameter) + 0xff) & ~0xff);
    randomCB_->Map(0, nullptr, reinterpret_cast<void**>(&randomCBData_));
    randomCBData_->time = 0.0f;

    // ノイズテクスチャのロードとSRVインデックス取得繝弱う繧ｺ繝・け繧ｹ繝√Ε縺ｮ繝ｭ繝ｼ繝峨→SRV繧､繝ｳ繝・ャ繧ｯ繧ｹ蜿門ｾ・
    TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
    noiseSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex("resources/noise0.png");

    // 雎ｺ・ｱ陟趣ｽｦ郢晁・繝｣郢晁ｼ斐＜騾包ｽｨ邵ｺ・ｮSRV郢ｧ蜑・ｽｽ諛医・
    depthSrvIndex_ = srv_->Allocate();
    srv_->CreateSRVTexture2D(depthSrvIndex_, dx_->GetDepthStencilResource(), DXGI_FORMAT_R32_FLOAT, 1);

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
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        return;
    }

    enabledEffects_[index] = enabled;
    currentMode_ = PostEffectMode::FullScreen;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
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
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
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
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0; // t0
    range0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1; // t1
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[7]{};
    // [0]: SRV (t0) Color
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &range0;

    // [1]: CBV (b0) Gaussian Filter
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // [2]: SRV (t1) Depth
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &range1;

    // [3]: CBV (b1) Outline
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[3].Descriptor.ShaderRegister = 1;
    rootParams[3].Descriptor.RegisterSpace = 0;

    // [4]: CBV (b2) RadialBlur
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[4].Descriptor.ShaderRegister = 2;
    rootParams[4].Descriptor.RegisterSpace = 0;

    // [5]: CBV (b3) Dissolve
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[5].Descriptor.ShaderRegister = 3;
    rootParams[5].Descriptor.RegisterSpace = 0;

    // [6]: CBV (b4) Random
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[6].Descriptor.ShaderRegister = 4;
    rootParams[6].Descriptor.RegisterSpace = 0;

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
    // Depth or Mask (t1)
    uint32_t t1SrvIndex = (mode == PostEffectMode::Dissolve) ? noiseSrvIndex_ : depthSrvIndex_;
    cmd->SetGraphicsRootDescriptorTable(2, srv_->GetGPUDescriptionHandle(t1SrvIndex));

    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        cmd->SetGraphicsRootConstantBufferView(1, gaussianFilterCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Outline) {
        cmd->SetGraphicsRootConstantBufferView(3, outlineCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::RadialBlur) {
        cmd->SetGraphicsRootConstantBufferView(4, radialBlurCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Dissolve) {
        cmd->SetGraphicsRootConstantBufferView(5, dissolveCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Random) {
#ifdef USE_IMGUI
        // 時間を更新してセットする
        if (randomCBData_) {
            randomCBData_->time = (float)ImGui::GetTime();
        }
#endif
        cmd->SetGraphicsRootConstantBufferView(6, randomCB_->GetGPUVirtualAddress());
    }

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

    dx_->SetBackBufferRenderTargetForPostEffect();
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

    dst.BeginForPostEffect();
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
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
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
        // 繝昴せ繝医お繝輔ぉ繧ｯ繝亥ｮ御ｺ・ｾ後∝ｾ檎ｶ壹・UI繧・せ繝励Λ繧､繝域緒逕ｻ縺ｮ縺溘ａ縺ｫ騾壼ｸｸ縺ｮRTV+DSV迥ｶ諷九↓謌ｻ縺励※縺翫￥
        dx_->SetBackBufferRenderTarget();
        return;
    }

    // 豺ｱ蠎ｦ繝舌ャ繝輔ぃ繧担RV縺ｨ縺励※隱ｭ縺ｿ霎ｼ繧縺溘ａ縺ｮ繝舌Μ繧｢
    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    ID3D12Resource* srcResource = offscreen_->GetResource();
    uint32_t srcSrvIndex = offscreen_->GetSrvIndex();
    int bufferIndex = 0;

    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (!enabledEffects_[i]) {
            continue;
        }

        const PostEffectMode mode = static_cast<PostEffectMode>(i);
        if (mode == PostEffectMode::GaussianBlur) {
            // Horizontal (X)
            OffscreenPass& dstX = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurX, srcSrvIndex, srcResource, dstX);
            srcResource = dstX.GetResource();
            srcSrvIndex = dstX.GetSrvIndex();
            bufferIndex = 1 - bufferIndex;

            // Vertical (Y)
            if (i == lastEffect) {
                DrawFullscreenPassToBackBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource);
            } else {
                OffscreenPass& dstY = *postBuffers_[bufferIndex];
                DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource, dstY);
                srcResource = dstY.GetResource();
                srcSrvIndex = dstY.GetSrvIndex();
                bufferIndex = 1 - bufferIndex;
            }
        } else {
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

    // 豺ｱ蠎ｦ繝舌ャ繝輔ぃ繧貞・縺ｮ迥ｶ諷具ｼ域嶌縺崎ｾｼ縺ｿ逕ｨ・峨↓謌ｻ縺・
    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    // 繝昴せ繝医お繝輔ぉ繧ｯ繝亥ｮ御ｺ・ｾ後∝ｾ檎ｶ壹・UI繧・せ繝励Λ繧､繝域緒逕ｻ縺ｮ縺溘ａ縺ｫ騾壼ｸｸ縺ｮRTV+DSV迥ｶ諷九↓謌ｻ縺励※縺翫￥
    dx_->SetBackBufferRenderTarget();
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
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        bool enabled = enabledEffects_[i];
        if (ImGui::Checkbox(kEffectNames[i], &enabled)) {
            SetEffectEnabled(static_cast<PostEffectMode>(i), enabled);
        }

        if (static_cast<PostEffectMode>(i) == PostEffectMode::GaussianBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("Sigma", &sigma_, 0.1f, 10.0f)) {
                gaussianFilterCBData_->sigma = sigma_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::Outline && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("Color", &outlineColor_.x)) {
                outlineCBData_->color = outlineColor_;
            }
            if (ImGui::SliderFloat("Thickness", &outlineThickness_, 0.1f, 10.0f)) {
                outlineCBData_->thickness = outlineThickness_;
            }
            if (ImGui::SliderFloat("Threshold", &outlineThreshold_, 0.0f, 1.0f)) {
                outlineCBData_->threshold = outlineThreshold_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::RadialBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat2("Center", &radialBlurCenter_.x, 0.0f, 1.0f)) {
                radialBlurCBData_->center = radialBlurCenter_;
            }
            if (ImGui::SliderInt("NumSamples", &radialBlurNumSamples_, 0, 50)) {
                radialBlurCBData_->numSamples = radialBlurNumSamples_;
            }
            if (ImGui::SliderFloat("BlurWidth", &radialBlurWidth_, 0.0f, 0.1f)) {
                radialBlurCBData_->blurWidth = radialBlurWidth_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::Dissolve && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("EdgeColor", &dissolveEdgeColor_.x)) {
                dissolveCBData_->edgeColor = dissolveEdgeColor_;
            }
            if (ImGui::SliderFloat("Threshold", &dissolveThreshold_, 0.0f, 1.0f)) {
                dissolveCBData_->threshold = dissolveThreshold_;
            }
            if (ImGui::SliderFloat("EdgeWidth", &dissolveEdgeWidth_, 0.0f, 0.2f)) {
                dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
            }
            if (ImGui::ColorEdit4("BackgroundColor", &dissolveBackgroundColor_.x)) {
                dissolveCBData_->backgroundColor = dissolveBackgroundColor_;
            }
            ImGui::Unindent();
        }
    }

    ImGui::Separator();
    ImGui::Text("Current Chain:");
    bool any = false;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
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
