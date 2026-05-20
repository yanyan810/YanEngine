#pragma once
#include <memory>
#include <wrl.h>
#include <d3d12.h>
#include <array>
#include <string>

#include "OffscreenPass.h"


class DirectXCommon;
class SrvManager;

// ポストエフェクトの種類
enum class PostEffectMode {
    FullScreen = 0,  // コピーのみ（エフェクトなし）
    Grayscale,       // グレースケール
    Vignette,        // ヴィネット
    BoxFilter,       // BoxFilter（スムージング）
    GaussianBlurX,   // Gaussian（水平方向）- 内部処理用
    GaussianBlurY,   // Gaussian（垂直方向）- 内部処理用
    GaussianBlur,    // Gaussian（統合） - UI・外部インターフェース用

    Count            // 種類の数（番兵）
};

class RenderManager {
public:
    void Initialize(DirectXCommon* dx, SrvManager* srv);

    void BeginOffscreen();
    void EndOffscreen();
    void BeginBackBuffer();

    void DrawOffscreenToBackBuffer();

    // ImGui でポストエフェクトを切り替える
    void DrawImGui();

    // 現在のモードを取得・設定
    PostEffectMode GetMode() const { return currentMode_; }
    void SetMode(PostEffectMode mode);
    void SetEffectEnabled(PostEffectMode mode, bool enabled);
    bool IsEffectEnabled(PostEffectMode mode) const;
    void ClearEffects();

    uint32_t GetOffscreenSrvIndex() const;
    OffscreenPass* GetOffscreen() const { return offscreen_.get(); }

private:
    void CreateCopyImageRootSignature();
    // 各エフェクト用のPSOを作成するヘルパー
    void CreatePipelineState(
        const wchar_t* psPath,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO);
    void DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex);
    void DrawFullscreenPassToBackBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource);
    void DrawFullscreenPassToBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource, OffscreenPass& dst);

private:
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    std::unique_ptr<OffscreenPass> offscreen_;
    std::array<std::unique_ptr<OffscreenPass>, 2> postBuffers_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;

    // エフェクトの数だけPSOを持つ
    static constexpr int kEffectCount = static_cast<int>(PostEffectMode::Count);
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kEffectCount> pipelineStates_;

    // 現在選択中のエフェクト
    PostEffectMode currentMode_ = PostEffectMode::FullScreen;
    std::array<bool, kEffectCount> enabledEffects_{};

    // GaussianFilter用の定数バッファ
    struct GaussianFilterParameter {
        float sigma;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> gaussianFilterCB_;
    GaussianFilterParameter* gaussianFilterCBData_ = nullptr;
    float sigma_ = 4.0f;
};
