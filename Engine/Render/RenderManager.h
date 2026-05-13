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
    GaussianBlur,    // Gaussian（線形フィルタ）

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
    void SetMode(PostEffectMode mode) { currentMode_ = mode; }

    uint32_t GetOffscreenSrvIndex() const;
    OffscreenPass* GetOffscreen() const { return offscreen_.get(); }

private:
    void CreateCopyImageRootSignature();
    // 各エフェクト用のPSOを作成するヘルパー
    void CreatePipelineState(
        const wchar_t* psPath,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO);

private:
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    std::unique_ptr<OffscreenPass> offscreen_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;

    // エフェクトの数だけPSOを持つ
    static constexpr int kEffectCount = static_cast<int>(PostEffectMode::Count);
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kEffectCount> pipelineStates_;

    // 現在選択中のエフェクト
    PostEffectMode currentMode_ = PostEffectMode::FullScreen;
};