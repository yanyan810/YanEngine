#pragma once
#include "MathStruct.h"
#include <string>
#include <vector>
#include <format>
#include <filesystem>
#include <fstream>
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "Camera.h"
#include "SkinningCommon.h"
#include "VideoPlayerMF.h"
#include "Animator.h"
#include "Object3dLight.h"

//class Object3dCommon;

class PrimitiveCommon;

class Object3d
{

public:

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Matrix4x4 WorldInverseTranspose;
	};

	struct CameraGPU {
		Vector3 worldPosition;
		float pad; // ★16byte揃え
	};

public:

	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx);
	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom);


	void Update(float dt);

	void Draw();

	void SetModel(Model* model) { this->model_ = model; }

	void SetModel(const std::string& filePath);

	// ===== Transform 用 setter =====
public:
	void SetScale(const Vector3& s) { transform.scale = s; }
	void SetRotate(const Vector3& r) { transform.rotate = r; }
	void SetTranslate(const Vector3& t) { transform.translate = t; }

	// ===== Transform 用 getter =====
	const Vector3& GetScale()     const { return transform.scale; }
	const Vector3& GetRotate()    const { return transform.rotate; }
	const Vector3& GetTranslate() const { return transform.translate; }

	// 光源用セッター・ゲッター (Object3dLightに委譲)
	void SetLightColor(const Vector4& color) { light_->SetDirectionalLightColor(color); }
	void SetDirection(const Vector3& direction) { light_->SetDirectionalLightDirection(direction); }
	void SetIntensity(float intensity) { light_->SetDirectionalLightIntensity(intensity); }

	const Vector4& GetLightColor() const { return light_->GetDirectionalLightColor(); }
	const Vector3& GetDirection() const { return light_->GetDirectionalLightDirection(); }
	float          GetIntensity() const { return light_->GetDirectionalLightIntensity(); }

	void SetEnableLighting(int enable) {
		if (model_ && model_->GetMaterial()) {
			model_->GetMaterial()->enableLighting = enable;
		}
	}
	void SetShininess(float s) {
		if (model_ && model_->GetMaterial()) {
			model_->GetMaterial()->shininess = s;
		}
	}
	int GetEnableLighting() const {
		return (model_ && model_->GetMaterial()) ? model_->GetMaterial()->enableLighting : 0;
	}
	float GetShininess() const {
		return (model_ && model_->GetMaterial()) ? model_->GetMaterial()->shininess : 0.0f;
	}

	//ブレンド設定
	void SetBlendMode(Object3dCommon::BlendMode m) { blendMode_ = m; }

	//色関係
	void SetMaterialColor(const Vector4& c) {
		if (model_) {
			model_->SetMaterialColor(c);
		}
	}
	Vector4 GetMaterialColor() const {
		return model_ ? model_->GetMaterialColor() : Vector4{ 1,1,1,1 };
	}

	//カメラセッター
	void SetCamera(Camera* camera) { camera_ = camera; }

	//ポイントライトセッター
	void SetPointLightColor(const Vector4& c) { light_->SetPointLightColor(c); }
	void SetPointLightPos(const Vector3& p) { light_->SetPointLightPos(p); }
	void SetPointLightIntensity(float i) { light_->SetPointLightIntensity(i); }
	void SetPointLightRadius(float r) { light_->SetPointLightRadius(r); }
	void SetPointLightDecay(float d) { light_->SetPointLightDecay(d); }

	//スポットライトセッター
	void SetSpotLightColor(const Vector4& c) { light_->SetSpotLightColor(c); }
	void SetSpotLightPos(const Vector3& p) { light_->SetSpotLightPos(p); }
	void SetSpotLightIntensity(float i) { light_->SetSpotLightIntensity(i); }
	void SetSpotLightDirection(const Vector3& d) { light_->SetSpotLightDirection(d); }
	void SetSpotLightDistance(float d) { light_->SetSpotLightDistance(d); }
	void SetSpotLightDecay(float d) { light_->SetSpotLightDecay(d); }
	void SetSpotLightCosAngle(float c) { light_->SetSpotLightCosAngle(c); }
	void SetSpotLightCosFalloffStart(float c) { light_->SetSpotLightCosFalloffStart(c); }

	//テクスチャを指定
	void SetTexture(const std::string& path);

	Model* GetModel() const { return model_; }

	void SetIsVisible(bool visible) { isVisible_ = visible; }
	bool GetIsVisible() const { return isVisible_; }

	void SetPrimitiveCommon(PrimitiveCommon* p) { primitiveCommon_ = p; }


	//bool GetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const;


	//Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;

