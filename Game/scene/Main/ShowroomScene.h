#pragma once
#ifdef _DEBUG
#include "GameScene.h"
// Separate scene instance and level; combat behavior remains shared with GameScene.
class ShowroomScene final : public GameScene {
public:
    ShowroomScene() : GameScene(true) {}
};
#endif
