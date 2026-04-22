#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
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

#include <d3d12.h>

GameScene::~GameScene() = default;

static float RandRange(float min, float max) {
    return min + (max - min) * (float(rand()) / float(RAND_MAX));
}

void GameScene::OnEnter(GameApp& app) {
    // テクスチャやモデルのロード（必要なものをここで）
  //  TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

    input_ = app.GetInput();
    assert(input_); // ここでnullなら初期化順が悪い

    camera_ = std::make_unique<Camera>();

    // 斜め上・少し後ろ
    camera_->SetTranslate({
        0.0f,   // X
        20.0f,   // Y（高さ）
       -50.0f   // Z（後ろ）
        });

    // 少し下向き（ラジアン）
    camera_->SetRotate({
        0.35f,  // X回転（見下ろし）
        0.0f,   // Y
        0.0f
        });


    // 近接3（0,1,2秒）
    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Melee, 1.0f * i);
    }

    // シューター3（0.5,1.5,2.5秒）
    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Shooter, 0.5f + 1.0f * i);
    }


    // ObjCommon は GameApp が持つ。カメラ設定だけここで。
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
    hpBack_->SetScale({ 300.0f, 20.0f ,1.0f}); // 背景バー

    hpFill_ = std::make_unique<Sprite>();
    hpFill_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    hpFill_->SetPosition({ 30.0f, 30.0f });
    hpFill_->SetScale({ 300.0f, 20.0f,1.0f }); // 中身バー（毎フレーム幅変える）

    // 0..9ロード
    for (int i = 0; i < 10; ++i) {
        char path[256];
        sprintf_s(path, "resources/ui/num/%d.png", i);
        TextureManager::GetInstance()->LoadTexture(path);
    }

    // 3桁分Sprite作る
    for (int i = 0; i < 3; ++i) {
        hpDigits_[i] = std::make_unique<Sprite>();
        hpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");

        // アンカーを左上にすると位置合わせが楽（好み）
        hpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
    }

    for (int i = 0; i < 3; ++i) {
        if (!hpDigits_[i]) continue;
        hpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f }); // ★黒
    }

    hpBack_->SetColor({ 0.2f, 0.2f, 0.2f, 1.0f }); // 濃いグレー
    hpFill_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 白

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

    // ★スカイドームは基本「ライト無視」
    skyDome_->SetEnableLighting(0);              // ← あなたの仕様の「無照明モード」に合わせて
    skyDome_->SetMaterialColor({ 1,1,1,1 });       // 念のため
    skyDome_->SetShininess(1.0f);                // 影響しないけど保険
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

    // 数字 3桁
    for (int i = 0; i < 3; ++i) {
        bossHpDigits_[i] = std::make_unique<Sprite>();
        bossHpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");
        bossHpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
        bossHpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    // ---- Intro Phase 初期化 ----
    phase_ = Phase::IntroVideo;
    introFrame_ = 0;
    introTime_ = 0.0f;

    // ---- Video Plane ----
    videoPlane_ = std::make_unique<Object3d>();
    videoPlane_->Initialize(app.ObjCom(), app.Dx());
    videoPlane_->SetCamera(camera_.get());
    videoPlane_->SetModel("video/plane.obj");

    // 動画は「ライト無視」が基本（色そのまま出したい）
    videoPlane_->SetEnableLighting(0);
    videoPlane_->SetMaterialColor({ 1,1,1,1 });
    videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    // 位置：カメラ前に置く（あなたのカメラ向きに合わせて調整）
    // GameSceneカメラは俯瞰なので、地面に貼るようにするなら回転させてもOK
    videoPlane_->SetTranslate({ 0.0f, 3.0f, 3.1f });
    videoPlane_->SetScale({ 9.5f, 5.3f, 2.0f });
    videoPlane_->SetRotate({ 0.0f, 3.14f, 0.0f });

    // ---- Video Player ----
    video_ = std::make_unique<VideoPlayerMF>();
    video_->Open("resources/video/battle.mp4", false);
    video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
    video_->SetVolume(1.0f);

    // ★最初のフレームを必ず作る
    video_->ReadNextVideoFrame();

    // 最初の1枚
    if (!video_->ReadNextFrame()) {
        OutputDebugStringA("[GameScene] First ReadNextFrame failed\n");
    }

    enableVideo_ = true;

    srtVideo_.pos = { 0.7f, 5.5f, -10.1f };
    srtVideo_.rot = { -0.38f, -3.14f, 0.0f };
    srtVideo_.scale = { 18.1f, 9.73f, 2.0f };

    // 1回反映（任意。Updateで毎回反映してもOK）
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

     // 初期
     isPaused_ = false;
     prevTab_ = false;
     pauseSel_ = PauseSel::Close;


}

