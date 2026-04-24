#include "Object3d.h"
#include "Object3dCommon.h"
#include "PrimitiveCommon.h"


//Vector3 Normalize(const Vector3& v) {
//	float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
//	if (length == 0.0f) return { 0.0f, 0.0f, 0.0f };
//	return { v.x / length, v.y / length, v.z / length };
//}

static void ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time) {
	for (auto& joint : skeleton.joints) {

		auto it = animation.nodeAnimations.find(joint.name);
		if (it == animation.nodeAnimations.end()) {
			continue;
		}

		const NodeAnimation& na = it->second;

		// “そのジョイントの元値” から始める（カーブが無い成分は維持）
		Vector3 t = joint.transform.translate;
		Quaternion r = joint.transform.rotate;
		Vector3 s = joint.transform.scale;

		if (!na.translate.keyframes.empty()) t = CalculateValue(na.translate.keyframes, time);
		if (!na.rotate.keyframes.empty())    r = CalculateValue(na.rotate.keyframes, time);
		if (!na.scale.keyframes.empty())     s = CalculateValue(na.scale.keyframes, time);

		joint.transform.translate = t;
		joint.transform.rotate = r;
		joint.transform.scale = s;
	}
}

static uint32_t CalcTotalVertexCount(const Model::ModelData& modelData) {
	uint64_t total = 0;
	for (const auto& m : modelData.meshes) {
		total += m.vertices.size();
	}
	return static_cast<uint32_t>(total);
}

void Object3d::Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx) {
	// ★Object3dCommon から拾う
	SrvManager* srv = object3dCommon ? object3dCommon->GetSrvManager() : nullptr;
	SkinningCommon* skin = object3dCommon ? object3dCommon->GetSkinningCommon() : nullptr;

	Initialize(object3dCommon, dx, srv, skin);
}

void Object3d::Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom) {
	// 初期化処理
	this->object3dCommon = object3dCommon;
	dx_ = dx;
	srvManager_ = srv;
	skinningCommon_ = skinCom;

	transformationMatrixResourceModel= dx->CreateBufferResource(sizeof(TransformationMatrix));
	//書き込むためのアドレスを取得
	transformationMatrixResourceModel->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataModel));
	//単位行列を書き込んでおく
	transformationMatrixDataModel->WVP = Matrix4x4::MakeIdentity4x4();
	transformationMatrixDataModel->World = Matrix4x4::MakeIdentity4x4();

	// ライト関連の初期化
	light_ = std::make_unique<Object3dLight>();
	light_->Initialize(dx);

	// アニメーターの初期化
	animator_ = std::make_unique<Animator>();
	if (model_) {
		animator_->Initialize(model_);
		if (model_->HasSkinning() && srvManager_) {
			animator_->CreateSkinCluster(
				dx_->GetDevice(),
				dx_,
				srvManager_,
				TextureManager::GetInstance()->GetSrvDescriptorHeap(),
				dx_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			);
		}
	}

	//Transform変数
	transform = { {1.0f,1.0f,1.0f},
				  {0.0f,0.0f,0.0f},
				  {0.0f,0.0f,0.0f} };
	cameraTransform = { {1.0f,1.0f,1.0f},
						{0.3f,0.0f,0.0f},
						{0.0f,4.0f,-10.0f} };

	this->camera_ = object3dCommon->GetDefaultCamera();

	cameraResource_ = dx_->CreateBufferResource(sizeof(CameraGPU));
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
}

void Object3d::Update(float dt)
{
	// 1) 通常のWorld（Object3dのTransform）
	Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(
		transform.scale, transform.rotate, transform.translate);

	if (animator_) {
		animator_->Update(dt);
		animator_->UpdateSkinCluster(dx_);
	}


	// 2) glTF/FBX/OBJ共通：ModelのRootNode行列を適用（Rigidのみ）
	if (model_) {
		if (!model_->HasSkinning()) {
			const Matrix4x4& root = model_->GetRootLocalMatrix();
			worldMatrixModel = Matrix4x4::Multiply(root, worldMatrixModel);
		}
	}

	// 3) camera
	if (!camera_) {
		camera_ = object3dCommon->GetDefaultCamera();
	}

	// ★ボーン点表示更新（Line/Sphere無し版）
	if (debugDrawBones_ && model_ && model_->HasSkinning() && animator_ && animator_->IsPoseReady()) {
		const auto& poseSkeleton = animator_->GetPoseSkeleton();
		for (size_t i = 0; i < poseSkeleton.joints.size() && i < boneMarkers_.size(); ++i) {

			const auto& j = poseSkeleton.joints[i];

			Matrix4x4 jointWorld =
				Matrix4x4::Multiply(j.skeletonSpaceMatrix, worldMatrixModel);

			Vector3 pos{
				jointWorld.m[3][0],
				jointWorld.m[3][1],
				jointWorld.m[3][2]
			};

			boneMarkers_[i]->SetTranslate(pos);
			boneMarkers_[i]->Update(0.0f);
		}
	}

	// WVP
	Matrix4x4 wvpModel = worldMatrixModel;
	if (camera_) {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		wvpModel = Matrix4x4::Multiply(worldMatrixModel, vp);
	}

	transformationMatrixDataModel->WVP = wvpModel;
	transformationMatrixDataModel->World = worldMatrixModel;

	// 4) WorldInverseTranspose
	Matrix4x4 invW = Matrix4x4::Inverse(worldMatrixModel);
	transformationMatrixDataModel->WorldInverseTranspose = Matrix4x4::Transpose(invW);
}



