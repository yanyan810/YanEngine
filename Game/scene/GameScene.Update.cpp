#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
#include "DebugAI/DebugAIManager.h"
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
#include <d3d12.h>
#include <sstream>

namespace {

const char* PlayerAttackTypeLabel(Player::PlayerAttackType type) {
    switch (type) {
    case Player::PlayerAttackType::Weak:
        return "AttackWeak";
    case Player::PlayerAttackType::Tilt:
        return "AttackTilt";
    case Player::PlayerAttackType::Smash:
        return "AttackSmash";
    case Player::PlayerAttackType::NeutralSpecial:
        return "AttackNeutralSpecial";
    case Player::PlayerAttackType::SideSpecial:
        return "AttackSideSpecial";
    case Player::PlayerAttackType::None:
    default:
        return "None";
    }
}

std::string BuildPlayerAttackHitMessage(
    const EnemyManager::PlayerAttackHitEvent& hit,
    Player::PlayerAttackType attackType) {

    std::ostringstream message;
    message
        << "attack=" << PlayerAttackTypeLabel(attackType)
        << " serial=" << hit.attackSerial
        << " target=" << hit.targetId
        << " type=" << hit.targetType
        << " damage=" << hit.damage
        << " hp " << hit.hpBefore << "->" << hit.hpAfter
        << " playerPos=("
        << hit.playerPosition.x << "," << hit.playerPosition.y << "," << hit.playerPosition.z
        << ") targetPos=("
        << hit.targetPosition.x << "," << hit.targetPosition.y << "," << hit.targetPosition.z
        << ")";
    return message.str();
}

}

bool GameScene::ProcessDebugAIRequests_(GameApp& app) {
    bool stateWasRestored = false;
    DebugAIManager* debugAI = app.DebugAI();
    if (!debugAI) {
        debugRequestStartReplay_ = false;
        debugRequestStopReplay_ = false;
        debugRequestStartBot_ = false;
        debugRequestStopBot_ = false;
        debugRequestRestoreInitialState_ = false;
        return false;
    }

    if (debugRequestStopReplay_) {
        debugAI->StopReplay();
        debugAIEnabled_ = false;
        debugManualRecordingActive_ = false;
    }

    if (debugRequestRestoreInitialState_ && debugAI->ReplayPlayer().HasInitialState()) {
        RestoreDebugState(debugAI->ReplayPlayer().InitialState());
        stateWasRestored = true;
        debugManualRecordingActive_ = false;
    }

    if (debugRequestStartReplay_) {
        const bool replayStarted = debugSelectedReplayPath_.empty()
            ? debugAI->StartLatestReplay()
            : debugAI->StartReplay(debugSelectedReplayPath_);
        if (replayStarted) {
            debugAIEnabled_ = true;
            stateWasRestored = true;
            debugManualRecordingActive_ = false;
        }
    }

    if (debugRequestStopBot_) {
        SetDebugAIEnabled_(app, false);
    }

    if (debugRequestStartBot_ && !debugAI->IsReplayPlaying()) {
        SetDebugAIEnabled_(app, true);
    }

    debugRequestStartReplay_ = false;
    debugRequestStopReplay_ = false;
    debugRequestStartBot_ = false;
    debugRequestStopBot_ = false;
    debugRequestRestoreInitialState_ = false;
    return stateWasRestored;
}

