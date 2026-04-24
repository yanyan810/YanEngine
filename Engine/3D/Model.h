#pragma once
#include "ModelCommon.h"
#include "MathStruct.h"
#include "TextureManager.h"
#include <format>
#include <filesystem>
#include <fstream>
//#include "Animation.h"
#include <unordered_map>
#include <algorithm>
#include <map>      // std::map
#include "SkinningTypes.h"
#include "AnimationEvaluate.h"

class Model
{

public:

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};



	static_assert(offsetof(Model::VertexData, position) == 0);
	static_assert(offsetof(Model::VertexData, texcoord) == 16);
	static_assert(offsetof(Model::VertexData, normal) == 24);

	struct MaterialData { std::string textureFilePath; };

	struct Joint {
		QuaternionTransform transform;   // bind pose
		Matrix4x4 localMatrix;
		Matrix4x4 skeletonSpaceMatrix;
		std::string name;

		std::vector<int32_t> children;
		int32_t index;//自身のインデックス
		std::optional<int32_t> parent;
	};

	struct Skeleton {
		int32_t root;
		std::unordered_map<std::string, int32_t> jointMap;
		std::vector<Joint> joints;
	};

	//ノード
	struct Node {

		QuaternionTransform transform;
		Matrix4x4 localMatrix = Matrix4x4::MakeIdentity4x4();
		std::string name;
		std::vector<uint32_t> meshIndices; // aiNode::mMeshes を保持（scene->mMeshes の index）
		std::vector<Node> children;
	};

	struct NodeInstance {
		uint32_t nodeIndex; // nodePtrs_ の index
		uint32_t meshIndex; // modelData_.meshes の index
	};

	struct MeshData {
		std::vector<VertexData> vertices; // 読み込み直後のデータ（CPU側）
		uint32_t materialIndex = 0;

		// GPUバッファに詰めた後の範囲
		uint32_t startVertex = 0;
		uint32_t vertexCount = 0;

		// ★追加：IndexBuffer 内での範囲
		uint32_t startIndex = 0;
		uint32_t indexCount = 0;

		bool skinned = false;
	};


	struct VertexWeightData {

		float weight;
		uint32_t vertexIndex;

	};

	struct JointWeightData {

		Matrix4x4 inverseBindPoseMatrix;
		std::vector<VertexWeightData> vertexWeights;

	};

	struct MeshInstance {
		uint32_t meshIndex = 0;     // aiNode->mMeshes[i]
		Matrix4x4 nodeGlobal;       // ノードのグローバル行列
	};

	struct ModelData {
		std::map<std::string, JointWeightData> skinClusterData;
		std::vector<MaterialData> materials;
		std::vector<MeshData> meshes;

		Node rootNode;

		bool hasSkinning = false;

		std::vector<uint32_t> indices;

		// ★追加：アニメーション（Assimpから読めたもの）
		std::unordered_map<std::string, Animation> animations;
		std::vector<MeshInstance> instances;

	};


	struct Material {
		Vector4  color;           // 16
		int32_t  enableLighting;  // 4
		float    pad0[3];         // 12  -> ここで16byte揃う

		Matrix4x4 uvTransform;    // 64

		float    shininess;       // 4
		float environmentCoefficient;
		float    pad1[2];         // 8  -> ここで16byte揃う
	};
	static_assert(sizeof(Material) % 16 == 0, "Material must be 16-byte aligned");

