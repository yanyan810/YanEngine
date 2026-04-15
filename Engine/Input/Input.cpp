#include "Input.h"
#include <cassert>
#include <cstring>

#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")



void Input::Initialize  (WinApp* winApp) {
    HRESULT hr;

    //借りてきたWinAppのインスタンスを記録
    this->winApp_ = winApp;

    // DirectInputの初期化
    hr = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput_, nullptr);
    assert(SUCCEEDED(hr));

    // キーボードデバイスの作成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboardDevice_, nullptr);
    assert(SUCCEEDED(hr));

    // データフォーマットを設定（標準のキーボードフォーマット）
    hr = keyboardDevice_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 協調レベルの設定
    hr = keyboardDevice_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(hr));

    // デバイスの取得開始
    keyboardDevice_->Acquire();
}

void Input::UpdateMouseDelta() {
    POINT currentMousePos;
    GetCursorPos(&currentMousePos);
    ScreenToClient(GetActiveWindow(), &currentMousePos);

    if (firstMouseUpdate_) {
        // 初回は差分をゼロにしておく
        mouseDelta_ = { 0, 0 };
        firstMouseUpdate_ = false;
    } else {
        mouseDelta_.x = currentMousePos.x - prevMousePos_.x;
        mouseDelta_.y = currentMousePos.y - prevMousePos_.y;
    }

    prevMousePos_ = currentMousePos;

    if (cameraControlEnabled_) {
        // ウィンドウの中央座標を取得して固定
        HWND hwnd = GetActiveWindow();
        RECT rect;
        GetClientRect(hwnd, &rect);
        POINT center;
        center.x = (rect.right - rect.left) / 2;
        center.y = (rect.bottom - rect.top) / 2;

        // 現在位置を取得
        POINT currentMousePos;
        GetCursorPos(&currentMousePos);
        ScreenToClient(hwnd, &currentMousePos);

        // 差分計算
        mouseDelta_.x = currentMousePos.x - center.x;
        mouseDelta_.y = currentMousePos.y - center.y;

        // マウスを中央に戻す
        ClientToScreen(hwnd, &center);
        SetCursorPos(center.x, center.y);
    } else {
        mouseDelta_ = { 0, 0 };
    }

}


void Input::Update() {
    // 前フレームの状態を保存
    memcpy(prevKeys_, keys_, sizeof(keys_));

    HRESULT hr = keyboardDevice_->GetDeviceState(sizeof(keys_), keys_);
    if (FAILED(hr)) {
        keyboardDevice_->Acquire();
        keyboardDevice_->GetDeviceState(sizeof(keys_), keys_);
    }

    UpdateMouseDelta();

    // === 修正済み：トグル処理は1回だけ ===
    bool toggleKey = keys_[DIK_TAB];
    if (toggleKey && !prevToggleKeyState_) {
        cameraControlEnabled_ = !cameraControlEnabled_;
        justEnteredCameraMode_ = cameraControlEnabled_; // 初回だけtrue

        ShowCursor(!cameraControlEnabled_);
    }
    prevToggleKeyState_ = toggleKey;
}

bool Input::IsKeyTrigger(BYTE keyCode) const {
    // DirectInput は 0x80 が押下
    return (keys_[keyCode] & 0x80) && !(prevKeys_[keyCode] & 0x80);
}

bool Input::IsKeyPressed(BYTE keyCode) const {
    return (keys_[keyCode] & 0x80) != 0;
}

bool Input::IsKeyReleased(BYTE keyCode) const {
    return !(keys_[keyCode] & 0x80) && (prevKeys_[keyCode] & 0x80);
}

//
//bool Input::IsKeyPressed(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) != 0;
//}
//bool Input::IsKeyReleased(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) == 0;
//}
