#pragma once
#include "Sprite.h"
#include <vector>
#include <string_view>

// Retained glyph sprites: Reset -> DrawText (queue) -> Draw, once per frame.
// size is the horizontal advance in pixels; glyph height is size * 64 / 48.
class UIFont {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx, size_t capacity = 160);
    void Reset() { used_ = 0; }
    void DrawText(std::string_view text, float x, float y, float size, Vector4 color);
    void Draw(const Matrix4x4& view, const Matrix4x4& projection);
private:
    // Each visible character needs its own GPU buffers until the frame completes.
    std::vector<Sprite> glyphs_;
    size_t used_ = 0;
    Vector2 atlasSize_{};
};
