#pragma once
#include <memory>
#include "SceneManager.h"
#include "Input.h"
#include "SkyboxCommon.h"

class WinApp;
class DirectXCommon;
class SrvManager;
class SpriteCommon;
class Object3dCommon;
class PrimitiveCommon;
class ParticleCommon;
class ImGuiManagaer;
class SkinningCommon;
#include "RenderManager.h" // PostEffectMode 繧貞・繧ｷ繝ｼ繝ｳ縺ｧ菴ｿ縺医ｋ繧医≧縺ｫ縺吶ｋ

class SceneManager;

class GameApp {
public:
    GameApp();
    ~GameApp();

    int Run();
    void RequestQuit() { quit_ = true; }

    // 蜈ｱ譛峨す繧ｹ繝・Β縺ｫ繧｢繧ｯ繧ｻ繧ｹ・・ameScene 縺九ｉ菴ｿ縺・ｼ・
    WinApp* Win() const { return win_.get(); }
    DirectXCommon* Dx() const { return dx_.get(); }
    SrvManager* Srv() const { return srv_.get(); }
    SpriteCommon* SpriteCom() const { return spriteCommon_.get(); }
    Object3dCommon* ObjCom() const { return objCommon_.get(); }
    PrimitiveCommon* PrimitiveCom() const { return primitiveCommon_.get(); }
    ParticleCommon* ParticleCom() const { return particleCommon_.get(); }
    ImGuiManagaer* ImGui() const { return imgui_.get(); }
    SkyboxCommon* SkyboxCom() const { return skyboxCommon_.get(); }
    SkinningCommon* SkinCom() { return skinCom_.get(); }

    SceneManager& Scenes() { return *sceneMgr_; }

    void Update(float dt);

    void Draw();

    //繝ｬ繝ｳ繝繝ｼ逕ｨ繧ｲ繝・ち繝ｼ
    RenderManager* Render() const { return render_.get(); }

    Input* GetInput() { return input_.get(); }
    const Input* GetInput() const { return input_.get(); }

private:
    bool Initialize_();
    void Finalize_();
    void WarmupAssets_();
private:
    bool quit_ = false;

    std::unique_ptr<WinApp> win_;
    std::unique_ptr<DirectXCommon> dx_;
    std::unique_ptr<SrvManager> srv_;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Object3dCommon> objCommon_;
    std::unique_ptr<PrimitiveCommon> primitiveCommon_;
    std::unique_ptr<ParticleCommon> particleCommon_;
    std::unique_ptr<ImGuiManagaer> imgui_;

    std::unique_ptr<SceneManager> sceneMgr_;
    std::unique_ptr<Input> input_; 
    std::unique_ptr<SkinningCommon> skinCom_;
    std::unique_ptr<SkyboxCommon> skyboxCommon_;

	//RenderManager繧呈戟縺溘○繧・
    std::unique_ptr<RenderManager> render_;

    bool isDebugMode_ = false;

};