void GameScene::OnExit(GameApp& /*app*/) {
    player_.reset(); // ★追加
    particle_.reset();
    objB_.reset();
    objA_.reset();
    sprite_.reset();
    camera_.reset();
    debugTitleParticle_.reset();
    pauseClose_.reset();
    pauseToTitle_.reset();

}


void GameScene::Update(GameApp& app, float dt) {
    // ESC で終了（Input クラス持ってるなら差し替え）
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        app.RequestQuit();
        return;
    }

    if (!input_) return; // 念のため

    camera_->Update();

    ground_->Update(dt);

    skyDome_->Update(dt);

    // ----------------------------
    // Intro Video フェーズ
    // ----------------------------
    if (phase_ == Phase::IntroVideo) {

        bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool enterNow = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;

        bool spaceTrig = spaceNow && !prevSpace_;
        bool enterTrig = enterNow && !prevEnter_;

        // dt方式（推奨）
        introTime_ += dt;
        if (introTime_ >= kIntroSeconds_|| enterTrig|| spaceTrig) {
            phase_ = Phase::Battle;

            // 戦闘開始時に「最初のスポーンをここでやる」ならここに置く
            // enemyMgr_.QueueSpawn(...) をここでやる / BGM開始などもここ
        }

        // フレーム方式（60fps固定前提）でやるなら↓
        // introFrame_++;
        // if (introFrame_ >= kIntroFrames_) { phase_ = Phase::Battle; }

        // 動画の更新（映像 + 音）
        if (enableVideo_ && video_) {
            // もしあなたのVideoPlayerMFがこの名前なら

            videoPlane_->Update(dt);

            // あなたが今使ってる形に合わせるなら：
            video_->ReadNextVideoFrame(); // 1フレーム進める（映像+音の実装によりけり）
            video_->PumpAudio();

        }

        prevSpace_ = spaceNow;
        prevEnter_ = enterNow;

    } else if (phase_ == Phase::Battle) {

        // --- TABでポーズ切替（Battle中のみ）---
        bool tabNow = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
        bool tabTrig = tabNow && !prevTab_;
        prevTab_ = tabNow;

        if (tabTrig) {
            isPaused_ = !isPaused_;
            // 開いた時は選択をCloseに戻す（好み）
            if (isPaused_) pauseSel_ = PauseSel::Close;
        }

        if (isPaused_) {

            // 左右で選択（A/D or ←/→）
            bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState('A') & 0x8000) != 0;
            bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0;

            if (left)  pauseSel_ = PauseSel::Close;
            if (right) pauseSel_ = PauseSel::ToTitle;

            // 見た目（選択してる方を明るく）
            if (pauseClose_ && pauseToTitle_) {
                pauseClose_->SetColor(pauseSel_ == PauseSel::Close ? pauseNormal_ : pauseDim_);
                pauseToTitle_->SetColor(pauseSel_ == PauseSel::ToTitle ? pauseNormal_ : pauseDim_);
            }

            // 決定（Enter/Space）
            bool enterNow = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

            // 連打で暴れないように「トリガ」扱いしたいなら prevEnter_/prevSpace_ を流用してOK
            bool enterTrig = enterNow && !prevEnter_;
            bool spaceTrig = spaceNow && !prevSpace_;
            prevEnter_ = enterNow;
            prevSpace_ = spaceNow;

            if (enterTrig || spaceTrig) {
                if (pauseSel_ == PauseSel::Close) {
                    isPaused_ = false;
                } else {
                    // タイトルへ
                    RequestChangeScene_("Title");
                    return;
                }
            }

            // ★ここで return するので、プレイヤー/敵/タイマーが進まない
            return;
        }


        if (player_) {
            player_->Update(dt, *input_, enemyMgr_);
        }


    // enemyMgr_ に渡す playerPos を Player から取る
        Vector2 playerPos2D{ 0.0f, 0.0f };
        if (player_) {
            playerPos2D = player_->GetPos2D();
        }

        float playerZ = 15.0f;
        if (player_) {
            playerZ = player_->GetZ(); // 追加したgetter
        }
        enemyMgr_.Update(dt, playerPos2D, playerZ, *player_);

        if (bossHpFill_) {
            if (Enemy* boss = enemyMgr_.GetBoss()) {
                int hp = boss->GetHP();
                int maxHp = boss->GetMaxHP();

                float t = (maxHp > 0) ? float(hp) / float(maxHp) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);

                bossHpFill_->SetScale({ bossHpBarW_ * t, bossHpBarH_, 1.0f });

                UpdateBossHPDigits_(hp);
            } else {
                // ボスがいない（倒した後など）→UI消す
                bossHpFill_->SetScale({ 0.0f, bossHpBarH_, 1.0f });
                UpdateBossHPDigits_(0);
            }
        }


        if (player_ && hpFill_) {
            int hp = player_->GetHP();      // ← getter作る（無ければ）
            int maxHp = player_->GetMaxHP();// ← getter作る（無ければ）

            float t = (maxHp > 0) ? (float(hp) / float(maxHp)) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            const float fullW = 300.0f;
            hpFill_->SetScale({ fullW * t, 20.0f ,1.0f });
        }

        if (player_) {
            UpdateHPDigits_(player_->GetHP());
        }

        if (player_->IsDead()) {
            RequestChangeScene_("GameOver");
            return;
        }

        if (enemyMgr_.IsBossDefeated()) {

            // ★ Outro開始
            phase_ = Phase::OutroVideo;
            outroTime_ = 0.0f;

            enableVideo_ = true;

            // 動画を outro に差し替え（デコーダ状態をリセット）
            if (!video_) {
                video_ = std::make_unique<VideoPlayerMF>();
            } else {
                video_->Close();
            }

            video_->Open("resources/video/outro.mp4", false); // ★ここ
            video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
            video_->SetVolume(1.0f);

            // ★最初のフレーム準備
            video_->ReadNextVideoFrame();
            video_->ReadNextFrame();

            // （任意）UIを消したいならここでバーを隠すなど
            // 例：bossHpFill_->SetScale({0, bossHpBarH_, 1});

            return;
        }


    } else if (phase_ == Phase::OutroVideo) {

        // 動画進行
        if (enableVideo_ && video_) {
            videoPlane_->Update(dt);
            video_->ReadNextVideoFrame();
            video_->PumpAudio();
        }

        // 秒数で終了（まずはこれが最速）
        outroTime_ += dt;
        if (outroTime_ >= kOutroSeconds_) {
            RequestChangeScene_("GameClear");
            return;
        }

        // （任意）スペースでスキップ
         if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
             RequestChangeScene_("GameClear");
             return;
         }
    }


