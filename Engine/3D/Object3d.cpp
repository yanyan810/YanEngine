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

		// 遯ｶ諛岩落邵ｺ・ｮ郢ｧ・ｸ郢晢ｽｧ郢ｧ・､郢晢ｽｳ郢晏現繝ｻ陷医・ﾂ・､遯ｶ繝ｻ邵ｺ荵晢ｽ芽沂荵晢ｽ∫ｹｧ蜈ｷ・ｼ蛹ｻ縺咲ｹ晢ｽｼ郢晄じ窶ｲ霎滂ｽ｡邵ｺ繝ｻ繝ｻ陋ｻ繝ｻ繝ｻ驍ｯ・ｭ隰悶・・ｼ繝ｻ
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
	// 隨倥・bject3dCommon 邵ｺ荵晢ｽ芽ｫ｡・ｾ邵ｺ繝ｻ
	SrvManager* srv = object3dCommon ? object3dCommon->GetSrvManager() : nullptr;
	SkinningCommon* skin = object3dCommon ? object3dCommon->GetSkinningCommon() : nullptr;

	Initialize(object3dCommon, dx, srv, skin);
}

void Object3d::Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom) {
	// 陋ｻ譎・ｄ陋ｹ髢繝ｻ騾・・
	this->object3dCommon = object3dCommon;
	dx_ = dx;
	srvManager_ = srv;
	skinningCommon_ = skinCom;

	if (!dx_) {
		OutputDebugStringA("[Object3d] Initialize failed: DirectXCommon is null.\n");
		return;
	}

	transformationMatrixResourceModel= dx->CreateBufferResource(sizeof(TransformationMatrix));
	if (!transformationMatrixResourceModel) {
		OutputDebugStringA("[Object3d] Initialize failed: transformationMatrixResourceModel is null.\n");
		return;
	}
	//隴厄ｽｸ邵ｺ蟠趣ｽｾ・ｼ郢ｧﾂ邵ｺ貅假ｽ∫ｸｺ・ｮ郢ｧ・｢郢晏ｳｨﾎ樒ｹｧ・ｹ郢ｧ雋槫徐陟輔・
	transformationMatrixResourceModel->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataModel));
	//陷雁・ｽｽ蟠趣ｽ｡謔溘・郢ｧ蜻亥ｶ檎ｸｺ蟠趣ｽｾ・ｼ郢ｧ阮吶堤ｸｺ鄙ｫ・･
	transformationMatrixDataModel->WVP = Matrix4x4::MakeIdentity4x4();
	transformationMatrixDataModel->World = Matrix4x4::MakeIdentity4x4();

	// 郢晢ｽｩ郢ｧ・､郢晉｣ｯ譛ｪ鬨ｾ・｣邵ｺ・ｮ陋ｻ譎・ｄ陋ｹ繝ｻ
	light_ = std::make_unique<Object3dLight>();
	light_->Initialize(dx);

	// 郢ｧ・｢郢昜ｹ斟鍋ｹ晢ｽｼ郢ｧ・ｿ郢晢ｽｼ邵ｺ・ｮ陋ｻ譎・ｄ陋ｹ繝ｻ
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

	//Transform陞溽判辟・
	transform = { {1.0f,1.0f,1.0f},
				  {0.0f,0.0f,0.0f},
				  {0.0f,0.0f,0.0f} };
	cameraTransform = { {1.0f,1.0f,1.0f},
						{0.3f,0.0f,0.0f},
						{0.0f,4.0f,-10.0f} };

	this->camera_ = object3dCommon->GetDefaultCamera();

	cameraResource_ = dx_->CreateBufferResource(sizeof(CameraGPU));
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

	effectParamResource_ = dx_->CreateBufferResource(sizeof(EffectParam));
	effectParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&effectParamData_));
	if (effectParamData_) {
		effectParamData_->outlineColor = outlineColor_;
		effectParamData_->outlineThickness = outlineThickness_;
		effectParamData_->enableOutline = enableOutline_ ? 1.0f : 0.0f;
		effectParamData_->dissolveThreshold = dissolveThreshold_;
		effectParamData_->enableDissolve = enableDissolve_ ? 1.0f : 0.0f;
		effectParamData_->dissolveEdgeWidth = dissolveEdgeWidth_;
		effectParamData_->dissolveEdgeColor = dissolveEdgeColor_;
	}
	
	if (!maskTexturePath_.empty()) {
		TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
	}
}

