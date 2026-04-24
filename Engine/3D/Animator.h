#pragma once
#include <string>
#include <vector>
#include <map>
#include "Model.h"
#include "SkinningTypes.h"
#include "SrvManager.h"
#include "DirectXCommon.h"

class Animator {
public:
	void Initialize(Model* model);

	void PlayAnimation(const std::string& animName = "", bool loop = true);
	void StopAnimation() { isPlayAnimation_ = false; }
	void SetAnimationNodeName(const std::string& node) { playingNodeName_ = node; }

	bool IsAnimationFinished() const;
	bool HasAnimation() const;
	const std::string& GetPlayingAnimName() const { return playingAnimName_; }
	float GetTime() const { return animationTime_; }

	void Update(float dt);

	void UpdateSkinCluster(DirectXCommon* dx);

	SkinCluster& GetSkinCluster() { return skinCluster_; }
	const SkinCluster& GetSkinCluster() const { return skinCluster_; }

	Model::Skeleton& GetPoseSkeleton() { return poseSkeleton_; }
	const Model::Skeleton& GetPoseSkeleton() const { return poseSkeleton_; }

	bool IsPoseReady() const { return poseReady_; }

	// スキンクラスター生成
	void CreateSkinCluster(
		ID3D12Device* device,
		DirectXCommon* dx,
		SrvManager* srvManager,
		ID3D12DescriptorHeap* srvHeap,
		uint32_t descriptorSize);

	void SetSrvManager(SrvManager* srv) { srvManager_ = srv; }

private:
	void ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time);

private:
	Model* model_ = nullptr;
	SrvManager* srvManager_ = nullptr;

	bool isPlayAnimation_ = false;
	float animationTime_ = 0.0f;
	std::string playingAnimName_;
	std::string playingNodeName_ = "root";
	bool loop_ = true;

	bool poseReady_ = false;

	Model::Skeleton poseSkeleton_;
	SkinCluster skinCluster_;
};
