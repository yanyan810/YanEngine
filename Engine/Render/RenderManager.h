#pragma once

#include <array>
#include <memory>
#include <string>
#include <wrl.h>
#include <d3d12.h>

#include "OffscreenPass.h"

class DirectXCommon;
class SrvManager;

enum class PostEffectMode {
    FullScreen = 0,
    Grayscale,
    Vignette,
    BoxFilter,
    GaussianBlurX,
    GaussianBlurY,
    GaussianBlur,
    Outline,
    RadialBlur,
    Dissolve,
    Random,
    OutlineBloom,

    Count
};

class RenderManager {
public:
    void Initialize(DirectXCommon* dx, SrvManager* srv);

    void BeginOffscreen();
    void EndOffscreen();
    void BeginBackBuffer();
    void BeginParticlePostLayer(PostEffectMode mode);
    void EndParticlePostLayer();
    void ClearParticlePostLayer();

    void DrawOffscreenToBackBuffer();
    uint32_t RenderPostEffectsForSceneTexture();

    void DrawImGui();

    PostEffectMode GetMode() const { return currentMode_; }
    void SetMode(PostEffectMode mode);
    void SetEffectEnabled(PostEffectMode mode, bool enabled);
    bool IsEffectEnabled(PostEffectMode mode) const;
    void ClearEffects();

    uint32_t GetOffscreenSrvIndex() const;
    OffscreenPass* GetOffscreen() const { return offscreen_.get(); }

private:
    void CreateCopyImageRootSignature();
    void CreatePipelineState(
        const wchar_t* psPath,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO);
    void DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex);
    void DrawFullscreenPassToBackBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource);
    void DrawFullscreenPassToBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource, OffscreenPass& dst);
    void DrawAdditiveCompositePass(uint32_t baseSrvIndex, uint32_t addSrvIndex);
    int FindLastEnabledPostEffect_() const;
    uint32_t RenderPostEffectsToBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex);
    void RenderPostEffectsToBackBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex);
    uint32_t CompositeParticlePostToBuffer_(uint32_t baseSrvIndex);
    void CompositeParticlePostToBackBuffer_(uint32_t baseSrvIndex);

private:
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    std::unique_ptr<OffscreenPass> offscreen_;
    std::array<std::unique_ptr<OffscreenPass>, 2> postBuffers_;
    std::unique_ptr<OffscreenPass> particlePostLayer_;
    std::unique_ptr<OffscreenPass> particlePostBuffer_;
    std::unique_ptr<OffscreenPass> compositeBuffer_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> additiveCompositePSO_;

    static constexpr int kEffectCount = static_cast<int>(PostEffectMode::Count);
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kEffectCount> pipelineStates_;

    PostEffectMode currentMode_ = PostEffectMode::FullScreen;
    std::array<bool, kEffectCount> enabledEffects_{};

    struct GaussianFilterParameter {
        float sigma;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> gaussianFilterCB_;
    GaussianFilterParameter* gaussianFilterCBData_ = nullptr;
    float sigma_ = 4.0f;

    struct OutlineParameter {
        Vector4 color;
        float thickness;
        float threshold;
        float _pad[2];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> outlineCB_;
    OutlineParameter* outlineCBData_ = nullptr;

    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    float outlineThickness_ = 1.0f;
    float outlineThreshold_ = 0.5f;

    struct RadialBlurParameter {
        Vector2 center;
        int32_t numSamples;
        float blurWidth;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> radialBlurCB_;
    RadialBlurParameter* radialBlurCBData_ = nullptr;

    Vector2 radialBlurCenter_ = { 0.5f, 0.5f };
    int32_t radialBlurNumSamples_ = 10;
    float radialBlurWidth_ = 0.01f;

    struct DissolveParameter {
        Vector4 edgeColor;
        float threshold;
        float edgeWidth;
        float _pad[2];
        Vector4 backgroundColor;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveCB_;
    DissolveParameter* dissolveCBData_ = nullptr;

    Vector4 dissolveEdgeColor_ = { 1.0f, 0.0f, 0.0f, 1.0f };
    float dissolveThreshold_ = 0.5f;
    float dissolveEdgeWidth_ = 0.05f;
    Vector4 dissolveBackgroundColor_ = { 0.0f, 1.0f, 0.0f, 1.0f };

    struct RandomParameter {
        float time;
        float _pad[3];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> randomCB_;
    RandomParameter* randomCBData_ = nullptr;

    struct BloomParameter {
        Vector4 color;
        float intensity;
        float threshold;
        float alpha;
        float _pad;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomCB_;
    BloomParameter* bloomCBData_ = nullptr;

    Vector4 bloomColor_ = { 1.0f, 0.72f, 0.22f, 1.0f };
    float bloomIntensity_ = 0.9f;
    float bloomThreshold_ = 0.62f;
    float bloomAlpha_ = 1.0f;

    uint32_t noiseSrvIndex_ = 0;
    uint32_t previewSrvIndex_ = 0;
    uint32_t depthSrvIndex_ = 0;
    bool hasParticlePostLayer_ = false;
    PostEffectMode particlePostEffectMode_ = PostEffectMode::FullScreen;
};