void Object3d::Update(float dt)
{
	// 1) 鬨ｾ螢ｼ・ｸ・ｸ邵ｺ・ｮWorld繝ｻ繝ｻbject3d邵ｺ・ｮTransform繝ｻ繝ｻ
	Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(
		transform.scale, transform.rotate, transform.translate);

	if (animator_) {
		animator_->Update(dt);
		animator_->UpdateSkinCluster(dx_);
	}


	// 2) glTF/FBX/OBJ陷茨ｽｱ鬨ｾ螟ｲ・ｼ蜩ｺodel邵ｺ・ｮRootNode髯ｦ謔溘・郢ｧ蟶昶・騾包ｽｨ繝ｻ繝ｻigid邵ｺ・ｮ邵ｺ・ｿ繝ｻ繝ｻ
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

	// 隨倥・繝ｻ郢晢ｽｼ郢晢ｽｳ霓､・ｹ髯ｦ・ｨ驕会ｽｺ隴厄ｽｴ隴・ｽｰ繝ｻ繝ｻine/Sphere霎滂ｽ｡邵ｺ遉ｼ豐ｿ繝ｻ繝ｻ
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

	if (effectParamData_) {
		effectParamData_->outlineColor = outlineColor_;
		effectParamData_->outlineThickness = outlineThickness_;
		effectParamData_->enableOutline = enableOutline_ ? 1.0f : 0.0f;
		effectParamData_->dissolveThreshold = dissolveThreshold_;
		effectParamData_->enableDissolve = enableDissolve_ ? 1.0f : 0.0f;
		effectParamData_->dissolveEdgeWidth = dissolveEdgeWidth_;
		effectParamData_->dissolveEdgeColor = dissolveEdgeColor_;
	}
	
	if (!maskTexturePath_.empty()) {
		TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
	}
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
	// EnvMap SRV 郢ｧ繝ｻRootParameter 7 邵ｺ・ｫ郢ｧ・ｻ郢昴・繝ｨ邵ｺ蜷ｶ・玖怦・ｱ鬨ｾ螢ｼ繝ｻ騾・・
	// ------------------------------------------------------------
	auto BindEnvironmentMapIfNeeded = [&]() {
		if (!useEnvironmentMap_) {
			return;
		}

		if (environmentTexturePath_.empty()) {
			OutputDebugStringA("[EnvMap] environmentTexturePath_ is empty\n");
			return;
		}

		// 隴幢ｽｪ郢晢ｽｭ郢晢ｽｼ郢晏ｳｨ竊醍ｹｧ蟲ｨﾎ溽ｹ晢ｽｼ郢昴・
		TextureManager::GetInstance()->LoadTexture(environmentTexturePath_);

		// RootParameter 7 : t2
		cmd->SetGraphicsRootDescriptorTable(
			7,
			TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
		);
		};

	if (model_->HasSkinning()) {
		// =====================================================
		// Compute Shader 邵ｺ・ｫ郢ｧ蛹ｻ・狗ｹｧ・ｹ郢ｧ・ｭ郢昜ｹ斟ｦ郢ｧ・ｰ陷・ｽｦ騾・・
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
		// 郢ｧ・ｹ郢ｧ・ｭ郢晢ｽｳ闔牙･窶ｳ郢晢ｽ｡郢昴・縺咏ｹ晢ｽ･隴幢ｽｬ闖ｴ繝ｻ(CS陟募ｾ後・鬯・ｉ縺帷ｹ晁・繝｣郢晁ｼ斐＜郢ｧ蜑・ｽｽ・ｿ邵ｺ・｣邵ｺ・ｦ隰蜀怜愛)
		auto SetNormalPipelineState = [&]() {
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
		};

		SetNormalPipelineState();

		// Transform (Root 1)
		cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

		// Lights / Camera (Root 3..6 驕ｲ蟲ｨﾂ・｣rimitiveCommon/Object3dCommon 邵ｺ・ｮ郢ｧ・ｷ郢ｧ・ｰ郢晞亂繝｡郢晢ｽ｣邵ｺ・ｫ陷ｷ蛹ｻ・冗ｸｺ蟶呻ｽ・
		cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

		BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}

		// Draw skinned (CS邵ｺ・ｧ陷・ｽｺ陷牙ｸ呻ｼ・ｹｧ蠕娯螺鬯・ｉ縺帷ｹ晁・繝｣郢晁ｼ斐＜郢ｧ蜑・ｽｽ・ｿ騾包ｽｨ)
		if (animator_ && animator_->IsPoseReady()) {
			if (enableOutline_ && object3dCommon) {
				object3dCommon->SetGraphicsPipelineStateOutline();
				cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
				model_->DrawSkinnedCompute(cmd, animator_->GetSkinCluster());
				SetNormalPipelineState();
			}
			
			if (!maskTexturePath_.empty()) {
				cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
			}
			cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
			
			model_->DrawSkinnedCompute(cmd, animator_->GetSkinCluster());
		}

		// =====================================================
		// 郢ｧ・ｹ郢ｧ・ｭ郢晢ｽｳ霎滂ｽ｡邵ｺ證ｦ・ｼ莠･谿ｴ邵ｺ・ｪ邵ｺ・ｩ繝ｻ蟲ｨ・定ｬ荳奇ｿ･
		// =====================================================
		{
			auto SetNormalPipelineState = [&]() {
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
			};

			SetNormalPipelineState();

			// lights/camera (Rigid陋幢ｽｴRootSig)
			cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

			// EnvMap (Root 7 : t2)
			BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}
		cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

			// Material / VB / IB
			cmd->SetGraphicsRootConstantBufferView(0, model_->GetMaterialCBV());
			cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
			cmd->IASetIndexBuffer(&model_->GetIBV());

			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			const Matrix4x4 baseWorld = transformationMatrixDataModel->World;

			// -------------------------------------------------
			// 邵ｺ譏ｴ繝ｻ闔画じ繝ｻ鬮ｱ讒ｭ縺帷ｹｧ・ｭ郢晢ｽｳ郢ｧ蜻育ｷ堤ｸｺ繝ｻ
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
				
				if (enableOutline_ && object3dCommon) {
					object3dCommon->SetGraphicsPipelineStateOutline();
					cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
					model_->DrawOneMesh(cmd, inst.meshIndex, 2);
					SetNormalPipelineState();
				}
				
				if (!maskTexturePath_.empty()) {
					cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
				}
				cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

				model_->DrawOneMesh(cmd, inst.meshIndex, 2);
			}

			// 隰鯉ｽｻ邵ｺ繝ｻ
			transformationMatrixDataModel->World = baseWorld;
			transformationMatrixDataModel->WVP = Matrix4x4::Multiply(baseWorld, vp);
			transformationMatrixDataModel->WorldInverseTranspose =
				Matrix4x4::Transpose(Matrix4x4::Inverse(baseWorld));
		}
	} else {
		auto SetNormalPipelineState = [&]() {
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
		};

		SetNormalPipelineState();

		// light/camera CBV
		cmd->SetGraphicsRootConstantBufferView(3, light_->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, light_->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, light_->GetSpotLightResource()->GetGPUVirtualAddress());

		// EnvMap (Root 7 : t2)
		BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}
		cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

		// VB/IB/Material
		cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
		cmd->IASetIndexBuffer(&model_->GetIBV());
		cmd->SetGraphicsRootConstantBufferView(0, model_->GetMaterialCBV());

		// ---- 郢晏ｼｱ繝ｻ郢晏ｳｨ縺・ｹ昜ｹ斟鍋ｸｺ蠕娯旺郢ｧ荵昶・郢ｧ繝ｻnode雎亥ｼｱ竊楢ｬ荳奇ｿ･ ----
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

				if (enableOutline_ && object3dCommon) {
					object3dCommon->SetGraphicsPipelineStateOutline();
					cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
					model_->DrawOneMesh(cmd, inst.meshIndex, 2);
					SetNormalPipelineState();
				}

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

				//隰悶・・ｮ螢ｹ・・ｹｧ蠕娯螺郢昴・縺醍ｹｧ・ｹ郢昶・ﾎ慕ｹｧ蟶昶・陟｢繝ｻ
				if (useOverrideTexture_) {
					auto handle = TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_);
					
					if (enableOutline_ && object3dCommon) {
						object3dCommon->SetGraphicsPipelineStateOutline();
						cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
						model_->Draw(cmd, 1, &handle);
						SetNormalPipelineState();
					}
					
					model_->Draw(cmd, 1, &handle);
				} else {
					if (enableOutline_ && object3dCommon) {
						object3dCommon->SetGraphicsPipelineStateOutline();
						cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
						model_->Draw(cmd);
						SetNormalPipelineState();
					}

					//郢晢ｽ｢郢昴・ﾎ晉ｸｺ・ｫ邵ｺ繧・ｽ狗ｹ昴・縺醍ｹｧ・ｹ郢昶・ﾎ慕ｹｧ蟶昶・陟｢繝ｻ
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

	if (!maskTexturePath_.empty()) {
		cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
	}
	cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

	model_->Draw(cmd, 1, &srv);
}

