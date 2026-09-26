#pragma once
#include "Sprite.h"
#include "StageProgress.h"
#include "UIFont.h"

// Sprite-based text also works in Release without ImGui.
class StageClearOverlay {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx) {
        panel_.Initialize(common, dx, "resources/white1x1.png");
        panel_.SetAnchorPoint({.5f,.5f});
        panel_.SetColor({.015f,.025f,.045f,.9f});
        const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        panel_.SetScale({620.0f/static_cast<float>(white.width),360.0f/static_cast<float>(white.height),1});
        font_.Initialize(common, dx, 80);
    }
    void SetResult(const StageProgress& stage, float width, float height) {
        font_.Reset();
        panel_.SetPosition({width*.5f,height*.5f});
        AddLine("STAGE CLEAR", width*.5f, height*.5f-142, 32, {1,.85f,.35f,1});
        AddLine("TIME", width*.5f, height*.5f-65, 16, {.65f,.75f,.85f,1});
        AddLine(StageProgress::FormatTime(stage.Time()), width*.5f, height*.5f-34, 25, {1,1,1,1});
        AddLine("ENEMIES DEFEATED", width*.5f, height*.5f+38, 16, {.65f,.75f,.85f,1});
        AddLine(std::to_string(stage.DefeatedCount()), width*.5f, height*.5f+70, 25, {1,1,1,1});
    }
    void Draw(const Matrix4x4& view, const Matrix4x4& projection) {
        panel_.Update(view, projection);
        panel_.Draw();
        font_.Draw(view, projection);
    }
private:
    void AddLine(const std::string& text, float center, float y, float advance, Vector4 color) {
        font_.DrawText(text, center - static_cast<float>(text.size()) * advance * .5f, y, advance, color);
    }
    Sprite panel_;
    UIFont font_;
};
