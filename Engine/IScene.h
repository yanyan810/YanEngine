#pragma once
#include <string>

class GameApp;

class IScene {
public:
    virtual ~IScene() = default;

    virtual void OnEnter(GameApp& app) {}
    virtual void OnExit(GameApp& app) {}

    virtual void Update(GameApp& app, float dt) = 0;
    virtual void Draw(GameApp& app) = 0;

    // ===== シーン遷移用 =====
    void RequestChangeScene_(const std::string& next) {
        nextScene_ = next;
    }

    const std::string& NextScene() const {
        return nextScene_;
    }

    void ClearNextScene_() {
        nextScene_.clear();
    }

private:
    std::string nextScene_;
};