void GameScene::Update(GameApp& app, float dt) {
    if (!input_) return; // 蠢ｵ縺ｮ縺溘ａ
    ++debugFrameNumber_;
    
    // ESC 縺ｧ邨ゆｺ・
    if (input_->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }

    const bool replayShortcutBlocked = app.DebugAI() && app.DebugAI()->IsReplayPlaying();

    if (!replayShortcutBlocked && input_->IsKeyTrigger(DIK_F8)) {
        SetDebugAIEnabled_(app, !debugAIEnabled_);
    }

    if (input_->IsKeyTrigger(DIK_F7)) {
        debugRequestStartReplay_ = true;
    }

    if (app.DebugAI() && !app.DebugAI()->IsEnabled()) {
        debugAIEnabled_ = false;
    }

    if (ProcessDebugAIRequests_(app)) {
        return;
    }
    const bool blockExternalGameInput = app.DebugAI() && app.DebugAI()->IsReplayPlaying();

    camera_->Update();

    ground_->Update(dt);

    skyDome_->Update(dt);

    // ----------------------------
    // Intro Video 繝輔ぉ繝ｼ繧ｺ
    // ----------------------------
    if (phase_ == Phase::IntroVideo) {

        bool spaceNow = input_->IsKeyPressed(DIK_SPACE);
        bool enterNow = input_->IsKeyPressed(DIK_RETURN);

        bool spaceTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE);
        bool enterTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_RETURN);

        // dt譁ｹ蠑擾ｼ域耳螂ｨ・・
        introTime_ += dt;
        if (introTime_ >= kIntroSeconds_|| enterTrig|| spaceTrig) {
            phase_ = Phase::Battle;

            // 謌ｦ髣倬幕蟋区凾縺ｫ縲梧怙蛻昴・繧ｹ繝昴・繝ｳ繧偵％縺薙〒繧・ｋ縲阪↑繧峨％縺薙↓鄂ｮ縺・
            // enemyMgr_.QueueSpawn(...) 繧偵％縺薙〒繧・ｋ / BGM髢句ｧ九↑縺ｩ繧ゅ％縺・
        }

        // 繝輔Ξ繝ｼ繝譁ｹ蠑擾ｼ・0fps蝗ｺ螳壼燕謠撰ｼ峨〒繧・ｋ縺ｪ繧俄・
        // introFrame_++;
        // if (introFrame_ >= kIntroFrames_) { phase_ = Phase::Battle; }

        // 蜍慕判縺ｮ譖ｴ譁ｰ・域丐蜒・+ 髻ｳ・・
        if (enableVideo_ && video_) {
            // 繧ゅ＠縺ゅ↑縺溘・VideoPlayerMF縺後％縺ｮ蜷榊燕縺ｪ繧・

            videoPlane_->Update(dt);

            // 縺ゅ↑縺溘′莉贋ｽｿ縺｣縺ｦ繧句ｽ｢縺ｫ蜷医ｏ縺帙ｋ縺ｪ繧会ｼ・
            video_->ReadNextVideoFrame(); // 1繝輔Ξ繝ｼ繝騾ｲ繧√ｋ・域丐蜒・髻ｳ縺ｮ螳溯｣・↓繧医ｊ縺代ｊ・・
            video_->PumpAudio();

        }

        prevSpace_ = spaceNow;
        prevEnter_ = enterNow;

    } else if (phase_ == Phase::Battle) {

        // --- TAB縺ｧ繝昴・繧ｺ蛻・崛・・attle荳ｭ縺ｮ縺ｿ・・--
        bool tabNow = input_->IsKeyPressed(DIK_TAB);
        bool tabTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_TAB);
        prevTab_ = tabNow;

        if (tabTrig) {
            isPaused_ = !isPaused_;

            // 繝昴・繧ｺ荳ｭ縺ｯ繧ｰ繝ｬ繝ｼ繧ｹ繧ｱ繝ｼ繝ｫ
            app.Render()->SetMode(isPaused_
                ? PostEffectMode::Grayscale
                : PostEffectMode::FullScreen);

            // 髢九＞縺滓凾縺ｯ驕ｸ謚槭ｒClose縺ｫ謌ｻ縺呻ｼ亥･ｽ縺ｿ・・
            if (isPaused_) pauseSel_ = PauseSel::Close;
        }

        if (isPaused_) {

            // 蟾ｦ蜿ｳ縺ｧ驕ｸ謚橸ｼ・/D or 竊・竊抵ｼ・
            bool left = !blockExternalGameInput && (input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A));
            bool right = !blockExternalGameInput && (input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D));

            if (left)  pauseSel_ = PauseSel::Close;
            if (right) pauseSel_ = PauseSel::ToTitle;

            // 隕九◆逶ｮ・磯∈謚槭＠縺ｦ繧区婿繧呈・繧九￥・・
            if (pauseClose_ && pauseToTitle_) {
                pauseClose_->SetColor(pauseSel_ == PauseSel::Close ? pauseNormal_ : pauseDim_);
                pauseToTitle_->SetColor(pauseSel_ == PauseSel::ToTitle ? pauseNormal_ : pauseDim_);
            }

            // 豎ｺ螳夲ｼ・nter/Space・・
            bool enterNow = input_->IsKeyPressed(DIK_RETURN);
            bool spaceNow = input_->IsKeyPressed(DIK_SPACE);

            // 騾｣謇薙〒證ｴ繧後↑縺・ｈ縺・↓縲後ヨ繝ｪ繧ｬ縲肴桶縺・＠縺溘＞縺ｪ繧・prevEnter_/prevSpace_ 繧呈ｵ∫畑縺励※OK
            bool enterTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_RETURN);
            bool spaceTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE);
            prevEnter_ = enterNow;
            prevSpace_ = spaceNow;

            if (enterTrig || spaceTrig) {
                if (pauseSel_ == PauseSel::Close) {
                    isPaused_ = false;
                } else {
                    // 繧ｿ繧､繝医Ν縺ｸ
                    RequestChangeScene_("Title");
                    return;
                }
            }

            // 笘・％縺薙〒 return 縺吶ｋ縺ｮ縺ｧ縲√・繝ｬ繧､繝､繝ｼ/謨ｵ/繧ｿ繧､繝槭・縺碁ｲ縺ｾ縺ｪ縺・
            return;
        }


        if (debugAIEnabled_ && app.DebugAI()) {
            app.DebugAI()->InjectAction();
        }

        DebugAction manualAction;
        const bool recordManualAction =
            app.DebugAI() &&
            !app.DebugAI()->IsEnabled() &&
            CaptureManualDebugAction_(manualAction);
        const DebugGameState manualStateBefore = recordManualAction ? CaptureDebugState() : DebugGameState{};
        const unsigned int manualAttackSerialBefore =
            (recordManualAction && player_) ? player_->GetAttackSerial() : 0;

        if (player_) {
            player_->SetExternalInputBlocked(blockExternalGameInput);
            player_->Update(dt, *input_, enemyMgr_);
            const auto playerAttackHits = enemyMgr_.ApplyPlayerAttack(*player_);
            if (!playerAttackHits.empty() && app.DebugAI()) {
                const DebugGameState hitState = CaptureDebugState();
                const Player::PlayerAttackType attackType = player_->GetCurrentAttackType();
                for (const auto& hit : playerAttackHits) {
                    app.DebugAI()->LogEvent(
                        hitState,
                        "PlayerAttackHit",
                        BuildPlayerAttackHitMessage(hit, attackType));
                }
            }
        }


    // enemyMgr_ 縺ｫ貂｡縺・playerPos 繧・Player 縺九ｉ蜿悶ｋ
        Vector2 playerPos2D{ 0.0f, 0.0f };
        if (player_) {
            playerPos2D = player_->GetPos2D();
        }

        float playerZ = 15.0f;
        if (player_) {
            playerZ = player_->GetZ(); // 霑ｽ蜉縺励◆getter
        }

        // デバッグリプレイ用の簡易モードフラグ
        // true にすると敵の更新やスポーンが一時停止し、プレイヤーのみの挙動を確認できます
        constexpr bool kDebugDisableEnemies = false;

        // true にすると、Enemy本体はUpdateするが、PendingSpawnのカウントダウンと新規Spawnだけ止める
        constexpr bool kDebugDisablePendingSpawn = true;

        const bool skipPendingSpawn = kDebugDisablePendingSpawn || 
            (app.DebugAI() && app.DebugAI()->IsFirstReplayFrame());

        if (!kDebugDisableEnemies) {
            enemyMgr_.Update(dt, playerPos2D, playerZ, *player_, skipPendingSpawn);
        }

        if (bossHpFill_) {
            if (Enemy* boss = enemyMgr_.GetBoss()) {
                int hp = boss->GetHP();
                int maxHp = boss->GetMaxHP();

                float t = (maxHp > 0) ? float(hp) / float(maxHp) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);

                bossHpFill_->SetScale({ bossHpBarW_ * t, bossHpBarH_, 1.0f });

                UpdateBossHPDigits_(hp);
            } else {
                // 繝懊せ縺後＞縺ｪ縺・ｼ亥偵＠縺溷ｾ後↑縺ｩ・俄・UI豸医☆
                bossHpFill_->SetScale({ 0.0f, bossHpBarH_, 1.0f });
                UpdateBossHPDigits_(0);
            }
        }


        if (player_ && hpFill_) {
            int hp = player_->GetHP();      // 竊・getter菴懊ｋ・育┌縺代ｌ縺ｰ・・
            int maxHp = player_->GetMaxHP();// 竊・getter菴懊ｋ・育┌縺代ｌ縺ｰ・・

            float t = (maxHp > 0) ? (float(hp) / float(maxHp)) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            const float fullW = 300.0f;
            hpFill_->SetScale({ fullW * t, 20.0f ,1.0f });
        }

        if (player_) {
            UpdateHPDigits_(player_->GetHP());
        }

        if (recordManualAction) {
            FinalizeRecordedDebugAction_(manualAction, manualAttackSerialBefore);
            if (manualAction.name != "Wait") {
                debugManualRecordingActive_ = true;
            }
            if (debugManualRecordingActive_) {
                app.DebugAI()->RecordExternalAction(manualStateBefore, manualAction, CaptureDebugState());
            }
        }

        if (debugAIEnabled_ && app.DebugAI()) {
            app.DebugAI()->ProcessAfterUpdate(dt);
        }

        if (player_->IsDead()) {
            RequestChangeScene_("GameOver");
            return;
        }

        if (enemyMgr_.IsBossDefeated()) {

            // 笘・Outro髢句ｧ・
            phase_ = Phase::OutroVideo;
            outroTime_ = 0.0f;

            enableVideo_ = true;

            // 蜍慕判繧・outro 縺ｫ蟾ｮ縺玲崛縺茨ｼ医ョ繧ｳ繝ｼ繝迥ｶ諷九ｒ繝ｪ繧ｻ繝・ヨ・・
            if (!video_) {
                video_ = std::make_unique<VideoPlayerMF>();
            } else {
                video_->Close();
            }

            video_->Open("resources/video/outro.mp4", false); // 笘・％縺・
            video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
            video_->SetVolume(1.0f);

            // 笘・怙蛻昴・繝輔Ξ繝ｼ繝貅門ｙ
            video_->ReadNextVideoFrame();
            video_->ReadNextFrame();

            // ・井ｻｻ諢擾ｼ蔚I繧呈ｶ医＠縺溘＞縺ｪ繧峨％縺薙〒繝舌・繧帝國縺吶↑縺ｩ
            // 萓具ｼ喘ossHpFill_->SetScale({0, bossHpBarH_, 1});

            return;
        }


    } else if (phase_ == Phase::OutroVideo) {

        // 蜍慕判騾ｲ陦・
        if (enableVideo_ && video_) {
            videoPlane_->Update(dt);
            video_->ReadNextVideoFrame();
            video_->PumpAudio();
        }

        // 遘呈焚縺ｧ邨ゆｺ・ｼ医∪縺壹・縺薙ｌ縺梧怙騾滂ｼ・
        outroTime_ += dt;
        if (outroTime_ >= kOutroSeconds_) {
            RequestChangeScene_("GameClear");
            return;
        }

        // ・井ｻｻ諢擾ｼ峨せ繝壹・繧ｹ縺ｧ繧ｹ繧ｭ繝・・
         if (!blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE)) {
             RequestChangeScene_("GameClear");
             return;
         }
    }





}

// 繝昴せ繝医お繝輔ぉ繧ｯ繝亥ｯｾ雎｡縺ｮ3D謠冗判・医が繝輔せ繧ｯ繝ｪ繝ｼ繝ｳ縺ｸ・・
