#include "UIFont.h"
#include <cctype>

void UIFont::Initialize(SpriteCommon* common, DirectXCommon* dx, size_t capacity) {
    glyphs_.resize(capacity);
    for (auto& glyph : glyphs_) {
        glyph.Initialize(common, dx, "resources/ui/stage_font.png");
        glyph.SetTextureCutSize({48, 64});
    }
    const auto& atlas = TextureManager::GetInstance()->GetMetaData("resources/ui/stage_font.png");
    atlasSize_ = {static_cast<float>(atlas.width), static_cast<float>(atlas.height)};
    Reset();
}

void UIFont::DrawText(std::string_view text, float x, float y, float size, Vector4 color) {
    if (size <= 0) return;
    for (unsigned char character : text) {
        if (used_ == glyphs_.size()) break;
        const int upper = std::toupper(character);
        const int index = upper >= 32 && upper <= 95 ? upper - 32 : '?' - 32;
        auto& glyph = glyphs_[used_++];
        glyph.SetTextureTopLeft({static_cast<float>((index % 16) * 48), static_cast<float>((index / 16) * 64)});
        glyph.SetPosition({x, y});
        // Sprite size is the full atlas, independent of the UV crop.
        glyph.SetScale({size / atlasSize_.x, (size * 64.0f / 48.0f) / atlasSize_.y, 1});
        glyph.SetColor(color);
        x += size;
    }
}

void UIFont::Draw(const Matrix4x4& view, const Matrix4x4& projection) {
    for (size_t i = 0; i < used_; ++i) {
        glyphs_[i].Update(view, projection);
        glyphs_[i].Draw();
    }
}
