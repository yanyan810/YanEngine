#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSamplerLinear : register(s0);

struct RadialBlurParam {
    float32_t2 center;
    int32_t numSamples;
    float32_t blurWidth;
};

ConstantBuffer<RadialBlurParam> gParam : register(b2);

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    float32_t2 kCenter = gParam.center; // 中心点
    int32_t kNumSamples = gParam.numSamples; // サンプリング数
    float32_t kBlurWidth = gParam.blurWidth; // ぼかしの幅

    float32_t2 direction = input.texcoord - kCenter;
    float32_t3 outputColor = float32_t3(0.0f, 0.0f, 0.0f);

    for (int32_t sampleIndex = 0; sampleIndex < kNumSamples; ++sampleIndex) {
        float32_t2 texcoord = input.texcoord + direction * kBlurWidth * float32_t(sampleIndex);
        outputColor.rgb += gTexture.Sample(gSamplerLinear, texcoord).rgb;
    }

    if (kNumSamples > 0) {
        outputColor.rgb *= rcp(float32_t(kNumSamples));
    } else {
        outputColor = gTexture.Sample(gSamplerLinear, input.texcoord).rgb;
    }

    PixelShaderOutput output;
    output.color.rgb = outputColor;
    output.color.a = 1.0f;
    return output;
}
