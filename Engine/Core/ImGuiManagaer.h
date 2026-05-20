#pragma once
#include <d3d12.h>
#include <cstdint>


#ifdef USE_IMGUI

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#endif // USE_IMGUI


class WinApp;
class DirectXCommon;
class SrvManager;

class ImGuiManagaer {
public:
    void Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);
    void Begin();
    void End(ID3D12GraphicsCommandList* cmd);
    void Shutdown();

private:
    WinApp* winApp_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    bool initialized_ = false;
    uint32_t imguiSrvIndex_ = 0; // SrvManager が 0番をImGui予約してる前提
};
