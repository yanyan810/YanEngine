#include "ParticleTestScene.h"
#include "ParticleTestSceneSupport.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"

#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gParticleTestEditorModeSwitcherVisible;
extern int gParticleTestEditorMode;
extern std::vector<std::string> gParticleTestBlenderHierarchyNames;
extern int gParticleTestBlenderHierarchySelected;
extern bool gParticleTestBlenderHierarchySelectionChanged;
extern bool gParticleTestAnimationCameraPreviewVisible;
extern bool gParticleTestAnimationCameraPreviewSwapped;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;
using namespace ParticleTestSceneSupport;
bool ParticleTestScene::OpenModelFileDialog_(std::string& outModelPath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Model Files (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outModelPath = ToResourceRelativeModelPath(std::filesystem::path(filePath));
    return true;
}

bool ParticleTestScene::OpenModelFileDialog_()
{
    std::string path;
    if (OpenModelFileDialog_(path)) {
        strncpy_s(editorModelPath_, sizeof(editorModelPath_), path.c_str(), _TRUNCATE);
        return true;
    }
    return false;
}

bool ParticleTestScene::OpenTextureFileDialog_(std::string& outTexturePath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Texture Files (*.png;*.jpg;*.jpeg;*.dds;*.tga)\0*.png;*.jpg;*.jpeg;*.dds;*.tga\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outTexturePath = ToResourceRelativeModelPath(std::filesystem::path(filePath));
    return true;
}


bool ParticleTestScene::OpenParticleFileDialog_(std::vector<std::string>& outGroupNames, std::string& outFileName)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Particle JSON Files (*.json)\0*.json\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    if (std::filesystem::exists("Resources/Particles")) {
        openFileName.lpstrInitialDir = "Resources\\Particles";
    } else {
        openFileName.lpstrInitialDir = "resources";
    }
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    std::filesystem::path fullPath(filePath);
    std::string fileName = fullPath.filename().string();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    outGroupNames.clear();
    outFileName = fileName;

    try {
        nlohmann::json root;
        file >> root;
        
        if (root.is_array()) {
            for (const auto& item : root) {
                std::string gName = item.value("name", "");
                if (!gName.empty()) {
                    outGroupNames.push_back(gName);
                }
            }
        } else if (root.is_object()) {
            std::string gName = root.value("name", "");
            if (!gName.empty()) {
                outGroupNames.push_back(gName);
            }
        }

        if (outGroupNames.empty()) {
            outGroupNames.push_back(fullPath.stem().string());
        }

        return true;
    }
    catch (const std::exception&) {
        outGroupNames.push_back(fullPath.stem().string());
        return true;
    }
}

