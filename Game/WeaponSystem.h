#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

struct WeaponDefinition {
    std::string id;
    std::string displayName;
    float damage = 0;
    float fireInterval = 0;
    float range = 0;
    int magazineSize = 0;
    int reserveAmmo = 0; // ammunition supplied with a new pickup
    int maxReserveAmmo = 0;
    float reloadTime = 0;
    int pelletCount = 1;
    float spreadDegrees = 0; // cone half-angle; damage is PER pellet
    Vector3 pickupScale{.3f,.15f,.15f};
    Vector3 pickupColor{1,1,1};
};

// Each player owns mutable ammo/timers; definitions are copied only on equip.
class WeaponRuntime {
public:
    void Equip(const WeaponDefinition& definition) {
        definition_ = definition;
        magazine_ = definition.magazineSize;
        reserve_ = std::min(definition.reserveAmmo, definition.maxReserveAmmo);
        cooldown_ = reloadRemaining_ = 0;
        reloading_ = false;
    }
    const WeaponDefinition& Definition() const { return definition_; }
    int Magazine() const { return magazine_; }
    int Reserve() const { return reserve_; }
    bool Reloading() const { return reloading_; }
    double ReloadRemaining() const { return reloadRemaining_; }
    double Cooldown() const { return cooldown_; }
    void Update(float dt) {
        const double elapsed = std::isfinite(dt) ? std::max(0.0,static_cast<double>(dt)) : 0;
        cooldown_ = std::max(0.0,cooldown_-elapsed);
        if (!reloading_) return;
        reloadRemaining_ = std::max(0.0,reloadRemaining_-elapsed);
        if (reloadRemaining_ <= 1e-7) FinishReload();
    }
    bool TryFire() {
        if (definition_.id.empty() || reloading_ || cooldown_ > 1e-7 || magazine_ <= 0) return false;
        --magazine_;
        cooldown_ = definition_.fireInterval;
        return true;
    }
    bool StartReload() {
        if (definition_.id.empty() || reloading_ || magazine_ >= definition_.magazineSize || reserve_ <= 0) return false;
        reloading_ = true;
        reloadRemaining_ = definition_.reloadTime;
        if (reloadRemaining_ == 0) FinishReload();
        return true;
    }
private:
    void FinishReload() {
        const int amount = std::min(definition_.magazineSize-magazine_,reserve_);
        magazine_ += amount;
        reserve_ -= amount;
        reloadRemaining_ = 0;
        reloading_ = false;
    }
    WeaponDefinition definition_;
    int magazine_ = 0;
    int reserve_ = 0;
    double cooldown_ = 0;
    double reloadRemaining_ = 0;
    bool reloading_ = false;
};

struct WeaponPoolEntry { std::string id; double weight = 1; };
struct WeaponSpawnPoint {
    std::string id;
    Vector3 position{};
    Vector3 rotation{};
    std::vector<WeaponPoolEntry> weaponPool;
};
struct WeaponPickup {
    std::string spawnPointId;
    std::string weaponId;
    Vector3 position{};
    Vector3 rotation{};
    bool visible = true;
    bool pickedUp = false;
};
struct WeaponRandomSettings { bool useFixedSeed = false; uint32_t seed = 12345; };

class WeaponSystem {
public:
    // All definition/point validation and lottery happen once at stage initialization.
    bool Load(const std::string& definitionsPath, const std::string& levelPath,
        std::optional<WeaponRandomSettings> settingsOverride = std::nullopt);
    const WeaponDefinition* Find(const std::string& id) const {
        for (const auto& definition : definitions_) if (definition.id == id) return &definition;
        return nullptr;
    }
    const WeaponDefinition* InitialWeapon() const { return Find(initialWeaponId_); }
    const std::vector<WeaponSpawnPoint>& Points() const { return points_; }
    const std::vector<WeaponPickup>& Pickups() const { return pickups_; }
    const std::string& Error() const { return error_; }
    WeaponRandomSettings Settings() const { return settings_; }
    uint32_t ActualSeed() const { return actualSeed_; }
    static constexpr float kPickupRadius = 2.2f;
    std::optional<size_t> Nearest(const Vector3& player) const {
        std::optional<size_t> nearest;
        float best = kPickupRadius*kPickupRadius;
        for (size_t i=0; i<pickups_.size(); ++i) {
            const auto& pickup = pickups_[i];
            if (!pickup.visible || pickup.pickedUp) continue;
            const auto delta = player-pickup.position;
            const float distance = delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
            if (distance <= best && (!nearest || distance < best)) { nearest = i; best = distance; }
        }
        return nearest; // ties use JSON order
    }
    bool TryPickup(const Vector3& player, WeaponRuntime& weapon) {
        const auto index = Nearest(player);
        if (!index) return false;
        auto& pickup = pickups_[*index];
        const auto* definition = Find(pickup.weaponId);
        if (!definition) return false;
        weapon.Equip(*definition);
        pickup.pickedUp = true;
        pickup.visible = false;
        return true;
    }
private:
    std::vector<WeaponDefinition> definitions_;
    std::vector<WeaponSpawnPoint> points_;
    std::vector<WeaponPickup> pickups_;
    std::string initialWeaponId_;
    std::string error_;
    WeaponRandomSettings settings_;
    uint32_t actualSeed_ = 0;
};

// Camera basis avoids world-up singularities when aiming vertically.
inline Vector3 WeaponPelletDirection(const Vector3& forward, const Vector3& right, const Vector3& up,
    float spreadDegrees, std::mt19937& random) {
    if (spreadDegrees <= 0) return forward;
    std::uniform_real_distribution<float> unit(0,1);
    const float radius = std::sqrt(unit(random))*std::tan(spreadDegrees*.01745329252f);
    const float angle = unit(random)*6.28318530718f;
    const auto direction = forward + right*(radius*std::cos(angle)) + up*(radius*std::sin(angle));
    const float length = std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    return direction*(1.0f/length);
}
