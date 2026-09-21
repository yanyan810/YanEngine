#pragma once
#include "Sprite.h"
#include "StageProgress.h"
#include <array>

// Sprite-based text also works in Release without ImGui.
class StageClearOverlay {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx) {
        panel_.Initialize(common, dx, "resources/white1x1.png");
        panel_.SetAnchorPoint({.5f,.5f});
        panel_.SetColor({.015f,.025f,.045f,.9f});
        const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        panel_.SetScale({620.0f/static_cast<float>(white.width),360.0f/static_cast<float>(white.height),1});
        for (auto& glyph : glyphs_) {
            glyph.Initialize(common, dx, "resources/ui/stage_font.png");
            glyph.SetTextureCutSize({48,64});
        }
    }
    void SetResult(const StageProgress& stage, float width, float height) {
        used_ = 0;
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
        for (size_t i = 0; i < used_; ++i) {
            glyphs_[i].Update(view, projection);
            glyphs_[i].Draw();
        }
    }
private:
    void AddLine(const std::string& text, float center, float y, float advance, Vector4 color) {
        float x = center - static_cast<float>(text.size())*advance*.5f;
        for (unsigned char character : text) {
            if (used_ >= glyphs_.size()) return;
            const int index = character >= 32 && character <= 95 ? character - 32 : 0;
            auto& glyph = glyphs_[used_++];
            glyph.SetTextureTopLeft({static_cast<float>((index%16)*48),static_cast<float>((index/16)*64)});
            // Sprite's base size is the full 768x256 atlas, independent of UV crop.
            glyph.SetScale({advance/768.0f,(advance*64.0f/48.0f)/256.0f,1});
            glyph.SetPosition({x,y});
            glyph.SetColor(color);
            x += advance;
        }
    }
    Sprite panel_;
    std::array<Sprite,80> glyphs_;
    size_t used_ = 0;
};