public:

	void Initialize(ModelCommon* modelCommon,
		const std::string& directoryPath,
		const std::string& filename);

	void Draw(ID3D12GraphicsCommandList* cmd);
	//パーティクル用
	void Draw(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount);

	void Draw(ID3D12GraphicsCommandList* cmd,
		uint32_t instanceCount,
		const D3D12_GPU_DESCRIPTOR_HANDLE* overrideSrv);

	void DrawSkinned(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc);
	void DrawSkinnedCompute(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc);

	void DrawMeshIndexed(ID3D12GraphicsCommandList* cmd, uint32_t meshIndex, uint32_t instanceCount);
	
	void DrawSkinnedOneMesh(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc, uint32_t meshIndex);

	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

	static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	static ModelData    LoadAssimpFile(const std::string& fullPath);

	Vector4& GetMaterialColor()
	{
		static Vector4 dummy{ 1,1,1,1 };
		if (!materialData_) {
			OutputDebugStringA("[Model] GetMaterialColor returns dummy (materialData_ is null)\n");
			return dummy;
		}
		return materialData_->color;
	}

	void SetMaterialColor(const Vector4& c)
	{
		if (!materialData_) {
			OutputDebugStringA("[Model] SetMaterialColor skipped (materialData_ is null)\n");
			return;
		}
		materialData_->color = c;
	}

	Material* GetMaterial() { return materialData_; }

	const Matrix4x4& GetRootLocalMatrix() const;

	//modeldataを取得
	const std::unordered_map<std::string, Animation>& GetAnimations() const {
		return modelData_.animations;
	}

	bool HasSkinning() const { return modelData_.hasSkinning; }

	const Skeleton& GetSkeleton() const { return skeleton_; }

	static void UpdateSkeleton(Skeleton& skeleton);

	// 頂点数（Influence配列サイズ用）
	uint32_t GetVertexCount() const {
		uint32_t total = 0;
		for (const auto& m : modelData_.meshes) { total += m.vertexCount; }
		// もし vertexCount をまだ埋めてないなら meshes[i].vertices.size() にする
		if (total == 0) {
			for (const auto& m : modelData_.meshes) { total += (uint32_t)m.vertices.size(); }
		}
		return total;
	}

	// skinClusterData（jointName -> weights/invBind）
	const std::map<std::string, JointWeightData>& GetSkinClusterData() const {
		return modelData_.skinClusterData;
	}

	const std::vector<NodeInstance>& GetNodeInstances() const { return nodeInstances_; }

	// anim==nullptr なら「元のノードTransform（bind/local）」で計算
	void ComputeNodeGlobalMatrices(const Animation* anim, float time,
		std::vector<Matrix4x4>& outGlobals) const;

	// mesh 1個だけ描く（TransformCB/PSOは外側でセット済み前提）
	void DrawOneMesh(ID3D12GraphicsCommandList* cmd, uint32_t meshIndex, uint32_t texRootParam);

	// ---- getters for binding ----
	const D3D12_VERTEX_BUFFER_VIEW& GetVBV() const { return vertexBufferView_; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetVertexResource() const { return vertexResource_; }

	// Indexあり前提。無い場合に備えて bool も用意
	const D3D12_INDEX_BUFFER_VIEW& GetIBV() const { return indexBufferView_; }
	bool HasIndexBuffer() const { return indexResource_ != nullptr; }

	// Material CBV の GPU仮想アドレス
	D3D12_GPU_VIRTUAL_ADDRESS GetMaterialCBV() const {
		return materialResource_ ? materialResource_->GetGPUVirtualAddress() : 0;
	}

	int32_t GetMeshOwnerNodeIndex(uint32_t meshIndex) const {
		if (meshIndex >= meshOwnerNodeIndex_.size()) return -1;
		return meshOwnerNodeIndex_[meshIndex];
	}

	uint32_t GetMeshCount() const {
		return (uint32_t)modelData_.meshes.size();
	}

	bool IsMeshSkinned(uint32_t meshIndex) const;

	int32_t FindNodeIndexByName(const std::string& name) const {
		auto it = nodeNameToIndex_.find(name);
		if (it == nodeNameToIndex_.end()) return -1;
		return (int32_t)it->second;
	}

	const std::string& GetNodeName(int nodeIndex) const {
		static const std::string kEmpty = "";
		if (nodeIndex < 0 || nodeIndex >= (int)nodePtrs_.size() || nodePtrs_[nodeIndex] == nullptr) {
			return kEmpty;
		}
		return nodePtrs_[nodeIndex]->name;
	}

	// ノードのワールド行列を取得
	Matrix4x4 GetNodeWorldMatrix(int nodeIndex) const;

	// ModelDataを直接入れる
	void InitializeFromModelData(ModelCommon* modelCommon, const ModelData& modelData);

private:
	static Skeleton CreateSkeleton(const Node& rootNode);

	static int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints);

	void BuildNodeRuntime_(); // LoadAssimpFile後に呼ぶ
	void TraverseNode_(const Node* n, int32_t parent);

	void DebugValidateAnimationTracks_() const;

private:

	ModelCommon* modelCommon_;

	ModelData modelData_;

	// 頂点データ（バッファ）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;   // 頂点リソース
	VertexData* vertexData_ = nullptr;              // 頂点データのCPU側ポインタ
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};             // 頂点バッファビュー

	//マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	//マテリアルにデータを書き込む
	Material* materialData_ = nullptr;

	Skeleton skeleton_;

	// Indexデータ（バッファ）★追加
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	uint32_t* indexData_ = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	// ===== ノードをフラット管理するランタイム =====
	struct NodeRuntime {
		struct FlatNode {
			std::string name;
			QuaternionTransform transform;
			Matrix4x4 localMatrix = Matrix4x4::MakeIdentity4x4();
			int32_t parent = -1;
		};
		std::vector<FlatNode> nodes;
	};

	NodeRuntime nodeRuntime_;

	std::vector<const Node*> nodePtrs_;
	std::vector<int32_t> parentIndex_;
	std::unordered_map<std::string, uint32_t> nodeNameToIndex_;
	std::vector<NodeInstance> nodeInstances_;
	std::vector<int32_t> meshOwnerNodeIndex_;

};

