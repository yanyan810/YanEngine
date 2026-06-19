#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer BloomParameter : register(b5)
{
    float4 bloomColor;
    float bloomIntensity;
    float bloomThreshold;
    float bloomAlpha;
    float bloomPad;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float3 ExtractBright(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float threshold = saturate(bloomThreshold);
    float factor = saturate((brightness - threshold) / max(1.0f - threshold, 0.001f));
    return color * factor;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);

    float2 texel = float2(rcp((float)width), rcp((float)height));
    float bloomMaskSum = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int y = -4; y <= 4; ++y) {
        [unroll]
        for (int x = -4; x <= 4; ++x) {
            float2 offset = float2((float)x, (float)y);
            float dist2 = dot(offset, offset);
            float weight = exp(-dist2 / 16.0f);
            
            float4 sampleColor = gTexture.Sample(gSampler, input.texcoord + offset * texel * 4.0f);
            
            // アルファ値（オブジェクトの存在）と輝度値を考慮したマスク
            float brightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
            float mask = sampleColor.a * (0.5f + 0.5f * brightness);
            
            bloomMaskSum += mask * weight;
            totalWeight += weight;
        }
    }

    float blurredMask = bloomMaskSum / max(totalWeight, 0.001f);

    // ぼかしたマスク値にブルームカラーと強度を乗算して発光色を決定
    float3 glow = bloomColor.rgb * blurredMask * bloomIntensity * bloomAlpha * bloomColor.a;
    
    // アルファマスク
    float outAlpha = saturate(blurredMask * bloomAlpha * bloomColor.a);

    PixelShaderOutput output;
    output.color = float4(glow, outAlpha);
    return output;
}
