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
		float pad; // 笘・6byte謠・∴
	};

	struct EffectParam {
		// Outline
		Vector4 outlineColor;
		float outlineThickness;
		float enableOutline;
		float pad[2];

		// Dissolve
		Vector4 dissolveEdgeColor;
		float dissolveThreshold;
		float dissolveEdgeWidth;
		float enableDissolve;
		float pad2;

		// Random
		float enableRandom;
		float randomTime;
		float pad3[2];
	};

public:

	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx);
	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom);


	void Update(float dt);

	void Draw();

	void SetModel(Model* model) { this->model_ = model; }

	void SetModel(const std::string& filePath);

	// ===== Transform 逕ｨ setter =====
public:
	void SetScale(const Vector3& s) { transform.scale = s; }
	void SetRotate(const Vector3& r) { transform.rotate = r; }
	void SetTranslate(const Vector3& t) { transform.translate = t; }

	// ===== Transform 逕ｨ getter =====
	const Vector3& GetScale()     const { return transform.scale; }
	const Vector3& GetRotate()    const { return transform.rotate; }
	const Vector3& GetTranslate() const { return transform.translate; }

	// 蜈画ｺ千畑繧ｻ繝・ち繝ｼ繝ｻ繧ｲ繝・ち繝ｼ (Object3dLight縺ｫ蟋碑ｭｲ)
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

	//繝悶Ξ繝ｳ繝芽ｨｭ螳・
	void SetBlendMode(Object3dCommon::BlendMode m) { blendMode_ = m; }

	//濶ｲ髢｢菫・
	void SetMaterialColor(const Vector4& c) {
		if (model_) {
			model_->SetMaterialColor(c);
		}
	}
	Vector4 GetMaterialColor() const {
		return model_ ? model_->GetMaterialColor() : Vector4{ 1,1,1,1 };
	}

	//繧ｫ繝｡繝ｩ繧ｻ繝・ち繝ｼ
	void SetCamera(Camera* camera) { camera_ = camera; }

	//繝昴う繝ｳ繝医Λ繧､繝医そ繝・ち繝ｼ
	void SetPointLightColor(const Vector4& c) { light_->SetPointLightColor(c); }
	void SetPointLightPos(const Vector3& p) { light_->SetPointLightPos(p); }
	void SetPointLightIntensity(float i) { light_->SetPointLightIntensity(i); }
	void SetPointLightRadius(float r) { light_->SetPointLightRadius(r); }
	void SetPointLightDecay(float d) { light_->SetPointLightDecay(d); }

	//繧ｹ繝昴ャ繝医Λ繧､繝医そ繝・ち繝ｼ
	void SetSpotLightColor(const Vector4& c) { light_->SetSpotLightColor(c); }
	void SetSpotLightPos(const Vector3& p) { light_->SetSpotLightPos(p); }
	void SetSpotLightIntensity(float i) { light_->SetSpotLightIntensity(i); }
	void SetSpotLightDirection(const Vector3& d) { light_->SetSpotLightDirection(d); }
	void SetSpotLightDistance(float d) { light_->SetSpotLightDistance(d); }
	void SetSpotLightDecay(float d) { light_->SetSpotLightDecay(d); }
	void SetSpotLightCosAngle(float c) { light_->SetSpotLightCosAngle(c); }
	void SetSpotLightCosFalloffStart(float c) { light_->SetSpotLightCosFalloffStart(c); }

	//繝・け繧ｹ繝√Ε繧呈欠螳・
	void SetTexture(const std::string& path);

	Model* GetModel() const { return model_; }

	void SetIsVisible(bool visible) { isVisible_ = visible; }
	bool GetIsVisible() const { return isVisible_; }

	void SetEnableOutline(bool enable) { enableOutline_ = enable; }
	bool GetEnableOutline() const { return enableOutline_; }
	void SetOutlineColor(const Vector4& color) { outlineColor_ = color; }
	Vector4 GetOutlineColor() const { return outlineColor_; }
	void SetOutlineThickness(float t) { outlineThickness_ = t; }
	float GetOutlineThickness() const { return outlineThickness_; }

	void SetEnableDissolve(bool enable) { enableDissolve_ = enable; }
	bool GetEnableDissolve() const { return enableDissolve_; }
	void SetDissolveThreshold(float t) { dissolveThreshold_ = t; }
	float GetDissolveThreshold() const { return dissolveThreshold_; }
	void SetDissolveEdgeWidth(float w) { dissolveEdgeWidth_ = w; }
	float GetDissolveEdgeWidth() const { return dissolveEdgeWidth_; }
	void SetDissolveEdgeColor(const Vector4& color) { dissolveEdgeColor_ = color; }
	Vector4 GetDissolveEdgeColor() const { return dissolveEdgeColor_; }

	void SetEnableRandom(bool enable) { enableRandom_ = enable; }
	bool GetEnableRandom() const { return enableRandom_; }
	void SetRandomTime(float time) { randomTime_ = time; }

	void SetMaskTexturePath(const std::string& path) { maskTexturePath_ = path; TextureManager::GetInstance()->LoadTexture(path); }

	void SetPrimitiveCommon(PrimitiveCommon* p) { primitiveCommon_ = p; }


	//bool GetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const;


	//Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;