void Object3d::Draw()
{
	if (!model_) {
		OutputDebugStringA("[Object3d] Draw skipped: model_ is null\n");
		return;
	}

	if (cameraData_ && camera_) {
		cameraData_->worldPosition = camera_->GetTranslate();
	}

	auto* cmd = dx_->GetCommandList();

	// SRV heap
	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// ------------------------------------------------------------
	// EnvMap SRV を RootParameter 7 にセットする共通処理
	// ------------------------------------------------------------
	auto BindEnvironmentMapIfNeeded = [&]() {
		if (!useEnvironmentMap_) {
			return;
		}

		if (environmentTexturePath_.empty()) {
			OutputDebugStringA("[EnvMap] environmentTexturePath_ is empty\n");
			return;
		}

		// 未ロードならロード
		TextureManager::GetInstance()->LoadTexture(environmentTexturePath_);

		// RootParameter 7 : t2
		cmd->SetGraphicsRootDescriptorTable(
			7,
			TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
		);
		};

	if (model_->HasSkinning()) {
		// =====================================================
		// Compute Shader によるスキニング処理
		// =====================================================
		if (animator_ && animator_->IsPoseReady()) {
			auto& skinCluster = animator_->GetSkinCluster();
			if (skinCluster.isUavReady) {
				CD3DX12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
					skinCluster.outputVertexResource.Get(),
					D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				);
				cmd->ResourceBarrier(1, &preBarrier);
			} else {
				skinCluster.isUavReady = true;
			}

			cmd->SetPipelineState(skinningCommon_->GetComputePipelineState());
			cmd->SetComputeRootSignature(skinningCommon_->GetComputeRootSignature());

			cmd->SetComputeRootDescriptorTable(0, skinCluster.paletteSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(3, skinCluster.outputVertexUavHandle.second);
			cmd->SetComputeRootConstantBufferView(4, skinCluster.skinningInformationResource->GetGPUVirtualAddress());

			cmd->Dispatch((model_->GetVertexCount() + 1023) / 1024, 1, 1);

			CD3DX12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
				skinCluster.outputVertexResource.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
			);
			cmd->ResourceBarrier(1, &postBarrier);
		}

		// =====================================================
		// スキン付きメッシュ本体 (CS後の頂点バッファを使って描画)
		// =====================================================
		// スキン無し(通常のObject3dCommon等)のパイプラインを使用する
		if (primitiveCommon_) {
			if (useEnvironmentMap_) {
				primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
			} else {
				primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
			}
		} else {
			if (useEnvironmentMap_) {
				object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
			} else {
				object3dCommon->SetGraphicsPipelineState(blendMode_);
			}
		}

		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Transform (Root 1)
		cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

		// Lights / Camera (Root 3..6 等、PrimitiveCommon/Object3dCommon のシグネチャに合わせる)
		cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

		BindEnvironmentMapIfNeeded();

		// Draw skinned (CSで出力された頂点バッファを使用)
		if (animator_ && animator_->IsPoseReady()) {
			model_->DrawSkinnedCompute(cmd, animator_->GetSkinCluster());
		}

		// =====================================================
		// スキン無し（剣など）を描く
		// =====================================================
		{
			if (primitiveCommon_) {
				if (useEnvironmentMap_) {
					primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				} else {
					primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				}
			} else {
				if (useEnvironmentMap_) {
					object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
				} else {
					object3dCommon->SetGraphicsPipelineState(blendMode_);
				}
			}

			cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// lights/camera (Rigid側RootSig)
			cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

			// EnvMap (Root 7 : t2)
			BindEnvironmentMapIfNeeded();

			// Material / VB / IB
			cmd->SetGraphicsRootConstantBufferView(0, model_->GetMaterialCBV());
			cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
			cmd->IASetIndexBuffer(&model_->GetIBV());

			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			const Matrix4x4 baseWorld = transformationMatrixDataModel->World;

			// -------------------------------------------------
			// その他の非スキンを描く
			// -------------------------------------------------
			const Animation* anim = nullptr;
			float animTime = 0.0f;
			if (animator_ && animator_->HasAnimation()) {
				const auto& anims = model_->GetAnimations();
				if (!animator_->GetPlayingAnimName().empty()) {
					auto itA = anims.find(animator_->GetPlayingAnimName());
					if (itA != anims.end()) {
						anim = &itA->second;
					}
				}
				if (!anim && !anims.empty()) {
					anim = &anims.begin()->second;
				}
				animTime = animator_->GetTime();
			}

			std::vector<Matrix4x4> nodeGlobals;
			model_->ComputeNodeGlobalMatrices(anim, animTime, nodeGlobals);

			for (const auto& inst : model_->GetNodeInstances()) {
				if (model_->IsMeshSkinned(inst.meshIndex)) {
					continue;
				}

				const Matrix4x4 nodeWorld = nodeGlobals[inst.nodeIndex];
				Matrix4x4 world = Matrix4x4::Multiply(nodeWorld, baseWorld);
				Matrix4x4 wvpM = Matrix4x4::Multiply(world, vp);

				transformationMatrixDataModel->World = world;
				transformationMatrixDataModel->WVP = wvpM;
				transformationMatrixDataModel->WorldInverseTranspose =
					Matrix4x4::Transpose(Matrix4x4::Inverse(world));

				cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());
				model_->DrawOneMesh(cmd, inst.meshIndex, 2);
			}

			// 戻す
			transformationMatrixDataModel->World = baseWorld;
			transformationMatrixDataModel->WVP = Matrix4x4::Multiply(baseWorld, vp);
			transformationMatrixDataModel->WorldInverseTranspose =
				Matrix4x4::Transpose(Matrix4x4::Inverse(baseWorld));
		}
	} else {
		if (primitiveCommon_) {
			if (useEnvironmentMap_) {
				primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
			} else {
				primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
			}
		} else {
			if (useEnvironmentMap_) {
				object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
			} else {
				object3dCommon->SetGraphicsPipelineState(blendMode_);
			}
		}

		// light/camera CBV
		cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

		// EnvMap (Root 7 : t2)
		BindEnvironmentMapIfNeeded();

		// VB/IB/Material
		cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
		cmd->IASetIndexBuffer(&model_->GetIBV());
		cmd->SetGraphicsRootConstantBufferView(0, model_->GetMaterialCBV());

		// ---- ノードアニメがあるなら node毎に描く ----
		if (animator_ && animator_->HasAnimation()) {

			const auto& anims = model_->GetAnimations();
			const Animation* anim = nullptr;
			float animTime = animator_->GetTime();

			if (!animator_->GetPlayingAnimName().empty()) {
				auto itA = anims.find(animator_->GetPlayingAnimName());
				if (itA != anims.end()) {
					anim = &itA->second;
				}
			}
			if (!anim && !anims.empty()) {
				anim = &anims.begin()->second;
			}

			std::vector<Matrix4x4> nodeGlobals;
			model_->ComputeNodeGlobalMatrices(anim, animTime, nodeGlobals);

			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			const Matrix4x4 baseWorld = transformationMatrixDataModel->World;

			for (const auto& inst : model_->GetNodeInstances()) {
				const Matrix4x4 nodeWorld = nodeGlobals[inst.nodeIndex];

				Matrix4x4 world = Matrix4x4::Multiply(nodeWorld, baseWorld);
				Matrix4x4 wvp = Matrix4x4::Multiply(world, vp);

				transformationMatrixDataModel->World = world;
				transformationMatrixDataModel->WVP = wvp;
				transformationMatrixDataModel->WorldInverseTranspose =
					Matrix4x4::Transpose(Matrix4x4::Inverse(world));

				cmd->SetGraphicsRootConstantBufferView(
					1, transformationMatrixResourceModel->GetGPUVirtualAddress());

				model_->DrawOneMesh(cmd, inst.meshIndex, 2);
			}
		} else {
			cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

			if (video_ && video_->IsReady()) {
				video_->ReadNextFrame();
				video_->UploadToGpu(cmd);

				D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
				model_->Draw(cmd, 1, &vh);

				video_->EndFrame(cmd);
			} else {

				//指定されたテクスチャを適応
				if (useOverrideTexture_) {
					auto handle = TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_);
					model_->Draw(cmd, 1, &handle);
				} else {
					//モデルにあるテクスチャを適応
					model_->Draw(cmd);
				}
			}
		}
	}

	// debug bones
	if (debugDrawBones_ && !boneMarkers_.empty()) {
		for (auto& m : boneMarkers_) {
			m->Draw();
		}
	}
}

