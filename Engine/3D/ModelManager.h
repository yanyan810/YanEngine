#pragma once
#include "string"
#include "map"
#include "Model.h"
#include "ModelCommon.h"
#include "DirectXCommon.h"

class ModelManager
{

public:

	//初期化
	void Initialize(DirectXCommon* dxCommon);

	//シングルトンインスタンスの取得
	static ModelManager* GetInstance();

	//終了
	void Finalize();

	/// <summary>
	/// モデルファイルの読み込み
	/// </summary>
	/// <param name="filePath">モデルファイルのパス</param>
	void LoadModel(const std::string& filePath);

	/// <summary>
	/// モデルの検索
	/// </summary>
	/// <param name="filePath">モデルのファイルパス</param>
	/// <returns></returns>
	Model* FindModel(const std::string& filePath);

private:
	static ModelManager* instance;


	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(ModelManager&) = delete;
	ModelManager& operator=(ModelManager&) = delete;

	//モデルデータ
	std::map<std::string, std::unique_ptr<Model>> models;

	ModelCommon* modelCommon = nullptr;

};