#ifdef USE_IMGUI


    if (phase_ == Phase::IntroVideo && videoPlane_) {
        videoPlane_->SetTranslate(srtVideo_.pos);
        videoPlane_->SetRotate(srtVideo_.rot);
        videoPlane_->SetScale(srtVideo_.scale);
    }

    ImGui::Begin("VideoPlane SRT");
    ImGui::DragFloat3("T", &srtVideo_.pos.x, 0.1f);
    ImGui::DragFloat3("R", &srtVideo_.rot.x, 0.01f);
    ImGui::DragFloat3("S", &srtVideo_.scale.x, 0.1f);
    ImGui::End();


#endif // USE_IMGUI


}

void GameScene::Draw(GameApp& app) {
    auto* dx = app.Dx();
    auto* srv = app.Srv();
    auto* cmd = dx->GetCommandList();


    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 3D
    //if (objA_) objA_->Draw();
    //if (objB_) objB_->Draw();

     skyDome_->Draw();
   
    // --- Intro中は動画を最前面に出したいなら、描画順を最後にする ---
     if (phase_ == Phase::IntroVideo || phase_ == Phase::OutroVideo) {
         if (enableVideo_ && video_ && videoPlane_) {

             ID3D12DescriptorHeap* heaps[] = { app.Srv()->GetDescriptorHeap() };
             cmd->SetDescriptorHeaps(_countof(heaps), heaps);

             video_->UploadToGpu(cmd);

             D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
             videoPlane_->DrawWithOverrideSrv(vh);

             video_->EndFrame(cmd);
         }
     } else if (phase_ == Phase::Battle) {
        // 戦闘フェーズ中は普通に描画


        if (ground_) ground_->Draw();

        //player
        if (player_) player_->Draw();

        enemyMgr_.Draw();

    }

    // Particle
    app.ParticleCom()->SetGraphicsPipelineState();
  
    //

    // 2D sprite
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (phase_ == Phase::Battle) {

        // ★ 背景
        if (hpBack_) {
            hpBack_->Update(view, proj);
            hpBack_->Draw();
        }
        // ★ 中身
        if (hpFill_) {
            hpFill_->Update(view, proj);
            hpFill_->Draw();
        }

        // 数字（後 = 上に乗る）
        for (int i = 0; i < 3; ++i) {
            if (!hpDigits_[i]) continue;
            hpDigits_[i]->Update(view, proj);
            hpDigits_[i]->Draw();
        }
        // ===== Boss HP（追加）=====
        if (bossHpBack_) {
            bossHpBack_->Update(view, proj);
            bossHpBack_->Draw();
        }
        if (bossHpFill_) {
            bossHpFill_->Update(view, proj);
            bossHpFill_->Draw();
        }
        for (int i = 0; i < 3; ++i) {
            if (!bossHpDigits_[i]) continue;
            bossHpDigits_[i]->Update(view, proj);
            bossHpDigits_[i]->Draw();
        }

        // ---- Pause UI ----
        if (isPaused_) {
            if (pauseClose_) {
                pauseClose_->Update(view, proj);
                pauseClose_->Draw();
            }
            if (pauseToTitle_) {
                pauseToTitle_->Update(view, proj);
                pauseToTitle_->Draw();
            }
        }


    }
   
}