void Object3d::DrawWithOverrideSrv(const D3D12_GPU_DESCRIPTOR_HANDLE& srv)
{
	if (!model_) {
		return;
	}

	auto* cmd = dx_->GetCommandList();

	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	if (primitiveCommon_) {
		if (useEnvironmentMap_) {
			primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
		} else {
			primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
		}
	} else {
		if (useEnvironmentMap_) {
			object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
		} else {
			object3dCommon->SetGraphicsPipelineState(blendMode_);
		}
	}

	cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

	// RootParameter 7 : t2
	if (useEnvironmentMap_) {
		if (!environmentTexturePath_.empty()) {
			TextureManager::GetInstance()->LoadTexture(environmentTexturePath_);
			cmd->SetGraphicsRootDescriptorTable(
				7,
				TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
			);
		}
	}

	cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
	cmd->IASetIndexBuffer(&model_->GetIBV());
	cmd->SetGraphicsRootConstantBufferView(0, model_->GetMaterialCBV());
	cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

	model_->Draw(cmd, 1, &srv);
}

//テクスチャを指定
void Object3d::SetTexture(const std::string& path)
{
	texturePath_ = path;
	TextureManager::GetInstance()->LoadTexture(path);
	useOverrideTexture_ = true;
}

void Object3d::SetModel(const std::string& filePath) {
	auto* mgr = ModelManager::GetInstance();
	
	Model* m = mgr->FindModel(filePath);
	if (!m) {
		mgr->LoadModel(filePath);
		m = mgr->FindModel(filePath);
	}
	model_ = m;

	boneMarkers_.clear();

	if (!model_) { return; }

	if (animator_) {
		animator_->Initialize(model_);
		if (model_->HasSkinning() && srvManager_) {
			animator_->CreateSkinCluster(
				dx_->GetDevice(),
				dx_,
				srvManager_,
				TextureManager::GetInstance()->GetSrvDescriptorHeap(),
				dx_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			);
		}
	}

	if (model_->HasSkinning() && animator_) {
		// ==== boneMarkers 作成（デバッグ用） ====
		const auto& skel = animator_->GetPoseSkeleton();
		boneMarkers_.reserve(skel.joints.size());

		for (size_t i = 0; i < skel.joints.size(); ++i) {
			auto marker = std::make_unique<Object3d>();
			marker->Initialize(object3dCommon, dx_, srvManager_,skinningCommon_);
			marker->SetModel(boneMarkerModel_);
			marker->SetScale({ 0.03f, 0.03f, 0.03f });
			marker->SetRotate({ 0,0,0 });
			marker->SetEnableLighting(0);
			boneMarkers_.push_back(std::move(marker));
		}
	}

	swordNodeIndex_ = -1;
	swordMeshIndex_ = 2; // まずは固定でOK（あとで検索にしてもいい）

	if (model_) {
		swordNodeIndex_ = model_->FindNodeIndexByName("sword");
	}


}






Matrix4x4 Object3d::GetJointWorldMatrix(const std::string& jointName) const
{
	if (!model_ || !model_->HasSkinning() || !animator_ || !animator_->IsPoseReady()) {
		return Matrix4x4::MakeIdentity4x4();
	}

	const auto& poseSkeleton = animator_->GetPoseSkeleton();
	auto it = poseSkeleton.jointMap.find(jointName);
	if (it == poseSkeleton.jointMap.end()) {
		return Matrix4x4::MakeIdentity4x4();
	}
	const int32_t jointIndex = it->second;

	// Object3d の World（Updateで使ってるのと同じ）
	Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(
		transform.scale, transform.rotate, transform.translate);

	// ★あなたの行列系は「jointWorld = skeletonSpace * objectWorld」で統一してる
	Matrix4x4 jointWorld =
		Matrix4x4::Multiply(poseSkeleton.joints[jointIndex].skeletonSpaceMatrix, worldMatrixModel);

	return jointWorld;
}
