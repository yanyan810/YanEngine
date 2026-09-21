#pragma once
#include "Sprite.h"
#include "WeaponSystem.h"
#include <array>
#include <cctype>

// Reuses the existing stage font atlas, with a separate sprite per visible glyph.
class WeaponHUD {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx) {
        panel_.Initialize(common,dx,"resources/white1x1.png");
        panel_.SetColor({.015f,.025f,.045f,.8f});
        const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        panel_.SetScale({480.0f/static_cast<float>(white.width),130.0f/static_cast<float>(white.height),1});
        for (auto& glyph : glyphs_) {
            glyph.Initialize(common,dx,"resources/ui/stage_font.png");
            glyph.SetTextureCutSize({48,64});
        }
    }
    void Draw(const WeaponRuntime& weapon, const WeaponDefinition* nearby, bool error,
        float width, float height, const Matrix4x4& view, const Matrix4x4& projection) {
        used_ = 0;
        panel_.SetPosition({16,height-146});
        panel_.Update(view,projection);
        panel_.Draw();
        const auto& name = weapon.Definition().displayName;
        AddLine(name.empty() ? "NO WEAPON" : name,28,height-136,
            std::min(20.0f,440.0f/static_cast<float>(std::max(size_t{1},name.size()))),{1,1,1,1});
        AddLine(std::to_string(weapon.Magazine())+" / "+std::to_string(weapon.Reserve()),28,height-99,22,{1,.85f,.35f,1});
        AddLine(error ? "WEAPON CONFIG ERROR" : weapon.Reloading() ? "RELOADING" : "R: RELOAD",28,height-60,16,{.75f,.85f,1,1});
        if (nearby) {
            const std::string prompt = "PRESS E TO PICK UP";
            AddLine(prompt,width*.5f-static_cast<float>(prompt.size())*8,height*.70f,16,{1,1,.5f,1});
            const float advance = std::min(20.0f,800.0f/static_cast<float>(nearby->displayName.size()));
            AddLine(nearby->displayName,width*.5f-static_cast<float>(nearby->displayName.size())*advance*.5f,
                height*.70f+30,advance,{1,1,1,1});
        }
        for (size_t i=0; i<used_; ++i) { glyphs_[i].Update(view,projection); glyphs_[i].Draw(); }
    }
private:
    void AddLine(const std::string& text,float x,float y,float advance,Vector4 color) {
        for (unsigned char character : text) {
            if (used_ == glyphs_.size()) break;
            const int upper = std::toupper(character);
            const int index = upper>=32 && upper<=95 ? upper-32 : 31;
            auto& glyph = glyphs_[used_++];
            glyph.SetTextureTopLeft({static_cast<float>((index%16)*48),static_cast<float>((index/16)*64)});
            glyph.SetPosition({x,y});
            glyph.SetScale({advance/768,(advance*64/48)/256,1});
            glyph.SetColor(color);
            x += advance;
        }
    }
    Sprite panel_;
    std::array<Sprite,160> glyphs_;
    size_t used_ = 0;
};
