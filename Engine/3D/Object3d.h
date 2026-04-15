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
#include "AnimationEvaluate.h"
#include <array>
#include <span>
#include "SrvManager.h"
#include "SkinningTypes.h"
#include "SkinningCommon.h"
#include "VideoPlayerMF.h"

//class Object3dCommon;

class Object3d
{

public:

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Matrix4x4 WorldInverseTranspose;
	};

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	struct CameraGPU {
		Vector3 worldPosition;
		float pad; // ★16byte揃え
	};


	struct PointLight {
		Vector4 color;//!<ライトの色
		Vector3 position;//!<ライトの位置
		float intensity;//!<輝度
		float radius;//!<ライトの届く最大距離
		float decay;//!<減衰率
		float padding[2];
	};

	struct SpotLight {

		Vector4 color;//!< ライトの色
		Vector3 position;//!< ライトの位置
		float intensity;//!< 輝度
		Vector3 direction;//!< ライトの向き
		float distance;//!< ライトの届く最大距離
		float decay;//!< 減衰率
		float cosAngle;//!スポットライトの余弦
		float cosFalloffStart;
		float padding[2];


	};

	//struct WellForGPU {
	//	Matrix4x4 skeletonSpaceMatrix;
	//	Matrix4x4 skeletonSpaceInverseTransposeMatrix;
	//};

	//struct SkinCluster {
	//	// ---- inverse bind (jointIndex順) ----
	//	std::vector<Matrix4x4> inverseBindPoseMatrices;

	//	// ---- influence (vertexCount分) ----
	//	Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
	//	D3D12_VERTEX_BUFFER_VIEW influenceBufferView;
	//	std::span<Model::VertexInfluence> mappedInfluence;
	//	
	//	// ---- palette (jointCount分) ----
	//	Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
	//	std::span<WellForGPU> mappedPalette;
	//	std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle{};
	//};




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

	//光源用
	void SetLightColor(const Vector4& color) { if (directionalLightData) directionalLightData->color = color; }

	void SetDirection(const Vector3& direction);

	void SetIntensity(float intensity) { if (directionalLightData) directionalLightData->intensity = intensity; }

	// 光源 getter（正しく返すように修正）
	const Vector4& GetLightColor()     const { return directionalLightData->color; }
	const Vector3& GetDirection() const { return directionalLightData->direction; }
	float          GetIntensity() const { return directionalLightData->intensity; }

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
	void SetBlendMode(Object3dCommon::BlendMode m) { object3dCommon->SetBlendMode(m); }

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
	void SetPointLightColor(const Vector4& c) { if (pointLightData_) pointLightData_->color = c; }
	void SetPointLightPos(const Vector3& p) { if (pointLightData_) pointLightData_->position = p; }
	void SetPointLightIntensity(float i) { if (pointLightData_) pointLightData_->intensity = i; }
	void SetPointLightRadius(float r) { if (pointLightData_) pointLightData_->radius = r; }
	void SetPointLightDecay(float d) { if (pointLightData_) pointLightData_->decay = d; }

	//スポットライトセッター
	void SetSpotLightColor(const Vector4& c) { if (spotLightData_) spotLightData_->color = c; }
	void SetSpotLightPos(const Vector3& p) { if (spotLightData_) spotLightData_->position = p; }
	void SetSpotLightIntensity(float i) { if (spotLightData_) spotLightData_->intensity = i; }
	void SetSpotLightDirection(const Vector3& d) { if (spotLightData_) spotLightData_->direction = d; } // 正規化推奨
	void SetSpotLightDistance(float d) { if (spotLightData_) spotLightData_->distance = d; }
	void SetSpotLightDecay(float d) { if (spotLightData_) spotLightData_->decay = d; }
	void SetSpotLightCosAngle(float c) { if (spotLightData_) spotLightData_->cosAngle = c; } // cos(角度)

	void SetSpotLightCosFalloffStart(float c) {
		if (spotLightData_) spotLightData_->cosFalloffStart = c;
	}


	Model* GetModel() const { return model_; }


	bool GetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const;


	Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;


private:

	DirectXCommon* dx_ = nullptr;

	Object3dCommon* object3dCommon = nullptr;

	SkinningCommon* skinningCommon_ = nullptr;

	Model* model_ = nullptr;

	Model::Skeleton poseSkeleton_; // ★再生用（modelのskeletonをコピーして使う）

	//モデル用のTransformationMatrix用のリソースを作る。Matrix4x4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceModel;/* = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));*/
	//データを書き込む
	TransformationMatrix* transformationMatrixDataModel = nullptr;

	//ライトのリソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource;
	DirectionalLight* directionalLightData = nullptr;

	Transform transform;
	Transform cameraTransform;
	//カメラ
	Camera* camera_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraGPU* cameraData_ = nullptr;

	// 点光源（CB）
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	PointLight* pointLightData_ = nullptr;

	// スポットライト（CB）
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	SpotLight* spotLightData_ = nullptr;


public:
	//=============
	//アニメーション
	//=============

	void PlayAnimation(const std::string& animName = "", bool loop = true);

	bool IsAnimationFinished() const;
	const std::string& GetPlayingAnimName() const;
	void StopAnimation() { isPlayAnimation_ = false; }
	void SetAnimationNodeName(const std::string& node) { playingNodeName_ = node; }

	bool HasAnimation() const;

	//デバッグ用
	void SetDebugDrawBones(bool enable) { debugDrawBones_ = enable; }
	void SetBoneMarkerModel(const std::string& path) { boneMarkerModel_ = path; }

private:

	// Object3d.h （private でOK）
	bool  isPlayAnimation_ = false;
	float animationTime_ = 0.0f;
	std::string playingAnimName_;   // 空なら先頭を使う
	std::string playingNodeName_ = "root"; // まずはroot/なければ先頭
	bool loop_ = true;
	
	bool poseReady_ = false;

	bool debugDrawBones_ = false;
	std::string boneMarkerModel_ = "cube/cube.obj";
	std::vector<std::unique_ptr<Object3d>> boneMarkers_;

	SkinCluster skinCluster_;

	SrvManager* srvManager_ = nullptr; // ★参照

	private:
		SkinCluster CreateSkinCluster(
			ID3D12Device* device,
			const Model::Skeleton& skeleton,
			const std::map<std::string, Model::JointWeightData>& skinData,
			uint32_t vertexCount,
			ID3D12DescriptorHeap* srvHeap,
			uint32_t descriptorSize
		);

		void UpdateSkinCluster_();


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

