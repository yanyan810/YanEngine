#pragma once

#include <dinput.h>
#include <Windows.h>
#include "WinApp.h"

class Input {
public:
    // 初期化
    void Initialize(WinApp* winApp);

    // 更新処理（毎フレーム呼び出し）
    void Update();

    // トリガー（今回押されたが前回押されていない）
    bool IsKeyTrigger(BYTE keyCode) const;

    // 押しっぱなし
    bool IsKeyPressed(BYTE keyCode) const;

    // 離した瞬間
    bool IsKeyReleased(BYTE keyCode) const;

    POINT prevMousePos_{};
    POINT mouseDelta_{};

    void UpdateMouseDelta();
    POINT GetMouseDelta() const { return mouseDelta_; }

 /*   bool IsKeyPressed(BYTE keyCode) const;
    bool IsKeyReleased(BYTE keyCode) const;*/

private:
    IDirectInput8* directInput_ = nullptr;
    IDirectInputDevice8* keyboardDevice_ = nullptr;
    BYTE keys_[256]{};
    BYTE prevKeys_[256]{};
    bool firstMouseUpdate_ = true;
    bool cameraControlEnabled_ = false;
    bool prevToggleKeyState_ = false; // トグル用
    bool justEnteredCameraMode_ = false;

    WinApp* winApp_ = nullptr;

};
