#include "ModelManager.h"
ModelManager* ModelManager::instance = nullptr;

ModelManager* ModelManager::GetInstance() {

	if (instance == nullptr) {
		instance = new ModelManager();
	}
	return instance;
}

void ModelManager::Finalize() {
	if (!instance) return;

	// モデルを片付け
	instance->models.clear();

	// ModelCommon を delete
	delete instance->modelCommon;
	instance->modelCommon = nullptr;

	delete instance;
	instance = nullptr;
}


void ModelManager::Initialize(DirectXCommon* dxCommon) {
	modelCommon = new ModelCommon;
	modelCommon->Initialize(dxCommon);


}

void ModelManager::LoadModel(const std::string& filePath) {

	// すでに読み込み済みならスキップ
	if (models.contains(filePath)) {
		return;
	}

	namespace fs = std::filesystem;
	fs::path p(filePath);

	std::string directoryPath = "resources";
	std::string filename = filePath;

	if (p.has_parent_path()) {
		// 例: filePath = "fence/fence.obj"
		//     p.parent_path() = "fence"
		//     p.filename()    = "fence.obj"
		directoryPath = (fs::path("resources") / p.parent_path()).string(); // "resources/fence"
		filename = p.filename().string();                              // "fence.obj"
	}

	//モデルの生成とファイル読み込み、初期化
	std::unique_ptr<Model> model = std::make_unique<Model>();
	model->Initialize(modelCommon, directoryPath, filename);

	//モデルをmapコンテナに格納する
	models.insert(std::make_pair(filePath, std::move(model)));
}

Model* ModelManager::FindModel(const std::string& filePath) {

	//読み込み済みのモデルを検索
	if (models.contains(filePath)) {
		//読み込みモデルを戻り値としてreturn 
		return models.at(filePath).get();
	}

	//ファイル名一致無し
	return nullptr;

}

Model* ModelManager::CreatePrimitiveModel(const std::string& key, const Model::ModelData& modelData) {

	// すでにあるなら再利用
	if (models.contains(key)) {
		return models.at(key).get();
	}

	// 新規作成
	std::unique_ptr<Model> model = std::make_unique<Model>();
	model->InitializeFromModelData(modelCommon, modelData);

	Model* result = model.get();
	models.insert(std::make_pair(key, std::move(model)));
	return result;
}