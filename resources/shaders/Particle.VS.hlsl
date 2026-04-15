#include "Particle.hlsli"

// C++ 側と同じレイアウト
struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
};

// instancing 用：StructuredBuffer を SRV(t0) で受け取る
StructuredBuffer<ParticleForGPU> gParticle : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint instanceId : SV_InstanceID; // ← 追加
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // InstanceID から自分の Transform を取ってくる
    ParticleForGPU tm = gParticle[input.instanceId];

    output.position = mul(input.position, tm.WVP);
    output.texcoord = input.texcoord;
    output.color = gParticle[input.instanceId].color;
    //output.normal = normalize(mul(input.normal, (float3x3) tm.World));

    return output;
}
