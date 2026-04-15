#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"


void ImGuiManagaer::Initialize([[maybe_unused]]WinApp* winApp, [[maybe_unused]] DirectXCommon* dxCommon, [[maybe_unused]] SrvManager* srvManager)
{
#ifdef USE_IMGUI



    if (initialized_) return;

    winApp_ = winApp;
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    // Context（既にあれば作らない）
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    // Win32 backend（既にあればInitしない）
    if (ImGui::GetIO().BackendPlatformUserData == nullptr) {
        ImGui_ImplWin32_Init(winApp_->GetHwnd());
    }

    // DX12 backend
    ID3D12Device* device = dxCommon_->GetDevice();

    imguiSrvIndex_ = srvManager_->Allocate();

    // ★あなたのSwapChain枚数/formatに合わせる（とりあえず2枚+UNORMでOKならこのまま）
    const int backBufferCount = 2;
    const DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    auto* heap = srvManager_->GetDescriptorHeap();
    auto cpu = srvManager_->GetCPUDescriptionHandle(imguiSrvIndex_);
    auto gpu = srvManager_->GetGPUDescriptionHandle(imguiSrvIndex_);

    bool ok = ImGui_ImplDX12_Init(device, backBufferCount, rtvFormat, heap, cpu, gpu);
    assert(ok && "ImGui_ImplDX12_Init failed");
    ImGui_ImplDX12_CreateDeviceObjects(); // ★これを追加（重要）
    assert(ImGui::GetIO().Fonts->TexID != 0);

    initialized_ = true;
#endif // USE_IMGUI


}

void ImGuiManagaer::Begin()
{
#ifdef USE_IMGUI


    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#endif // USE_IMGUI


}

void ImGuiManagaer::End(ID3D12GraphicsCommandList* cmd)
{
#ifdef USE_IMGUI


    ImGui::Render();

    // ImGui描画前に SRV heap をセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

#endif // USE_IMGUI


}

void ImGuiManagaer::Shutdown()
{

#ifdef USE_IMGUI


    if (!initialized_) return;
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;

#endif // USE_IMGUI


}
