#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

class GameApp;
class IScene;

class SceneManager {
public:
    using Factory = std::function<std::unique_ptr<IScene>()>;

    void Register(const std::string& name, Factory factory);
    void Change(GameApp& app, const std::string& name);

    void Update(GameApp& app, float dt);

    void DrawRender(GameApp& app);
    void Draw3D(GameApp& app);
    void Draw2D(GameApp& app);
    void Draw(GameApp& app);

    IScene* Current() { return current_.get(); }

private:
    std::unordered_map<std::string, Factory> factories_;
    std::unique_ptr<IScene> current_;
    std::string currentName_;
};