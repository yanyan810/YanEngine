#pragma once
#include <string>
#include <vector>
#include <cstdint>

#include "Vector3.h"
#include "Matrix4x4.h"
#include "DirectXCommon.h"
#include "TextureManager.h"

#include <wrl.h>
#include <assimp/scene.h>

// t秒時点の Vec3 キーフレーム
struct AnimKeyVec3 {
    float   time = 0.0f;
    Vector3 value{};
};

// t秒時点の 回転（クォータニオン）キーフレーム
struct AnimKeyQuat {
    float time = 0.0f;
    // クォータニオンをまだクラスで持っていなければ、x,y,z,w だけ持つ簡易構造体でOK
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Node {
    std::string name;
    int         parentIndex = -1;
    Matrix4x4   bindLocal;   // aiNode::mTransformation
};

struct Bone {
    std::string name;
    int         parentIndex;
    int         nodeIndex = -1;  // この Bone がぶら下がっている Node
    Matrix4x4   offsetMatrix; // inverse bind pose
};


// 1ボーン分のアニメーション（位置・回転・スケール）
struct BoneAnimeChannel {
    std::string            boneName;
    int                    boneIndex = -1;  // SkinnedModel::bones_ の何番か
    int nodeIndex = -1;   // 対応する Node index（Armature 等も含む）
    std::vector<AnimKeyVec3> posKeys;
    std::vector<AnimKeyQuat> rotKeys;
    std::vector<AnimKeyVec3> scaleKeys;
};

// 1本のアニメクリップ（とりあえず FBX の Animation[0] 用）
struct AnimationClip {
    std::string                   name;
    float                         duration = 0.0f;        // 秒換算
    float                         ticksPerSecond = 0.0f;  // Assimp の値
    std::vector<BoneAnimeChannel>  channels;
};
// ==== 追加ここまで ====

class SkinnedModel {
public:
    // ==== 頂点構造体（ボーン4本まで）====
    struct SkinVertex {
        Vector4 position;
        Vector3 normal;
        Vector2 texCoord;
        uint32_t boneIndex[4];
        float    boneWeight[4];
    };

    // マテリアル（最小限）
    struct MaterialCBData {
        Vector4  color;
        int32_t  enableLighting;
        float    pad[3];
        Matrix4x4 uvTransform;
    };

    // Transform CB
    struct TransformCBData {
        Matrix4x4 worldViewProj;
        Matrix4x4 world;
    };

   
public:
    void Initialize(DirectXCommon* dx, const std::string& filePath);

    void Draw();

    // ==== Transform setter ====
    void SetScale(const Vector3& s) { scale_ = s; }
    void SetRotate(const Vector3& r) { rotate_ = r; }
    void SetTranslate(const Vector3& t) { translate_ = t; }

    // カメラ制御用
    void SetCameraRotate(const Vector3& r) { cameraRotate_ = r; }
    void SetCameraTranslate(const Vector3& t) { cameraTranslate_ = t; }

    void SetDebugBoneRotate(int index, const Vector3& rot);
    const std::vector<Bone>& GetBones() const { return bones_; }
    void SetDebugBoneTranslate(int index, const Vector3& trans);
	void SetDebugBoneScale(int index, const Vector3& scale);

    void UpdateAnimation(float deltaTime);

    Vector3 SamplePosition(const BoneAnimeChannel& ch, float time);

    Matrix4x4::Quat SampleRotation(const BoneAnimeChannel& ch, float time);

    Vector3 SampleScale(const BoneAnimeChannel& ch, float time);


private:
    void LoadFbx_(const std::string& filePath);
    void CreateBuffers_();
    void CreatePipelineIfNeeded_();

private:
    DirectXCommon* dx_ = nullptr;

    // 頂点
    std::vector<SkinVertex> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    // マテリアル
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    MaterialCBData* materialData_ = nullptr;

    // Transform CB
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformCBData* transformData_ = nullptr;

    // Bone 行列 CB
    std::vector<Bone> bones_;
    std::vector<Matrix4x4> boneMatrices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> boneMatrixResource_;
    Matrix4x4* boneMatrixData_ = nullptr;

    //Node
    std::vector<Node> nodes_;                  // シーン全体のノード
    std::unordered_map<std::string, int> nodeNameToIndex_; // name -> nodeIndex

    Matrix4x4 globalInverse_;                 // scene root の逆行列


    //ボーンごとのデバッグ用回転
    std::vector<Vector3>    debugBoneRot_;   // ラジアン
    bool                    debugPoseEnable_ = true;
    std::vector<Vector3>    debugBoneTrans_;
	std::vector<Vector3>    debugBoneScale_;

    // テクスチャ
    std::string texturePath_ = "resources/white1x1.png";


    // ==== アニメーション再生用 ====  ←★ ここから追加
    AnimationClip anime_;       // 今は「1本だけ」扱う
    float         animeTime_ = 0.0f;      // 現在の再生時間[秒]
    bool          animePlaying_ = true;   // 再生中フラグ
    float         animeSpeed_ = 1.0f;     // 再生速度（1.0=等倍）
 

    // Transform 値
    Vector3 scale_{ 1.0f,1.0f,1.0f };
    Vector3 rotate_{ 0.0f,0.0f,0.0f };
    Vector3 translate_{ 0.0f,0.0f,0.0f };

    // カメラ（とりあえず Object3d と同じ値にしておく）
    Vector3 cameraScale_{ 1.0f,1.0f,1.0f };
    Vector3 cameraRotate_{ 0.3f,0.0f,0.0f };
    Vector3 cameraTranslate_{ 0.0f,4.0f,-10.0f };

    // ==== 共有 RootSignature / PSO ====
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> sRootSignature_;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> sPipelineState_;
};