void GameScene::SpawnEnemyFromOutside_(EnemyType type) {
    // ===== 画面範囲（調整用） =====
    const float screenLeft = -12.0f;
    const float screenRight = 12.0f;
    const float outsidePad = 3.0f;   // 画面外にどれだけ出すか
    const float z = 15.0f;

    // 左 or 右 をランダム
    bool fromLeft = (rand() % 2) == 0;

    float x;
    if (fromLeft) {
        x = RandRange(screenLeft - outsidePad - 3.0f,
            screenLeft - outsidePad);
    } else {
        x = RandRange(screenRight + outsidePad,
            screenRight + outsidePad + 3.0f);
    }

    // Y は少しランダムに
    float y = RandRange(-1.0f, 1.0f);

    enemyMgr_.Spawn(type, Vector3{ x, y, z });
}

void GameScene::UpdateHPDigits_(int hp) {
    hp = std::clamp(hp, 0, 999);

    int d0 = (hp / 100) % 10;
    int d1 = (hp / 10) % 10;
    int d2 = (hp / 1) % 10;

    // 表示する桁（先頭ゼロ消し）
    bool show0 = (hp >= 100);
    bool show1 = (hp >= 10);
    bool show2 = true;

    // 右詰めでバーの上に置く（例：バー右端付近）
    const float baseX = 128.0f*1.5f; // ★バー右端
    const float baseY = hpBarPos_.y - 18.0f;              // ★バーの上

    const float w = 16.0f;     // 1桁幅（画像に合わせて調整）
    const float h = 20.0f;     // 1桁高さ
    const float sp = 2.0f;     // 桁間

    auto setDigit = [&](int idx, int digit, float x, float y, bool visible) {
        if (!hpDigits_[idx]) return;
        if (!visible) {
            hpDigits_[idx]->SetPosition({ -9999.0f, -9999.0f });
            return;
        }
        char path[256];
        sprintf_s(path, "resources/ui/num/%d.png", digit);
        hpDigits_[idx]->SetTextureFilePath(path);

        hpDigits_[idx]->SetPosition({ x, y });
        hpDigits_[idx]->SetScale({ 1.0f, 1.0f, 1.0f }); // 必要なら
     
        };

    // 右詰め配置：一の位を一番右
    float x2 = baseX - (w);
    float x1 = x2 - (w + sp);
    float x0 = x1 - (w + sp);

    setDigit(0, d0, x0, baseY, show0);
    setDigit(1, d1, x1, baseY, show1);
    setDigit(2, d2, x2, baseY, show2);

    // もし数字がデカいなら
    for (int i = 0; i < 3; ++i) {
        if (hpDigits_[i]) hpDigits_[i]->SetScale({ 0.5f, 0.5f, 1.0f }); // 好みで調整
    }
}

void GameScene::UpdateBossHPDigits_(int hp)
{
    hp = std::clamp(hp, 0, 999);

    int d0 = (hp / 100) % 10;
    int d1 = (hp / 10) % 10;
    int d2 = (hp / 1) % 10;

    bool show0 = (hp >= 100);
    bool show1 = (hp >= 10);
    bool show2 = true;

    const float baseX = bossHpBarPos_.x + bossHpBarW_ - 8.0f; // バー右端寄せ
    const float baseY = bossHpBarPos_.y - 18.0f;

    const float w = 16.0f;
    const float sp = 2.0f;

    auto setDigit = [&](int idx, int digit, float x, float y, bool visible) {
        if (!bossHpDigits_[idx]) return;
        if (!visible) {
            bossHpDigits_[idx]->SetPosition({ -9999.0f, -9999.0f });
            return;
        }
        char path[256];
        sprintf_s(path, "resources/ui/num/%d.png", digit);
        bossHpDigits_[idx]->SetTextureFilePath(path);
        bossHpDigits_[idx]->SetPosition({ x-130, y });
        bossHpDigits_[idx]->SetScale({ 0.7f, 0.7f, 1.0f });
        };

    float x2 = baseX - (w);
    float x1 = x2 - (w + sp);
    float x0 = x1 - (w + sp);

    setDigit(0, d0, x0, baseY, show0);
    setDigit(1, d1, x1, baseY, show1);
    setDigit(2, d2, x2, baseY, show2);
}