public:

	//環境マップ
	void SetUseEnvironmentMap(bool use) { useEnvironmentMap_ = use; }
	bool GetUseEnvironmentMap() const { return useEnvironmentMap_; }

	void SetEnvironmentTexturePath(const std::string& path) { environmentTexturePath_ = path; }
	const std::string& GetEnvironmentTexturePath() const { return environmentTexturePath_; }

	void SetEnvironmentCoefficient(float v) {
		if (model_ && model_->GetMaterial()) {
			model_->GetMaterial()->environmentCoefficient = v;
		}
	}

	float GetEnvironmentCoefficient() const {
		return (model_ && model_->GetMaterial()) ?
			model_->GetMaterial()->environmentCoefficient : 0.0f;
	}

private:
	//環境マップ
	bool useEnvironmentMap_ = false;
	std::string environmentTexturePath_;

	bool isVisible_=false;

private:

	DirectXCommon* dx_ = nullptr;

	Object3dCommon* object3dCommon = nullptr;

	SkinningCommon* skinningCommon_ = nullptr;

	Model* model_ = nullptr;

	//モデル用のTransformationMatrix用のリソースを作る。Matrix4x4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceModel;/* = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));*/
	//データを書き込む
	TransformationMatrix* transformationMatrixDataModel = nullptr;

	Transform transform;
	Transform cameraTransform;
	//カメラ
	Camera* camera_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraGPU* cameraData_ = nullptr;

	//テクスチャ
	std::string texturePath_ = "";
	bool useOverrideTexture_ = false;
	
	Object3dCommon::BlendMode blendMode_ = Object3dCommon::BlendMode::kBlendModeNormal;
	PrimitiveCommon* primitiveCommon_ = nullptr;

public:
	//=============
	//アニメーション
	//=============

	void PlayAnimation(const std::string& animName = "", bool loop = true) { if(animator_) animator_->PlayAnimation(animName, loop); }

	bool IsAnimationFinished() const { return animator_ ? animator_->IsAnimationFinished() : true; }

	Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;

	const std::string& GetPlayingAnimName() const { static std::string empty; return animator_ ? animator_->GetPlayingAnimName() : empty; }
	void StopAnimation() { if(animator_) animator_->StopAnimation(); }
	void SetAnimationNodeName(const std::string& node) { if(animator_) animator_->SetAnimationNodeName(node); }

	bool HasAnimation() const { return animator_ ? animator_->HasAnimation() : false; }

	//デバッグ用
	void SetDebugDrawBones(bool enable) { debugDrawBones_ = enable; }
	void SetBoneMarkerModel(const std::string& path) { boneMarkerModel_ = path; }

private:
	std::unique_ptr<Animator> animator_;
	std::unique_ptr<Object3dLight> light_;

	bool debugDrawBones_ = false;
	std::string boneMarkerModel_ = "cube/cube.obj";
	std::vector<std::unique_ptr<Object3d>> boneMarkers_;

	SrvManager* srvManager_ = nullptr; // ★参照

private:
	int32_t swordNodeIndex_ = -1;   // ノード index
	uint32_t swordMeshIndex_ = 2;   // 今ログ的に sword は mesh[2]


public:

	//========================
	//video
	//========================

	void SetVideo(VideoPlayerMF* v) { video_ = v; useVideo_ = (v != nullptr); }

	void DrawWithOverrideSrv(const D3D12_GPU_DESCRIPTOR_HANDLE& srv);


private:
	//=======
	//video
	//=======

	bool useVideo_ = false;
	VideoPlayerMF* video_ = nullptr;

};

