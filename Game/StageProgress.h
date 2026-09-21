#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

enum class StageState { Playing, Cleared };
struct GoalTrigger {
    std::string id;
    Vector3 position{};
    Vector3 size{}; // full AABB extents, position is center
    bool activated = false;
    bool Contains(const Vector3& player) const {
        const auto delta = player - position;
        return std::abs(delta.x) <= size.x*.5f && std::abs(delta.y) <= size.y*.5f && std::abs(delta.z) <= size.z*.5f;
    }
};

// No dependency on spawning, rendering or Enemy ownership. The scene gates gameplay.
class StageProgress {
public:
    bool LoadGoals(const std::string& levelPath);
    bool IsPlaying() const { return state_ == StageState::Playing; }
    StageState State() const { return state_; }
    double Time() const { return elapsed_; }
    size_t DefeatedCount() const { return defeatedIds_.size(); }
    const std::vector<GoalTrigger>& Goals() const { return goals_; }
    const std::string& Error() const { return error_; }
    // Called after player motion, BEFORE any spawn, enemy AI, shot or attack.
    // Returns true only on the transition; residual enemies are irrelevant.
    bool Update(float dt, const Vector3& player) {
        if (!IsPlaying()) return false;
        if (std::isfinite(dt)) elapsed_ += std::max(0.0, static_cast<double>(dt));
        for (auto& goal : goals_) {
            if (!goal.activated && goal.Contains(player)) {
                goal.activated = true;
                state_ = StageState::Cleared;
                return true;
            }
        }
        return false;
    }
    void ObserveEnemy(uint64_t id, bool dead) {
        if (IsPlaying() && dead) defeatedIds_.insert(id);
    }
    void Reset() {
        state_ = StageState::Playing;
        elapsed_ = 0;
        defeatedIds_.clear();
        for (auto& goal : goals_) goal.activated = false;
    }
    static std::string FormatTime(double seconds) {
        const double safe = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 599999.99) : 0;
        const auto centiseconds = static_cast<uint64_t>(std::floor(safe * 100.0 + 1e-6));
        std::ostringstream text;
        text << std::setfill('0') << std::setw(2) << centiseconds/6000 << ':'
            << std::setw(2) << (centiseconds/100)%60 << '.' << std::setw(2) << centiseconds%100;
        return text.str();
    }
private:
    StageState state_ = StageState::Playing;
    double elapsed_ = 0;
    std::unordered_set<uint64_t> defeatedIds_;
    std::vector<GoalTrigger> goals_;
    std::string error_;
};