//郢昴・縺醍ｹｧ・ｹ郢昶・ﾎ慕ｹｧ蜻域ｬ陞ｳ繝ｻ
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
		// ==== boneMarkers 闖ｴ諛医・繝ｻ蛹ｻ繝ｧ郢晁・繝｣郢ｧ・ｰ騾包ｽｨ繝ｻ繝ｻ====
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
	swordMeshIndex_ = 2; // 邵ｺ・ｾ邵ｺ螢ｹ繝ｻ陜暦ｽｺ陞ｳ螢ｹ縲丹K繝ｻ蛹ｻ竕邵ｺ・ｨ邵ｺ・ｧ隶諛・ｽｴ・｢邵ｺ・ｫ邵ｺ蜉ｱ窶ｻ郢ｧ繧・ｼ樒ｸｺ繝ｻ・ｼ繝ｻ

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

	// Object3d 邵ｺ・ｮ World繝ｻ繝ｻpdate邵ｺ・ｧ闖ｴ・ｿ邵ｺ・｣邵ｺ・ｦ郢ｧ荵昴・邵ｺ・ｨ陷ｷ蠕個ｧ繝ｻ繝ｻ
	Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(
		transform.scale, transform.rotate, transform.translate);

	// 隨倥・竕邵ｺ・ｪ邵ｺ貅倥・髯ｦ謔溘・驍会ｽｻ邵ｺ・ｯ邵ｲ譯ＰintWorld = skeletonSpace * objectWorld邵ｲ髦ｪ縲帝お・ｱ闕ｳﾂ邵ｺ蜉ｱ窶ｻ郢ｧ繝ｻ
	Matrix4x4 jointWorld =
		Matrix4x4::Multiply(poseSkeleton.joints[jointIndex].skeletonSpaceMatrix, worldMatrixModel);

	return jointWorld;
}

bool Object3d::HasJoint(const std::string& jointName) const
{
	if (!model_ || !model_->HasSkinning() || !animator_ || !animator_->IsPoseReady()) {
		return false;
	}

	const auto& poseSkeleton = animator_->GetPoseSkeleton();
	return poseSkeleton.jointMap.contains(jointName);
}
