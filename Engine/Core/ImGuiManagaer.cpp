#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"

#ifdef USE_IMGUI
#include <imgui_internal.h>
#endif // USE_IMGUI


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

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Win32 backend（既にあればInitしない）
    if (ImGui::GetIO().BackendPlatformUserData == nullptr) {
        ImGui_ImplWin32_Init(winApp_->GetHwnd());
    }

    // DX12 backend
    ID3D12Device* device = dxCommon_->GetDevice();


    // ★あなたのSwapChain枚数/formatに合わせる（とりあえず2枚+UNORMでOKならこのまま）
    const int backBufferCount = 2;
    const DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = device;
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = backBufferCount;
    initInfo.RTVFormat = rtvFormat;
    initInfo.SrvDescriptorHeap = srvManager_->GetDescriptorHeap();
    initInfo.UserData = srvManager_;
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* srvManager = static_cast<SrvManager*>(info->UserData);
        const uint32_t index = srvManager->Allocate();
        *outCpu = srvManager->GetCPUDescriptionHandle(index);
        *outGpu = srvManager->GetGPUDescriptionHandle(index);
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
    };

    bool ok = ImGui_ImplDX12_Init(&initInfo);
    assert(ok && "ImGui_ImplDX12_Init failed");
    ImGui_ImplDX12_CreateDeviceObjects(); // ★これを追加（重要）

    initialized_ = true;
#endif // USE_IMGUI


}

void ImGuiManagaer::SetSceneTexture(uint32_t srvIndex)
{
    sceneSrvIndex_ = srvIndex;
    hasSceneTexture_ = true;
}

void ImGuiManagaer::Begin()
{
#ifdef USE_IMGUI


    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    BeginDockSpace_();
    DrawEditorPanels_();

#endif // USE_IMGUI


}

void ImGuiManagaer::BeginDockSpace_()
{
#ifdef USE_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("MainDockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("CG5MainDockSpace");
    ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
    BuildDefaultDockLayout_(dockspaceId);
    ImGui::End();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
void ImGuiManagaer::BuildDefaultDockLayout_(ImGuiID dockspaceId)
{
    static bool built = false;
    if (built) {
        return;
    }
    built = true;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID mainNode = dockspaceId;
    ImGuiID leftNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.20f, nullptr, &mainNode);
    ImGuiID rightNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.22f, nullptr, &mainNode);
    ImGuiID bottomNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Down, 0.24f, nullptr, &mainNode);
    ImGuiID rightBottomNode = ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down, 0.45f, nullptr, &rightNode);

    ImGui::DockBuilderDockWindow("Hierarchy", leftNode);
    ImGui::DockBuilderDockWindow("Inspector", rightNode);
    ImGui::DockBuilderDockWindow("Scene", mainNode);
    ImGui::DockBuilderDockWindow("Console", bottomNode);

    ImGui::DockBuilderDockWindow("Sprite Position Control", bottomNode);
    ImGui::DockBuilderDockWindow("Post Effect", bottomNode);
    ImGui::DockBuilderDockWindow("VideoPlane SRT", bottomNode);
    ImGui::DockBuilderDockWindow("Object Specific Effects", bottomNode);
    ImGui::DockBuilderDockWindow("Camera Debug", bottomNode);
    ImGui::DockBuilderDockWindow("Ground PointLight", bottomNode);
    ImGui::DockBuilderDockWindow("Ground SpotLight", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Manager", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Camera", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Test Scene", bottomNode);

    ImGui::DockBuilderDockWindow("Model Switchers", rightBottomNode);
    ImGui::DockBuilderDockWindow("Phong Check", rightBottomNode);
    ImGui::DockBuilderDockWindow("Object SRT (Per-Object)", rightBottomNode);
    ImGui::DockBuilderDockWindow("Primitive Check", rightBottomNode);
    ImGui::DockBuilderDockWindow("Video", rightBottomNode);
    ImGui::DockBuilderDockWindow("Clear", rightBottomNode);
    ImGui::DockBuilderDockWindow("Clear Video", rightBottomNode);
    ImGui::DockBuilderDockWindow("GameOver Video", rightBottomNode);
    ImGui::DockBuilderDockWindow("DebugScene - LevelLoader", rightBottomNode);

    ImGui::DockBuilderFinish(dockspaceId);
}
#endif // USE_IMGUI

void ImGuiManagaer::DrawEditorPanels_()
{
#ifdef USE_IMGUI
    const char* objects[] = {
        "Scene Root",
        "Player",
        "Boss Enemy",
        "Main Camera",
        "Directional Light",
        "Stage"
    };
    constexpr int objectCount = 6;

    ImGui::SetNextWindowSize(ImVec2(220.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(8.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    for (int i = 0; i < objectCount; ++i) {
        if (ImGui::Selectable(objects[i], selectedEditorObject_ == i)) {
            selectedEditorObject_ = i;
        }
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(980.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    ImGui::TextUnformatted(objects[selectedEditorObject_]);
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");
    static float position[3] = { 0.0f, 0.0f, 0.0f };
    static float rotation[3] = { 0.0f, 0.0f, 0.0f };
    static float scale[3] = { 1.0f, 1.0f, 1.0f };
    static bool visible = true;
    ImGui::DragFloat3("Position", position, 0.05f);
    ImGui::DragFloat3("Rotation", rotation, 0.5f);
    ImGui::DragFloat3("Scale", scale, 0.05f);
    ImGui::Separator();
    ImGui::Checkbox("Visible", &visible);
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 160.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(260.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Console");
    ImGui::TextUnformatted("[Yan Editer] Docking ready.");
    ImGui::TextUnformatted("[Build] Debug x64 succeeded.");
    ImGui::TextUnformatted("[Hint] Drag panel tabs to dock them.");
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(360.0f, 220.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(260.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene");
    if (hasSceneTexture_ && srvManager_) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        constexpr float sceneAspect = 1280.0f / 720.0f;
        ImVec2 imageSize = avail;
        if (imageSize.x / imageSize.y > sceneAspect) {
            imageSize.x = imageSize.y * sceneAspect;
        } else {
            imageSize.y = imageSize.x / sceneAspect;
        }

        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(cursor.x + (avail.x - imageSize.x) * 0.5f);
        ImGui::SetCursorPosY(cursor.y + (avail.y - imageSize.y) * 0.5f);

        D3D12_GPU_DESCRIPTOR_HANDLE handle = srvManager_->GetGPUDescriptionHandle(sceneSrvIndex_);
        ImGui::Image(static_cast<ImTextureID>(handle.ptr), imageSize);
    } else {
        ImGui::TextUnformatted("Scene texture is not ready.");
    }
    ImGui::End();
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
