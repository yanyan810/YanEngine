#pragma once
#include <memory>
#include <wrl.h>
#include <d3d12.h>

#include "OffscreenPass.h"


class DirectXCommon;
class SrvManager;

class RenderManager {
public:
    void Initialize(DirectXCommon* dx, SrvManager* srv);

    void BeginOffscreen();
    void EndOffscreen();
    void BeginBackBuffer();

    void DrawOffscreenToBackBuffer();

    uint32_t GetOffscreenSrvIndex() const;
    OffscreenPass* GetOffscreen() const { return offscreen_.get(); }

private:
    void CreateCopyImageRootSignature();
    void CreateCopyImagePipelineState();

private:
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    std::unique_ptr<OffscreenPass> offscreen_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copyImagePipelineState_;
};