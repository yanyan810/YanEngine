#include "Object3d.hlsli"

// =====================
// Constant Buffers
// =====================
struct Material
{
    float4 color;
    int enableLighting;
    float3 _pad0;

    float4x4 uvTransform;

    float shininess;
    float3 _pad1;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct Camera
{
    float3 worldPosition;
    float _pad;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 _pad;
};

struct SpotLight
{
    float4 color; // 色
    float3 position; // 位置
    float intensity;

    float3 direction; // 向き（光源→照射方向）
    float distance; // 最大距離

    float decay; // 距離減衰
    float cosAngle; // 外側（終端）
    float cosFalloffStart; // 内側（100%）
};

// =====================
// Resources
// =====================
Texture2D gTexture : register(t1);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);

// =====================
// Pixel Shader
// =====================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV
    float4 uv = mul(float4(input.texcoord, 0, 1), gMaterial.uvTransform);
    float4 tex = gTexture.Sample(gSampler, uv.xy);

    //if (tex.a == 0.0f)
    //    discard;

    output.color = gMaterial.color * tex;

    if (gMaterial.enableLighting == 0)
        return output;

    // =====================
    // Common vectors
    // =====================
    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);

    // =====================
    // Directional Light
    // =====================
    float3 Ld = normalize(-gDirectionalLight.direction);
    float NdotLd = dot(N, Ld);

    float diffD =
        (gMaterial.enableLighting == 2)
        ? pow(NdotLd * 0.5f + 0.5f, 2.0f)
        : saturate(NdotLd);

    float3 diffuseD =
        gMaterial.color.rgb * tex.rgb *
        gDirectionalLight.color.rgb *
        diffD * gDirectionalLight.intensity;

    float3 Hd = normalize(Ld + V);
    float specD = pow(saturate(dot(N, Hd)), max(gMaterial.shininess, 1.0f));
    float3 specularD = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specD;

    // =====================
    // Point Light
    // =====================
    float3 toP = gPointLight.position - input.worldPosition;
    float distP = max(length(toP), 0.001f);
    float3 Lp = toP / distP;

    float t = saturate(1.0f - distP / max(gPointLight.radius, 0.001f));
    float attenP = pow(t, gPointLight.decay);

    float diffP =
        (gMaterial.enableLighting == 2)
        ? pow(dot(N, Lp) * 0.5f + 0.5f, 2.0f)
        : saturate(dot(N, Lp));

    float3 pointCol = gPointLight.color.rgb * gPointLight.intensity * attenP;

    float3 diffuseP = gMaterial.color.rgb * tex.rgb * pointCol * diffP;

    float3 Hp = normalize(Lp + V);
    float specP = pow(saturate(dot(N, Hp)), max(gMaterial.shininess, 1.0f));
    float3 specularP = pointCol * specP;

    // =====================
    // Spot Light（FalloffStart 対応）
    // =====================
    float3 toS = gSpotLight.position - input.worldPosition;
    float distS = max(length(toS), 0.001f);
    float3 Ls = toS / distS;

    // 距離減衰
    float ts = saturate(1.0f - distS / max(gSpotLight.distance, 0.001f));
    float attenS = pow(ts, gSpotLight.decay);

    // 角度計算
    float3 dirOnSurface = normalize(input.worldPosition - gSpotLight.position);
    float3 spotAxis = normalize(gSpotLight.direction);
    float cosTheta = dot(dirOnSurface, spotAxis);

    // Falloff（スライド通り）
    float denom = max(gSpotLight.cosFalloffStart - gSpotLight.cosAngle, 1e-6f);
    float falloff = saturate((cosTheta - gSpotLight.cosAngle) / denom);

    float diffS =
        (gMaterial.enableLighting == 2)
        ? pow(dot(N, Ls) * 0.5f + 0.5f, 2.0f)
        : saturate(dot(N, Ls));

    float3 spotCol =
        gSpotLight.color.rgb *
        gSpotLight.intensity *
        attenS *
        falloff;

    float3 diffuseS = gMaterial.color.rgb * tex.rgb * spotCol * diffS;

    float3 Hs = normalize(Ls + V);
    float specS = pow(saturate(dot(N, Hs)), max(gMaterial.shininess, 1.0f));
    float3 specularS = spotCol * specS;

    // =====================
    // Debug View
    // =====================
    if (gMaterial.enableLighting == 11)
    {
        output.color.rgb = diffuseD + specularD;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 12)
    {
        output.color.rgb = diffuseP + specularP;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 13)
    {
        output.color.rgb = diffuseS + specularS;
        output.color.a = 1;
        return output;
    }

    // =====================
    // Final
    // =====================
    output.color.rgb =
        diffuseD + specularD +
        diffuseP + specularP +
        diffuseS + specularS;

    output.color.a = gMaterial.color.a * tex.a;
    return output;
}
