#pragma once
#include "IScene.h"
#include <memory>

#include "Particle.h"
#include "Camera.h"
#include "Sprite.h"
#include "Object3d.h"
#include "Input.h"

#include "PlayerCombo.h"

#include "Enemy.h"
#include "Player.h"


#include "VideoPlayerMF.h"

class GameScene : public IScene {
public:
    GameScene() = default;
    ~GameScene(); // ★追加：ここが重要


    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;

    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;

    void SpawnEnemyFromOutside_(EnemyType type);

    void  UpdateHPDigits_(int hp);

    void UpdateBossHPDigits_(int hp);

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Sprite> sprite_;
    std::unique_ptr<Object3d> objA_;
    std::unique_ptr<Object3d> objB_;
    std::unique_ptr<Object3d> debugHitboxObj_;
    std::unique_ptr<Particle> particle_;
    std::unique_ptr<Particle> debugTitleParticle_;

    //player
    std::unique_ptr<Player> player_;

    std::unique_ptr<Sprite> hpBack_;
    std::unique_ptr<Sprite> hpFill_;
    int maxHP_ = 100; // Playerの最大HPに合わせる

	Input* input_ = nullptr;

    //enemy 
    EnemyManager enemyMgr_;

    // 数字テクスチャ（0..9）
    std::string numTex_[10];

    // HP数字表示（3桁）
    std::unique_ptr<Sprite> hpDigits_[3];
    int hpDigitsCount_ = 3;

    // 表示位置など
    Vector2 hpBarPos_{ 30.0f, 30.0f };
    Vector2 hpBarSize_{ 300.0f, 20.0f };

    Vector2 hpNumPos_{ 30.0f + 310.0f, 30.0f - 2.0f }; // ★バーの右側に表示（好みで）
    Vector2 hpNumSize_{ 16.0f, 20.0f };                // 1桁サイズ（PNGの見た目に合わせて調整）
    float   hpNumSpacing_ = 2.0f;

    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Object3d> skyDome_;

    // Boss HP UI
    std::unique_ptr<Sprite> bossHpBack_;
    std::unique_ptr<Sprite> bossHpFill_;
    std::unique_ptr<Sprite> bossHpDigits_[3]{};

    Vector2 bossHpBarPos_{ 800.0f, 650.0f };   // 例：プレイヤーの下に置く
    float   bossHpBarW_ = 300.0f;
    float   bossHpBarH_ = 32.0f;

    //動画関係
    enum class Phase {
        IntroVideo,
        Battle,
		OutroVideo,
    };
    Phase phase_ = Phase::IntroVideo;

    // 120f 管理（Updateが1フレーム=1回呼ばれる前提ならこれが一番ラク）
    int introFrame_ = 0;
    static constexpr int kIntroFrames_ = 120;

    // dtベースでやるならこっち（可変fpsでも安定）
    float introTime_ = 0.0f;
    static constexpr float kIntroSeconds_ = 6.0f; // 120f@60fps

    std::unique_ptr<Object3d> videoPlane_;
    std::unique_ptr<VideoPlayerMF> video_;
    bool enableVideo_ = true;

    struct SRT { Vector3 pos{ 0,0,0 }; Vector3 rot{ 0,0,0 }; Vector3 scale{ 1,1,1 }; };
    SRT srtVideo_{};

    float outroTime_ = 0.0f;
    static constexpr float kOutroSeconds_ = 6.0f; // ★outro.mp4 の秒数に合わせて

    bool prevSpace_ = false;
    bool prevEnter_ = false;

    // ===== Pause UI =====
    bool isPaused_ = false;
    bool prevTab_ = false;

    enum class PauseSel { Close, ToTitle };
    PauseSel pauseSel_ = PauseSel::Close;

    // 画像（128x128）
    std::unique_ptr<Sprite> pauseClose_;   // "とじる"
    std::unique_ptr<Sprite> pauseToTitle_; // "タイトルへ"

    // 表示位置
    Vector2 pausePosClose_{ 520.0f, 360.0f };
    Vector2 pausePosTitle_{ 680.0f, 360.0f };

    // 選択の見た目（明るさなど）
    Vector4 pauseNormal_{ 1,1,1,1 };
    Vector4 pauseDim_{ 0.6f,0.6f,0.6f,1 };



};