#include "Input.h"
#include <cassert>
#include <cstring>

#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

namespace {

WORD ToXInputButton(Input::GamepadButton button) {
    switch (button) {
    case Input::GamepadButton::A:
        return XINPUT_GAMEPAD_A;
    case Input::GamepadButton::B:
        return XINPUT_GAMEPAD_B;
    default:
        return 0;
    }
}

float NormalizeThumbAxis(SHORT value, SHORT deadZone) {
    const int magnitude = value < 0 ? -static_cast<int>(value) : static_cast<int>(value);
    if (magnitude <= deadZone) {
        return 0.0f;
    }

    const float sign = value < 0 ? -1.0f : 1.0f;
    const float range = static_cast<float>(32767 - deadZone);
    const float normalized = static_cast<float>(magnitude - deadZone) / range;
    return sign * (normalized > 1.0f ? 1.0f : normalized);
}

} // namespace



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

Input::~Input() {
    SetCameraControlEnabled(false);
    if (keyboardDevice_) { keyboardDevice_->Unacquire(); keyboardDevice_->Release(); }
    if (directInput_) directInput_->Release();
}

bool Input::HasFocus() const {
    return winApp_ && GetForegroundWindow() == winApp_->GetHwnd() && !IsIconic(winApp_->GetHwnd());
}

bool Input::GetCaptureRect_(RECT& rect) const {
    if (!winApp_ || !GetClientRect(winApp_->GetHwnd(), &rect)) return false;
    POINT corners[2]{{rect.left, rect.top}, {rect.right, rect.bottom}};
    ClientToScreen(winApp_->GetHwnd(), &corners[0]);
    ClientToScreen(winApp_->GetHwnd(), &corners[1]);
    rect = {corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    if (hasCaptureRect_) {
        RECT intersection{};
        if (!IntersectRect(&intersection, &rect, &captureRect_)) return false;
        rect = intersection;
    }
    return rect.right > rect.left && rect.bottom > rect.top;
}

void Input::SetMouseCaptureRect(const RECT* rect) {
    const bool changed = hasCaptureRect_ != (rect != nullptr) ||
        (rect && !EqualRect(rect, &captureRect_));
    hasCaptureRect_ = rect != nullptr;
    if (rect) captureRect_ = *rect;
    if (changed) { firstMouseUpdate_ = true; mouseDelta_ = {}; }
}

void Input::UpdateMouseDelta() {
    mouseDelta_ = {};
    if (!cameraControlEnabled_) return;
    RECT rect{};
    if (!HasFocus() || !GetCaptureRect_(rect)) {
        SetCameraControlEnabled(false);
        return;
    }
    // Reapply confinement after window movement/resizing. Never clip another app.
    if (!ClipCursor(&rect)) { SetCameraControlEnabled(false); return; }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    if (!firstMouseUpdate_) {
        mouseDelta_ = {cursor.x - prevMousePos_.x, cursor.y - prevMousePos_.y};
    }
    firstMouseUpdate_ = false;
    SetCursorPos(center.x, center.y);
    prevMousePos_ = center;
}

void Input::Update() {
    // 前フレームの状態を保存
    memcpy(prevKeys_, keys_, sizeof(keys_));
    prevGamepadState_ = gamepadState_;
    XINPUT_STATE newGamepadState{};
    gamepadConnected_ = XInputGetState(0, &newGamepadState) == ERROR_SUCCESS;
    gamepadState_ = gamepadConnected_ ? newGamepadState : XINPUT_STATE{};

    HRESULT hr = keyboardDevice_->GetDeviceState(sizeof(keys_), keys_);
    if (FAILED(hr)) {
        // Acquireを毎フレーム実行すると極めて重くなるため、1秒のインターバルを設ける
        static DWORD lastAcquireTime = 0;
        DWORD currentTime = GetTickCount();
        if (currentTime - lastAcquireTime > 1000) {
            keyboardDevice_->Acquire();
            lastAcquireTime = currentTime;
        }
        memset(keys_, 0, sizeof(keys_));
    }
    prevLeftMouseDown_ = leftMouseDown_;
    prevRightMouseDown_ = rightMouseDown_;

    const bool focused = HasFocus();

    leftMouseDown_ =
        focused && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    rightMouseDown_ =
        focused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    if (!focused) {
        memset(keys_, 0, sizeof(keys_));
    }

    UpdateMouseDelta();

    // === 修正済み：トグル処理は1回だけ ===
    bool toggleKey = keys_[DIK_F1];
    if (cameraToggleKeyEnabled_ && HasFocus() && toggleKey && !prevToggleKeyState_) {
        SetCameraControlEnabled(!cameraControlEnabled_);
        justEnteredCameraMode_ = cameraControlEnabled_; // 初回だけtrue

    }
    prevToggleKeyState_ = toggleKey;
}

void Input::SetCameraControlEnabled(bool enabled) {
    if (enabled && !HasFocus()) return;
    if (cameraControlEnabled_ == enabled) return;
    RECT rect{};
    if (enabled && (!GetCaptureRect_(rect) || !ClipCursor(&rect))) return;
    cameraControlEnabled_ = enabled;
    firstMouseUpdate_ = true;
    mouseDelta_ = {};
    if (enabled) {
        POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
        SetCursorPos(center.x, center.y);
        prevMousePos_ = center;
        // Balance our ShowCursor calls exactly when releasing control.
        int count;
        do { count = ShowCursor(FALSE); ++cursorHideCalls_; } while (count >= 0);
    } else {
        ClipCursor(nullptr);
        while (cursorHideCalls_ > 0) { ShowCursor(TRUE); --cursorHideCalls_; }
    }
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

bool Input::IsGamepadButtonPressed(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ && (gamepadState_.Gamepad.wButtons & mask) != 0;
}

bool Input::IsGamepadButtonTrigger(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ &&
        (gamepadState_.Gamepad.wButtons & mask) != 0 &&
        (prevGamepadState_.Gamepad.wButtons & mask) == 0;
}

bool Input::IsGamepadButtonReleased(GamepadButton button) const {
    const WORD mask = ToXInputButton(button);
    return gamepadConnected_ &&
        (gamepadState_.Gamepad.wButtons & mask) == 0 &&
        (prevGamepadState_.Gamepad.wButtons & mask) != 0;
}

float Input::GetLeftStickX() const {
    return gamepadConnected_
        ? NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        : 0.0f;
}

float Input::GetLeftStickY() const {
    return gamepadConnected_
        ? NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        : 0.0f;
}

bool Input::IsLeftStickUpTrigger(float threshold) const {
    if (!gamepadConnected_) {
        return false;
    }

    const float currentY =
        NormalizeThumbAxis(gamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const float previousY =
        NormalizeThumbAxis(prevGamepadState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    return currentY > threshold && previousY <= threshold;
}

//
//bool Input::IsKeyPressed(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) != 0;
//}
//bool Input::IsKeyReleased(BYTE keyCode) const {
//    return (keys_[keyCode] & 0x80) == 0;
//}

