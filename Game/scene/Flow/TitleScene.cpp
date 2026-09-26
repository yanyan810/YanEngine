#include "TitleScene.h"
#include "GameApp.h"
#include <algorithm>
#include "WinApp.h"

namespace {
    constexpr const char* kTitleImagePath = "resources/ui/char/title.png";
    constexpr const char* kPressSpacePath = "resources/ui/char/pressSpace.png";
}

void TitleScene::OnEnter(GameApp& app) {
    auto& input = *app.GetInput();
    input.SetCameraControlEnabled(false);
    input.SetCameraToggleKeyEnabled(false);
    input.SetMouseCaptureRect(nullptr);

    const float width = static_cast<float>(WinApp::kClientWidth);
    const float height = static_cast<float>(WinApp::kClientHeight);
    uiView_ = Matrix4x4::MakeIdentity4x4();
    uiProjection_ = Matrix4x4::MakeOrthographicMatrix(0, 0, width, height, 0, 100);
    background_.Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    background_.SetPosition({0, 0});
    background_.SetColor({0, 0, 0, 1});
    const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    background_.SetScale({width / static_cast<float>(white.width), height / static_cast<float>(white.height), 1});
    background_.Update(uiView_, uiProjection_);

    logo_.Initialize(app.SpriteCom(), app.Dx(), kTitleImagePath);
    logo_.SetAnchorPoint({.5f, .5f});
    logo_.SetPosition({width * .5f, height * .5f});
    logo_.SetColor({1, 1, 1, 1});
    // Preserve the supplied image's layout and aspect ratio, including its black background.
    const auto& title = TextureManager::GetInstance()->GetMetaData(kTitleImagePath);
    const float titleScale = std::min(width / static_cast<float>(title.width),
        height / static_cast<float>(title.height));
    logo_.SetScale({titleScale, titleScale, 1});
    logo_.Update(uiView_, uiProjection_);

    pressSpace_.Initialize(app.SpriteCom(), app.Dx(), kPressSpacePath);
    pressSpace_.SetAnchorPoint({.5f, .5f});
    pressSpace_.SetPosition({width * .5f, height * .84f});
    // Crop only the unused black margins of the supplied 1280x720 image, without editing it.
    pressSpace_.SetTextureTopLeft({280, 450});
    pressSpace_.SetTextureCutSize({700, 150});
    const auto& prompt = TextureManager::GetInstance()->GetMetaData(kPressSpacePath);
    const float promptWidth = width * .32f;
    pressSpace_.SetScale({promptWidth / static_cast<float>(prompt.width),
        (promptWidth * 150.0f / 700.0f) / static_cast<float>(prompt.height), 1});
    pressSpace_.Update(uiView_, uiProjection_);
}

void TitleScene::OnExit(GameApp& app) {
    app.GetInput()->SetCameraControlEnabled(false);
    app.GetInput()->SetMouseCaptureRect(nullptr);
    app.GetInput()->SetCameraToggleKeyEnabled(true);
}

void TitleScene::Update(GameApp& app, float) {
    const auto& input = *app.GetInput();
    if (!input.HasFocus() || !NextScene().empty()) return;
    if (input.IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }
    if (input.IsKeyTrigger(DIK_SPACE)) RequestChangeScene_("Game");
}

void TitleScene::DrawRender(GameApp&) {
    // A scene-local background leaves the renderer's Game/Showroom clear color untouched.
    background_.Draw();
}

void TitleScene::DrawOverlay2D(GameApp&) {
    // This path is used by both the Debug scene viewport and the Release back buffer.
    logo_.Draw();
    pressSpace_.Draw();
}
