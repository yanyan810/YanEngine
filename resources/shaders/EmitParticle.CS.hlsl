struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
};

struct EmitterSphere {
    float3 translate; // 位置
    float radius; // 射出半径
    uint count; // 射出数
    float frequency; // 射出間隔
    float frequencyTime; // 射出間隔調整用時間
    uint emit; // 射出許可
};

ConstantBuffer<EmitterSphere> gEmitter : register(b0);
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);

static const uint kMaxParticles = 1024;

// ====================
// 乱数生成
// ====================
float rand3dTo1d(float3 value, float3 dotDir = float3(12.9898, 78.233, 37.719)) {
    float3 smallValue = sin(value);
    float random = dot(smallValue, dotDir);
    random = frac(sin(random) * 143758.5453);
    return random;
}

float3 rand3dTo3d(float3 value) {
    return float3(
        rand3dTo1d(value, float3(12.989, 78.233, 37.719)),
        rand3dTo1d(value, float3(39.346, 11.135, 83.155)),
        rand3dTo1d(value, float3(73.156, 52.235, 09.151))
    );
}

class RandomGenerator {
    float3 seed;
    float3 Generate3d() {
        seed = rand3dTo3d(seed);
        return seed;
    }
    float Generate1d() {
        float result = rand3dTo1d(seed);
        seed.x = result;
        return result;
    }
};

struct PerFrame {
    float time;
    float deltaTime;
};
ConstantBuffer<PerFrame> gPerFrame : register(b1);
// ====================

// 今回スレッド数は1。複数のEmitterを扱い、同時に処理したいような場合は適宜スレッド数を増やすと良い
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (gEmitter.emit != 0) { // 射出許可が出たので射出
        
        RandomGenerator generator;
        // スレッドIDと時間をSeedにする
        generator.seed = (float3(DTid) + gPerFrame.time) * gPerFrame.time;

        for (uint countIndex = 0; countIndex < gEmitter.count; ++countIndex) {
            int freeListIndex;
            InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);

            if (0 <= freeListIndex && freeListIndex < kMaxParticles) {
                uint particleIndex = gFreeList[freeListIndex];
                
                // カウント分Particleを創出する
                gParticles[particleIndex].scale = generator.Generate3d();
                
                // 0.0~1.0の乱数を -1.0~1.0 に変換し、半径と基準位置を適用する
                float3 randomDir = (generator.Generate3d() - 0.5f) * 2.0f;
                gParticles[particleIndex].translate = gEmitter.translate + (randomDir * gEmitter.radius);
                
                gParticles[particleIndex].color.rgb = generator.Generate3d();
                gParticles[particleIndex].color.a = 1.0f;

                gParticles[particleIndex].velocity = (generator.Generate3d() - 0.5f) * 0.1f;
                gParticles[particleIndex].currentTime = 0.0f;
                gParticles[particleIndex].lifeTime = 1.0f + generator.Generate1d() * 2.0f;
            } else {
                InterlockedAdd(gFreeListIndex[0], 1);
                break;
            }
        }
    }
}
