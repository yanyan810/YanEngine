#include "Animator.h"
#include "AnimationEvaluate.h"
#include <algorithm>

void Animator::Initialize(Model* model) {
	model_ = model;
	if (model_ && model_->HasSkinning()) {
		poseSkeleton_ = model_->GetSkeleton();
		poseReady_ = true;
		Model::UpdateSkeleton(poseSkeleton_);
	}
}

void Animator::ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time) {
	for (auto& joint : skeleton.joints) {

		auto it = animation.nodeAnimations.find(joint.name);
		if (it == animation.nodeAnimations.end()) {
			continue;
		}

		const NodeAnimation& na = it->second;

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

void Animator::PlayAnimation(const std::string& animName, bool loop) {
	if (!model_ || model_->GetAnimations().empty()) return;

	if (animName.empty()) {
		playingAnimName_ = model_->GetAnimations().begin()->first;
	}
	else {
		playingAnimName_ = animName;
	}

	loop_ = loop;
	animationTime_ = 0.0f;
	isPlayAnimation_ = true;
}

bool Animator::IsAnimationFinished() const {
	if (!isPlayAnimation_ || !model_) return true;

	auto it = model_->GetAnimations().find(playingAnimName_);
	if (it != model_->GetAnimations().end()) {
		return (!loop_ && animationTime_ >= it->second.duration);
	}
	return true;
}

bool Animator::HasAnimation() const {
	return model_ && !model_->GetAnimations().empty();
}

void Animator::Update(float dt) {
	if (!isPlayAnimation_ || !model_ || !poseReady_) return;

	auto it = model_->GetAnimations().find(playingAnimName_);
	if (it == model_->GetAnimations().end()) return;

	const Animation& clip = it->second;
	animationTime_ += dt;

	if (loop_) {
		animationTime_ = std::fmod(animationTime_, clip.duration);
	}
	else {
		if (animationTime_ > clip.duration) {
			animationTime_ = clip.duration;
		}
	}

	ApplyAnimation(poseSkeleton_, clip, animationTime_);
	Model::UpdateSkeleton(poseSkeleton_);
}

void Animator::UpdateSkinCluster(DirectXCommon* dx) {
	if (!poseReady_ || !model_ || !model_->HasSkinning()) return;

	for (size_t i = 0; i < poseSkeleton_.joints.size(); ++i) {
		assert(i < skinCluster_.inverseBindPoseMatrices.size());
		skinCluster_.mappedPalette[i].skeletonSpaceMatrix =
			Matrix4x4::Multiply(skinCluster_.inverseBindPoseMatrices[i], poseSkeleton_.joints[i].skeletonSpaceMatrix);
		skinCluster_.mappedPalette[i].skeletonSpaceInverseTransposeMatrix =
			Matrix4x4::Transpose(Matrix4x4::Inverse(skinCluster_.mappedPalette[i].skeletonSpaceMatrix));
	}
}

void Animator::CreateSkinCluster(
	ID3D12Device* device,
	DirectXCommon* dx,
	SrvManager* srvManager,
	ID3D12DescriptorHeap* srvHeap,
	uint32_t descriptorSize)
{
	if (!model_ || !model_->HasSkinning()) return;

	const auto& skeleton = model_->GetSkeleton();
	const auto& skinData = model_->GetSkinClusterData();
	uint32_t vertexCount = model_->GetVertexCount();

	skinCluster_ = SkinCluster{};

	// ========= palette (StructuredBuffer<WellForGPU>) =========
	skinCluster_.paletteResource = dx->CreateBufferResource(sizeof(WellForGPU) * skeleton.joints.size());

	WellForGPU* mappedPalette = nullptr;
	skinCluster_.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
	skinCluster_.mappedPalette = { mappedPalette, skeleton.joints.size() };

	uint32_t srvIndex = srvManager->Allocate();
	skinCluster_.paletteSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, srvIndex);
	skinCluster_.paletteSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC paletteSrvDesc{};
	paletteSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	paletteSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	paletteSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	paletteSrvDesc.Buffer.FirstElement = 0;
	paletteSrvDesc.Buffer.NumElements = static_cast<UINT>(skeleton.joints.size());
	paletteSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	paletteSrvDesc.Buffer.StructureByteStride = sizeof(WellForGPU);
	device->CreateShaderResourceView(skinCluster_.paletteResource.Get(), &paletteSrvDesc, skinCluster_.paletteSrvHandle.first);

	// ========= influence (VertexCount 分の VB) =========
	skinCluster_.influenceResource = dx->CreateBufferResource(sizeof(VertexInfluence) * vertexCount);

	VertexInfluence* mappedInfluence = nullptr;
	skinCluster_.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
	std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * vertexCount);
	skinCluster_.mappedInfluence = { mappedInfluence, vertexCount };

	skinCluster_.influenceBufferView.BufferLocation = skinCluster_.influenceResource->GetGPUVirtualAddress();
	skinCluster_.influenceBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexInfluence) * vertexCount);
	skinCluster_.influenceBufferView.StrideInBytes = sizeof(VertexInfluence);

	// --- influence用SRV作成 ---
	uint32_t influenceSrvIndex = srvManager->Allocate();
	skinCluster_.influenceSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, influenceSrvIndex);
	skinCluster_.influenceSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, influenceSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrvDesc{};
	influenceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	influenceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	influenceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	influenceSrvDesc.Buffer.FirstElement = 0;
	influenceSrvDesc.Buffer.NumElements = vertexCount;
	influenceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	influenceSrvDesc.Buffer.StructureByteStride = sizeof(VertexInfluence);
	device->CreateShaderResourceView(skinCluster_.influenceResource.Get(), &influenceSrvDesc, skinCluster_.influenceSrvHandle.first);

	// --- 入力頂点(ModelのvertexResource)用SRV作成 ---
	uint32_t inputVertexSrvIndex = srvManager->Allocate();
	skinCluster_.inputVertexSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, inputVertexSrvIndex);
	skinCluster_.inputVertexSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, inputVertexSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC inputVertexSrvDesc{};
	inputVertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	inputVertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	inputVertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	inputVertexSrvDesc.Buffer.FirstElement = 0;
	inputVertexSrvDesc.Buffer.NumElements = vertexCount;
	inputVertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	inputVertexSrvDesc.Buffer.StructureByteStride = sizeof(Model::VertexData);
	device->CreateShaderResourceView(model_->GetVertexResource().Get(), &inputVertexSrvDesc, skinCluster_.inputVertexSrvHandle.first);

	// --- スキニング結果書き込み用UAV/Resource作成 ---
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Model::VertexData) * vertexCount);
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&skinCluster_.outputVertexResource)
	);

	uint32_t uavIndex = srvManager->Allocate();
	skinCluster_.outputVertexUavHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, uavIndex);
	skinCluster_.outputVertexUavHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, uavIndex);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = vertexCount;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.StructureByteStride = sizeof(Model::VertexData);
	device->CreateUnorderedAccessView(skinCluster_.outputVertexResource.Get(), nullptr, &uavDesc, skinCluster_.outputVertexUavHandle.first);

	skinCluster_.skinnedVertexBufferView.BufferLocation = skinCluster_.outputVertexResource->GetGPUVirtualAddress();
	skinCluster_.skinnedVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Model::VertexData) * vertexCount);
	skinCluster_.skinnedVertexBufferView.StrideInBytes = sizeof(Model::VertexData);

	// --- 定数バッファ (SkinningInformation) の作成 ---
	skinCluster_.skinningInformationResource = dx->CreateBufferResource(sizeof(SkinningInformation));
	skinCluster_.skinningInformationResource->Map(0, nullptr, reinterpret_cast<void**>(&skinCluster_.mappedSkinningInformation));
	skinCluster_.mappedSkinningInformation->numVertices = vertexCount;

	// ========= inverse bind pose =========
	skinCluster_.inverseBindPoseMatrices.resize(skeleton.joints.size());
	std::generate(skinCluster_.inverseBindPoseMatrices.begin(), skinCluster_.inverseBindPoseMatrices.end(), Matrix4x4::MakeIdentity4x4);

	for (const auto& jw : skinData) {
		auto it = skeleton.jointMap.find(jw.first);
		if (it == skeleton.jointMap.end()) continue;

		const int32_t jointIndex = it->second;
		skinCluster_.inverseBindPoseMatrices[jointIndex] = jw.second.inverseBindPoseMatrix;

		for (const auto& vw : jw.second.vertexWeights) {
			if (vw.vertexIndex >= vertexCount) continue;
			auto& inf = skinCluster_.mappedInfluence[vw.vertexIndex];
			for (uint32_t k = 0; k < kNumMaxInfluence; ++k) {
				if (inf.weights[k] == 0.0f) {
					inf.weights[k] = vw.weight;
					inf.jointIndices[k] = jointIndex;
					break;
				}
			}
		}
	}
}
