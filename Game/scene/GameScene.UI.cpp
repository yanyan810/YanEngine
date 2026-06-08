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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <d3d12.h>

static float RandRange(float min, float max) {
    return min + (max - min) * (float(rand()) / float(RAND_MAX));
}

void GameScene::SpawnEnemyFromOutside_(EnemyType type) {
    // ===== 逕ｻ髱｢遽・峇・郁ｪｿ謨ｴ逕ｨ・・=====
    const float screenLeft = -12.0f;
    const float screenRight = 12.0f;
    const float outsidePad = 3.0f;   // 逕ｻ髱｢螟悶↓縺ｩ繧後□縺大・縺吶°
    const float z = 15.0f;

    // 蟾ｦ or 蜿ｳ 繧偵Λ繝ｳ繝繝
    bool fromLeft = (rand() % 2) == 0;

    float x;
    if (fromLeft) {
        x = RandRange(screenLeft - outsidePad - 3.0f,
            screenLeft - outsidePad);
    } else {
        x = RandRange(screenRight + outsidePad,
            screenRight + outsidePad + 3.0f);
    }

    // Y 縺ｯ蟆代＠繝ｩ繝ｳ繝繝縺ｫ
    float y = RandRange(-1.0f, 1.0f);

    enemyMgr_.Spawn(type, Vector3{ x, y, z });
}

void GameScene::UpdateHPDigits_(int hp) {
    hp = std::clamp(hp, 0, 999);

    int d0 = (hp / 100) % 10;
    int d1 = (hp / 10) % 10;
    int d2 = (hp / 1) % 10;

    // 陦ｨ遉ｺ縺吶ｋ譯・ｼ亥・鬆ｭ繧ｼ繝ｭ豸医＠・・
    bool show0 = (hp >= 100);
    bool show1 = (hp >= 10);
    bool show2 = true;

    // 蜿ｳ隧ｰ繧√〒繝舌・縺ｮ荳翫↓鄂ｮ縺擾ｼ井ｾ具ｼ壹ヰ繝ｼ蜿ｳ遶ｯ莉倩ｿ托ｼ・
    const float baseX = 128.0f*1.5f; // 笘・ヰ繝ｼ蜿ｳ遶ｯ
    const float baseY = hpBarPos_.y - 18.0f;              // 笘・ヰ繝ｼ縺ｮ荳・

    const float w = 16.0f;     // 1譯∝ｹ・ｼ育判蜒上↓蜷医ｏ縺帙※隱ｿ謨ｴ・・
    const float h = 20.0f;     // 1譯・ｫ倥＆
    const float sp = 2.0f;     // 譯・俣

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
        hpDigits_[idx]->SetScale({ 1.0f, 1.0f, 1.0f }); // 蠢・ｦ√↑繧・
     
        };

    // 蜿ｳ隧ｰ繧・・鄂ｮ・壻ｸ縺ｮ菴阪ｒ荳逡ｪ蜿ｳ
    float x2 = baseX - (w);
    float x1 = x2 - (w + sp);
    float x0 = x1 - (w + sp);

    setDigit(0, d0, x0, baseY, show0);
    setDigit(1, d1, x1, baseY, show1);
    setDigit(2, d2, x2, baseY, show2);

    // 繧ゅ＠謨ｰ蟄励′繝・き縺・↑繧・
    for (int i = 0; i < 3; ++i) {
        if (hpDigits_[i]) hpDigits_[i]->SetScale({ 0.5f, 0.5f, 1.0f }); // 螂ｽ縺ｿ縺ｧ隱ｿ謨ｴ
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

    const float baseX = bossHpBarPos_.x + bossHpBarW_ - 8.0f; // 繝舌・蜿ｳ遶ｯ蟇・○
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

