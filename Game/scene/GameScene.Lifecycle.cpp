#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
#include "DebugAI/IGameDebugAdapter.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <d3d12.h>

GameScene::~GameScene() = default;

static float RandRange(float min, float max) {
    return min + (max - min) * (float(rand()) / float(RAND_MAX));
}

void GameScene::OnEnter(GameApp& app) {
    debugRandomSeed_ = static_cast<unsigned int>(std::time(nullptr));
    std::srand(debugRandomSeed_);

    // 繝・け繧ｹ繝√Ε繧・Δ繝・Ν縺ｮ繝ｭ繝ｼ繝会ｼ亥ｿ・ｦ√↑繧ゅ・繧偵％縺薙〒・・
  //  TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

    input_ = app.GetInput();
    assert(input_); // 縺薙％縺ｧnull縺ｪ繧牙・譛溷喧鬆・′謔ｪ縺・

    camera_ = std::make_unique<Camera>();

    // 譁懊ａ荳翫・蟆代＠蠕後ｍ
    camera_->SetTranslate({
        0.0f,   // X
        20.0f,   // Y・磯ｫ倥＆・・
       -50.0f   // Z・亥ｾ後ｍ・・
        });

    // 蟆代＠荳句髄縺搾ｼ医Λ繧ｸ繧｢繝ｳ・・
    camera_->SetRotate({
        0.35f,  // X蝗櫁ｻ｢・郁ｦ倶ｸ九ｍ縺暦ｼ・
        0.0f,   // Y
        0.0f
        });


    // 霑第磁3・・,1,2遘抵ｼ・
    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Melee, 1.0f * i);
    }

    // 繧ｷ繝･繝ｼ繧ｿ繝ｼ3・・.5,1.5,2.5遘抵ｼ・
    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Shooter, 0.5f + 1.0f * i);
    }


    // ObjCommon 縺ｯ GameApp 縺梧戟縺､縲ゅき繝｡繝ｩ險ｭ螳壹□縺代％縺薙〒縲・
    app.ObjCom()->SetDefaultCamera(camera_.get());

    sprite_ = std::make_unique<Sprite>();
   // sprite_->Initialize(app.SpriteCom(), app.Dx(), "resources/uvChecker.png");
    sprite_->AdjustTextureSize();

    //player
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    player_->SetSpawnPos({ -12.0f, 0.0f, 5.0f });

    //enemy
    enemyMgr_.Initialize(app.ObjCom(), app.Dx(), camera_.get());

    enemyMgr_.Spawn(EnemyType::Boss, Vector3{ 0.0f, 0.0f, 5.0f });

    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

    hpBack_ = std::make_unique<Sprite>();
    hpBack_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    hpBack_->SetPosition({ 30.0f, 30.0f });
    hpBack_->SetScale({ 300.0f, 20.0f ,1.0f}); // 閭梧勹繝舌・

    hpFill_ = std::make_unique<Sprite>();
    hpFill_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    hpFill_->SetPosition({ 30.0f, 30.0f });
    hpFill_->SetScale({ 300.0f, 20.0f,1.0f }); // 荳ｭ霄ｫ繝舌・・域ｯ弱ヵ繝ｬ繝ｼ繝蟷・､峨∴繧具ｼ・

    // 0..9繝ｭ繝ｼ繝・
    for (int i = 0; i < 10; ++i) {
        char path[256];
        sprintf_s(path, "resources/ui/num/%d.png", i);
        TextureManager::GetInstance()->LoadTexture(path);
    }

    // 3譯∝・Sprite菴懊ｋ
    for (int i = 0; i < 3; ++i) {
        hpDigits_[i] = std::make_unique<Sprite>();
        hpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");

        // 繧｢繝ｳ繧ｫ繝ｼ繧貞ｷｦ荳翫↓縺吶ｋ縺ｨ菴咲ｽｮ蜷医ｏ縺帙′讌ｽ・亥･ｽ縺ｿ・・
        hpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
    }

    for (int i = 0; i < 3; ++i) {
        if (!hpDigits_[i]) continue;
        hpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f }); // 笘・ｻ・
    }

    hpBack_->SetColor({ 0.2f, 0.2f, 0.2f, 1.0f }); // 豼・＞繧ｰ繝ｬ繝ｼ
    hpFill_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 逋ｽ

    auto* mgr = ModelManager::GetInstance();
    mgr->LoadModel("ground/bossGround.obj");

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/bossGround.obj");

    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
    //ground_->Update(0.0f);
    ground_->SetEnableLighting(0);

    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");
    skyDome_->SetCamera(camera_.get());

    skyDome_->SetTranslate({ 0.0f, 0.0f, 0.0f });  
    skyDome_->SetRotate({ 0.0f, 0.0f, 0.0f });
    skyDome_->SetScale({ 5.0f, 5.0f, 5.0f });

    // 笘・せ繧ｫ繧､繝峨・繝縺ｯ蝓ｺ譛ｬ縲後Λ繧､繝育┌隕悶・
    skyDome_->SetEnableLighting(0);              // 竊・縺ゅ↑縺溘・莉墓ｧ倥・縲檎┌辣ｧ譏弱Δ繝ｼ繝峨阪↓蜷医ｏ縺帙※
    skyDome_->SetMaterialColor({ 1,1,1,1 });       // 蠢ｵ縺ｮ縺溘ａ
    skyDome_->SetShininess(1.0f);                // 蠖ｱ髻ｿ縺励↑縺・￠縺ｩ菫晞匱
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);
    skyDome_->Update(0.0f);

    // ===== Boss HP UI =====
    bossHpBack_ = std::make_unique<Sprite>();
    bossHpBack_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    bossHpBack_->SetPosition({ bossHpBarPos_.x, bossHpBarPos_.y });
    bossHpBack_->SetScale({ bossHpBarW_, bossHpBarH_, 1.0f });

    bossHpFill_ = std::make_unique<Sprite>();
    bossHpFill_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    bossHpFill_->SetPosition({ bossHpBarPos_.x, bossHpBarPos_.y });
    bossHpFill_->SetScale({ bossHpBarW_, bossHpBarH_, 1.0f });

    bossHpBack_->SetColor({ 1.8f, 0.0f, 0.0f, 1.0f });
    bossHpFill_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

    // 謨ｰ蟄・3譯・
    for (int i = 0; i < 3; ++i) {
        bossHpDigits_[i] = std::make_unique<Sprite>();
        bossHpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");
        bossHpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
        bossHpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    // ---- Intro Phase 蛻晄悄蛹・----
    phase_ = Phase::IntroVideo;
    introFrame_ = 0;
    introTime_ = 0.0f;

    // ---- Video Plane ----
    videoPlane_ = std::make_unique<Object3d>();
    videoPlane_->Initialize(app.ObjCom(), app.Dx());
    videoPlane_->SetCamera(camera_.get());
    videoPlane_->SetModel("video/plane.obj");

    // 蜍慕判縺ｯ縲後Λ繧､繝育┌隕悶阪′蝓ｺ譛ｬ・郁牡縺昴・縺ｾ縺ｾ蜃ｺ縺励◆縺・ｼ・
    videoPlane_->SetEnableLighting(0);
    videoPlane_->SetMaterialColor({ 1,1,1,1 });
    videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    // 菴咲ｽｮ・壹き繝｡繝ｩ蜑阪↓鄂ｮ縺擾ｼ医≠縺ｪ縺溘・繧ｫ繝｡繝ｩ蜷代″縺ｫ蜷医ｏ縺帙※隱ｿ謨ｴ・・
    // GameScene繧ｫ繝｡繝ｩ縺ｯ菫ｯ迸ｰ縺ｪ縺ｮ縺ｧ縲∝慍髱｢縺ｫ雋ｼ繧九ｈ縺・↓縺吶ｋ縺ｪ繧牙屓霆｢縺輔○縺ｦ繧０K
    videoPlane_->SetTranslate({ 0.0f, 3.0f, 3.1f });
    videoPlane_->SetScale({ 9.5f, 5.3f, 2.0f });
    videoPlane_->SetRotate({ 0.0f, 3.14f, 0.0f });

    // ---- Video Player ----
    video_ = std::make_unique<VideoPlayerMF>();
    video_->Open("resources/video/battle.mp4", false);
    video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
    video_->SetVolume(1.0f);

    // 笘・怙蛻昴・繝輔Ξ繝ｼ繝繧貞ｿ・★菴懊ｋ
    video_->ReadNextVideoFrame();

    // 譛蛻昴・1譫・
    if (!video_->ReadNextFrame()) {
        OutputDebugStringA("[GameScene] First ReadNextFrame failed\n");
    }

    enableVideo_ = true;

    srtVideo_.pos = { 0.7f, 5.5f, -10.1f };
    srtVideo_.rot = { -0.38f, -3.14f, 0.0f };
    srtVideo_.scale = { 18.1f, 9.73f, 2.0f };

    // 1蝗槫渚譏・井ｻｻ諢上６pdate縺ｧ豈主屓蜿肴丐縺励※繧０K・・
    videoPlane_->SetTranslate(srtVideo_.pos);
    videoPlane_->SetRotate(srtVideo_.rot);
    videoPlane_->SetScale(srtVideo_.scale);

    videoPlane_->Update(0.0f);

     prevSpace_ = false;
     prevEnter_ = false;

     // ---- Pause UI textures ----
     TextureManager::GetInstance()->LoadTexture("resources/ui/char/close.png");
     TextureManager::GetInstance()->LoadTexture("resources/ui/char/goTitle.png");

     // ---- Pause UI sprites ----
     pauseClose_ = std::make_unique<Sprite>();
     pauseClose_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/close.png");
     pauseClose_->AdjustTextureSize();
     pauseClose_->SetAnchorPoint({ 0.0f, 0.0f });
     pauseClose_->SetScale({ 1.0f, 1.0f, 1.0f });
     pauseClose_->SetPosition({ pausePosClose_.x, pausePosClose_.y });
     pauseClose_->SetColor(pauseNormal_);

     pauseToTitle_ = std::make_unique<Sprite>();
     pauseToTitle_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/goTitle.png");
     pauseToTitle_->AdjustTextureSize();
     pauseToTitle_->SetAnchorPoint({ 0.0f, 0.0f });
     pauseToTitle_->SetScale({ 1.0f, 1.0f, 1.0f });
     pauseToTitle_->SetPosition({ pausePosTitle_.x, pausePosTitle_.y });
     pauseToTitle_->SetColor(pauseDim_);

     // 蛻晄悄
     isPaused_ = false;
     prevTab_ = false;
     pauseSel_ = PauseSel::Close;

     SetupDebugAI_(app);

}

void GameScene::OnExit(GameApp& app) {
    ShutdownDebugAI_(app);

    if (auto* input = app.GetInput()) {
        input->SetCameraControlEnabled(false);
    }
    app.Render()->SetMode(PostEffectMode::FullScreen);
    isPaused_ = false;

    player_.reset(); // 笘・ｿｽ蜉
    particle_.reset();
    objB_.reset();
    objA_.reset();
    sprite_.reset();
    camera_.reset();
    debugTitleParticle_.reset();
    pauseClose_.reset();
    pauseToTitle_.reset();

}


