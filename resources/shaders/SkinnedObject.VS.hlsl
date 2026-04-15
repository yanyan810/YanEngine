#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

ConstantBuffer<TransformationMatrix> gTransformation : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    uint4 index : INDEX0;
};

struct Skinned
{
    float4 position;
    float3 normal;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<Well> gMatrixPalette : register(t0);

//Skinned Skinning(VertexShaderInput input)
//{
//    Skinned skinned;

//    skinned.position = mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
//    skinned.position += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
//    skinned.position += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
//    skinned.position += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
//    skinned.position.w = 1.0f;

//    float3 n0 = mul(input.normal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
//    float3 n1 = mul(input.normal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
//    float3 n2 = mul(input.normal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
//    float3 n3 = mul(input.normal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;

//    skinned.normal = normalize(n0 + n1 + n2 + n3);

//    return skinned;
//}

Skinned Skinning(VertexShaderInput input)
{
    Skinned skinned;

    float4 p = float4(0, 0, 0, 0);

    p += mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    p += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    p += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    p += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;

    p.w = 1.0f;
    skinned.position = p;

    float3 n = 0;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;

    skinned.normal = normalize(n);

    return skinned;
}


VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    Skinned skinned = Skinning(input);

    output.position = mul(skinned.position, gTransformation.WVP);
    output.texcoord = input.texcoord;

    //output.worldPosition = mul(skinned.position, gTransformation.World).xyz;

    float4 wp = mul(skinned.position, gTransformation.World);
    output.worldPosition = wp.xyz;

    
    float3 n = mul(skinned.normal, (float3x3) gTransformation.WorldInverseTranspose);
    output.normal = normalize(n);

    return output;
}

//VertexShaderOutput main(VertexShaderInput input)
//{
//    VertexShaderOutput output;

//    // ★ スキニングしない（元頂点をそのまま使う）
//    float4 localPos = input.position;
//    float3 localNrm = input.normal;

//    output.position = mul(localPos, gTransformation.WVP);
//    output.texcoord = input.texcoord;

//    float4 wp = mul(localPos, gTransformation.World);
//    output.worldPosition = wp.xyz;

//    float3 n = mul(localNrm, (float3x3) gTransformation.WorldInverseTranspose);
//    output.normal = normalize(n);

//    return output;
//}