public:

	//迺ｰ蠅・・繝・・
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
	//迺ｰ蠅・・繝・・
	bool useEnvironmentMap_ = false;
	std::string environmentTexturePath_;

	bool isVisible_=false;

private:

	DirectXCommon* dx_ = nullptr;

	Object3dCommon* object3dCommon = nullptr;

	SkinningCommon* skinningCommon_ = nullptr;

	Model* model_ = nullptr;

	//繝｢繝・Ν逕ｨ縺ｮTransformationMatrix逕ｨ縺ｮ繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｽ懊ｋ縲・atrix4x4 荳縺､蛻・・繧ｵ繧､繧ｺ繧堤畑諢上☆繧・
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceModel;/* = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));*/
	//繝・・繧ｿ繧呈嶌縺崎ｾｼ繧
	TransformationMatrix* transformationMatrixDataModel = nullptr;

	Transform transform;
	Transform cameraTransform;
	//繧ｫ繝｡繝ｩ
	Camera* camera_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraGPU* cameraData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> effectParamResource_;
	EffectParam* effectParamData_ = nullptr;
	
	bool enableOutline_ = false;
	Vector4 outlineColor_ = {1.0f, 0.0f, 0.0f, 1.0f}; // Default Red
	float outlineThickness_ = 0.05f;

	bool enableDissolve_ = false;
	float dissolveThreshold_ = 0.5f;
	float dissolveEdgeWidth_ = 0.05f;
	Vector4 dissolveEdgeColor_ = { 1.0f, 0.0f, 0.0f, 1.0f }; // 初期値:赤
	std::string maskTexturePath_ = "resources/noise0.png";

	bool enableRandom_ = false;
	float randomTime_ = 0.0f;

	//繝・け繧ｹ繝√Ε
	std::string texturePath_ = "";
	bool useOverrideTexture_ = false;
	
	Object3dCommon::BlendMode blendMode_ = Object3dCommon::BlendMode::kBlendModeNormal;
	PrimitiveCommon* primitiveCommon_ = nullptr;

public:
	//=============
	//繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ
	//=============

	void PlayAnimation(const std::string& animName = "", bool loop = true) { if(animator_) animator_->PlayAnimation(animName, loop); }

	// 笘・繧ｯ繝ｭ繧ｹ繝輔ぉ繝ｼ繝会ｼ・adeSec遘偵°縺代※ animName 縺ｸ貊代ｉ縺九↓驕ｷ遘ｻ・・
	void CrossFadeTo(const std::string& animName, float fadeSec = 0.2f, bool loop = true) { if(animator_) animator_->CrossFadeTo(animName, fadeSec, loop); }

	bool IsAnimationFinished() const { return animator_ ? animator_->IsAnimationFinished() : true; }
	bool IsFading() const { return animator_ ? animator_->IsFading() : false; }

	Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;
	bool HasJoint(const std::string& jointName) const;

	const std::string& GetPlayingAnimName() const { static std::string empty; return animator_ ? animator_->GetPlayingAnimName() : empty; }
	void StopAnimation() { if(animator_) animator_->StopAnimation(); }
	void SetAnimationNodeName(const std::string& node) { if(animator_) animator_->SetAnimationNodeName(node); }

	bool HasAnimation() const { return animator_ ? animator_->HasAnimation() : false; }

	//繝・ヰ繝・げ逕ｨ
	void SetDebugDrawBones(bool enable) { debugDrawBones_ = enable; }
	void SetBoneMarkerModel(const std::string& path) { boneMarkerModel_ = path; }

private:
	std::unique_ptr<Animator> animator_;
	std::unique_ptr<Object3dLight> light_;

	bool debugDrawBones_ = false;
	std::string boneMarkerModel_ = "cube/cube.obj";
	std::vector<std::unique_ptr<Object3d>> boneMarkers_;

	SrvManager* srvManager_ = nullptr; // 笘・盾辣ｧ

private:
	int32_t swordNodeIndex_ = -1;   // 繝弱・繝・index
	uint32_t swordMeshIndex_ = 2;   // 莉翫Ο繧ｰ逧・↓ sword 縺ｯ mesh[2]


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

