#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

// Pure map data: points never detect the player or create enemies.
struct EnemySpawnPoint {
    std::string id;
    Vector3 position{};
    Vector3 rotation{}; // radians, same convention as Enemy / Object3d
};
enum class SpawnPointSelection { Random, RoundRobin };
struct EnemySpawnTrigger {
    std::string id;
    Vector3 position{}; // AABB center
    Vector3 size{};     // full extents
    std::vector<std::string> spawnPointIds;
    int spawnCount = 1;
    double spawnInterval = 0;
    double initialDelay = 0;
    int maxAlive = 1;
    SpawnPointSelection selection = SpawnPointSelection::Random;
    bool oneShot = true;

    bool activated = false;
    bool active = false;
    bool wasInside = false;
    int spawned = 0;
    size_t nextPoint = 0;
    double nextSpawn = 0;
    std::vector<uint64_t> livingEnemies;
};

class EnemySpawnSystem {
public:
    using Spawn = std::function<uint64_t(const EnemySpawnPoint&, const std::string&)>;
    using IsAlive = std::function<bool(uint64_t)>;
    // Transactional validation: bad data never leaves a partially configured map.
    bool Load(const std::string& path);
    const std::string& Error() const { return error_; }
    const std::vector<EnemySpawnPoint>& Points() const { return points_; }
    const std::vector<EnemySpawnTrigger>& Triggers() const { return triggers_; }
    const EnemySpawnPoint* FindPoint(const std::string& id) const {
        for (const auto& point : points_) if (point.id == id) return &point;
        return nullptr;
    }
    void Update(float dt, const Vector3& player, const Spawn& spawn, const IsAlive& isAlive) {
        const double elapsed = std::isfinite(dt) ? std::max(0.0, static_cast<double>(dt)) : 0.0;
        for (auto& trigger : triggers_) {
            auto& living = trigger.livingEnemies;
            living.erase(std::remove_if(living.begin(), living.end(), [&](uint64_t id) { return !isAlive(id); }), living.end());
            const auto delta = player - trigger.position;
            const bool inside = std::abs(delta.x) <= trigger.size.x * .5f &&
                std::abs(delta.y) <= trigger.size.y * .5f && std::abs(delta.z) <= trigger.size.z * .5f;
            const bool start = inside && !trigger.wasInside && !trigger.active && (!trigger.oneShot || !trigger.activated);
            trigger.wasInside = inside;
            if (start) {
                trigger.activated = true;
                trigger.active = true;
                trigger.spawned = 0;
                trigger.nextPoint = 0;
                trigger.nextSpawn = trigger.initialDelay;
            }
            if (!trigger.active) continue;
            // Activation is observed at the end of this frame; do not charge its preceding dt.
            if (!start) trigger.nextSpawn -= elapsed;
            while (trigger.nextSpawn <= 1e-7 && trigger.spawned < trigger.spawnCount) {
                if (living.size() >= static_cast<size_t>(trigger.maxAlive)) {
                    trigger.nextSpawn = 0; // no accumulated burst debt while capacity is full
                    break;
                }
                size_t index = trigger.nextPoint % trigger.spawnPointIds.size();
                if (trigger.selection == SpawnPointSelection::Random)
                    index = std::uniform_int_distribution<size_t>(0, trigger.spawnPointIds.size() - 1)(random_);
                const auto* point = FindPoint(trigger.spawnPointIds[index]);
                living.push_back(spawn(*point, trigger.id));
                ++trigger.nextPoint;
                ++trigger.spawned;
                trigger.nextSpawn += trigger.spawnInterval;
            }
            if (trigger.spawned == trigger.spawnCount) {
                trigger.active = false;
                trigger.nextSpawn = 0;
            }
        }
    }
private:
    std::vector<EnemySpawnPoint> points_;
    std::vector<EnemySpawnTrigger> triggers_;
    std::string error_;
    std::mt19937 random_{std::random_device{}()};
};
