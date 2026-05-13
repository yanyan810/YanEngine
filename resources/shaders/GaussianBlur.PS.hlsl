#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 3x3カーネルのIndexオフセット（隣接Texelの相対位置）
static const float2 kIndex3x3[3][3] = {
    { { -1.0f, -1.0f }, { 0.0f, -1.0f }, { 1.0f, -1.0f } },
    { { -1.0f,  0.0f }, { 0.0f,  0.0f }, { 1.0f,  0.0f } },
    { { -1.0f,  1.0f }, { 0.0f,  1.0f }, { 1.0f,  1.0f } },
};

// Gaussianカーネル（3x3）：中心が最も重く、離れるほど軽くなる
// 合計 = 1/16 + 2/16 + 1/16 + 2/16 + 4/16 + 2/16 + 1/16 + 2/16 + 1/16 = 16/16 = 1.0
static const float kKernel3x3[3][3] = {
    { 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f },
    { 2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f },
    { 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f },
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    // 1. uvStepSizeを算出（Textureの幅と高さの逆数）
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float)width), rcp((float)height));

    PixelShaderOutput output;
    output.color.rgb = float3(0.0f, 0.0f, 0.0f);
    output.color.a = 1.0f;

    // 2. 3x3ループ（BoxFilterと同じ構造）
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            // 3. 隣接テクセルのUVを計算
            float2 texcoord = input.texcoord + kIndex3x3[x][y] * uvStepSize;
            // 4. Gaussianカーネルの重みで加算
            float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            output.color.rgb += fetchColor * kKernel3x3[x][y];
        }
    }

    return output;
}
