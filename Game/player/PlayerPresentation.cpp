#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"
#include <algorithm>
#include <format>
#include <numbers>

void Player::SetLighting(const LightingParam& p)
{
    light_ = p;
    if (!model_) return;

    model_->SetEnableLighting(light_.lightingMode);

    model_->SetDirection(light_.dir);
    model_->SetIntensity(light_.dirIntensity);
    model_->SetLightColor(light_.dirColor);

    model_->SetPointLightPos(light_.pointPos);
    model_->SetPointLightIntensity(light_.pointIntensity);
    model_->SetPointLightColor(light_.pointColor);
    model_->SetPointLightRadius(light_.pointRadius);
    model_->SetPointLightDecay(light_.pointDecay);

    light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

    const float cosOuter = std::cosf(light_.spotAngleDeg * (std::numbers::pi_v<float> / 180.0f));
    const float cosInner = std::cosf(light_.spotFalloffStartDeg * (std::numbers::pi_v<float> / 180.0f));

    model_->SetSpotLightPos(light_.spotPos);
    model_->SetSpotLightDirection(light_.spotDir);
    model_->SetSpotLightIntensity(light_.spotIntensity);
    model_->SetSpotLightDistance(light_.spotDistance);
    model_->SetSpotLightDecay(light_.spotDecay);
    model_->SetSpotLightCosAngle(cosOuter);
    model_->SetSpotLightCosFalloffStart(cosInner);
    model_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });
    if (!swordObj_) return;

    swordObj_->SetEnableLighting(light_.lightingMode);

    swordObj_->SetDirection(light_.dir);
    swordObj_->SetIntensity(light_.dirIntensity);
    swordObj_->SetLightColor(light_.dirColor);

    swordObj_->SetPointLightPos(light_.pointPos);
    swordObj_->SetPointLightIntensity(light_.pointIntensity);
    swordObj_->SetPointLightColor(light_.pointColor);
    swordObj_->SetPointLightRadius(light_.pointRadius);
    swordObj_->SetPointLightDecay(light_.pointDecay);

    light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

  
    swordObj_->SetSpotLightPos(light_.spotPos);
    swordObj_->SetSpotLightDirection(light_.spotDir);
    swordObj_->SetSpotLightIntensity(light_.spotIntensity);
    swordObj_->SetSpotLightDistance(light_.spotDistance);
    swordObj_->SetSpotLightDecay(light_.spotDecay);
    swordObj_->SetSpotLightCosAngle(cosOuter);
    swordObj_->SetSpotLightCosFalloffStart(cosInner);
    swordObj_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });

}


void Player::ResetTitleAttackDemo()
{
    titleDemoTimer_ = 0.0f;
    titleDemoNextIsI_ = true;

    // 隕九◆逶ｮ繧ゅΜ繧ｻ繝・ヨ縺励◆縺代ｌ縺ｰ
    if (model_) {
        model_->PlayAnimation("", true);
        curAnim_ = "Idle";
    }
}

void Player::UpdateTitleAttackDemo(float dt, float intervalSec)
{
    if (!model_) return;

    // interval 縺ｮ螳牙・遲・
    if (intervalSec < 0.05f) intervalSec = 0.05f;

    // 縺ｾ縺壹い繝九Γ譎る俣繧帝ｲ繧√ｋ・遺・縺薙ｌ雜・㍾隕・ｼ・
    model_->Update(dt);

    // 謾ｻ謦・い繝九Γ荳ｭ縺ｪ繧峨檎ｵゅｏ繧九∪縺ｧ蠕・▽縲・
    const bool inAttackAnim = (curAnim_ == "Attak_I" || curAnim_ == "Attak_O");
    if (inAttackAnim) {
        if (model_->IsAnimationFinished()) {
            model_->CrossFadeTo("Idle", 0.20f, true);
            curAnim_ = "Idle";
        }
        return;
    }

    // 谺｡縺ｮ謾ｻ謦・ち繧､繝溘Φ繧ｰ
    titleDemoTimer_ += dt;
    if (titleDemoTimer_ >= intervalSec) {
        titleDemoTimer_ = 0.0f;

        if (titleDemoNextIsI_) {
            model_->CrossFadeTo("Attak_I", 0.10f, false);
            curAnim_ = "Attak_I";
        } else {
            model_->CrossFadeTo("Attak_O", 0.10f, false);
            curAnim_ = "Attak_O";
        }
        titleDemoNextIsI_ = !titleDemoNextIsI_;
    }
}
void Player::SetTitleTransform(const Vector3& t, const Vector3& r, const Vector3& s)
{
    model_->SetTranslate(t);
    model_->SetRotate(r);
    model_->SetScale(s);
}
