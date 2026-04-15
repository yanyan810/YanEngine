#pragma once
#include <cstdint>

struct Animation;

class Animator {
public:
    void Play(const Animation* animation, bool loop = true);
    void Stop();

    void Update(float deltaTime);

    const Animation* GetAnimation() const { return animation_; }
    float GetTime() const { return time_; }
    bool IsPlaying() const { return playing_; }

private:
    const Animation* animation_ = nullptr;
    float time_ = 0.0f;
    bool loop_ = true;
    bool playing_ = false;
};
