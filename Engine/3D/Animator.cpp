#include "Animator.h"
#include "Animation.h"

void Animator::Play(const Animation* animation, bool loop) {
    animation_ = animation;
    loop_ = loop;
    time_ = 0.0f;
    playing_ = (animation_ != nullptr);
}

void Animator::Stop() {
    playing_ = false;
    time_ = 0.0f;
}

void Animator::Update(float deltaTime) {
    if (!playing_ || !animation_) return;

    time_ += deltaTime;

    if (time_ >= animation_->duration) {
        if (loop_) {
            time_ = fmod(time_, animation_->duration);
        }
        else {
            time_ = animation_->duration;
            playing_ = false;
        }
    }
}
