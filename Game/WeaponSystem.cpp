#include "WeaponSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using nlohmann::json;
json Read(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);
    auto data = json::parse(file);
    if (!data.is_object()) throw std::runtime_error("Expected object: " + path);
    return data;
}
float Number(const json& value, float minimum, float maximum) {
    if (!value.is_number()) throw std::runtime_error("Expected numeric weapon parameter");
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < minimum || result > maximum) throw std::runtime_error("Weapon parameter out of range");
    return result;
}
int Integer(const json& value, int minimum, int maximum) {
    if (!value.is_number_integer()) throw std::runtime_error("Ammo/pellet count must be an integer");
    const double result = value.get<double>();
    if (result < minimum || result > maximum) throw std::runtime_error("Ammo/pellet count out of range");
    return static_cast<int>(result);
}
Vector3 Vector(const json& value) {
    if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected 3-component vector");
    return {Number(value[0],-1e6f,1e6f), Number(value[1],-1e6f,1e6f), Number(value[2],-1e6f,1e6f)};
}
}
bool WeaponSystem::Load(const std::string& definitionsPath, const std::string& levelPath,
    std::optional<WeaponRandomSettings> settingsOverride) {
    try {
        const auto source = Read(definitionsPath);
        const auto level = Read(levelPath);
        WeaponSystem loaded;
        std::set<std::string> ids;
        if (!source.at("weapons").is_array()) throw std::runtime_error("weapons must be an array");
        for (const auto& item : source.at("weapons")) {
            WeaponDefinition definition;
            definition.id = item.at("id").get<std::string>();
            if (definition.id.empty() || !ids.insert(definition.id).second) throw std::runtime_error("Empty/duplicate weapon ID: " + definition.id);
            definition.displayName = item.at("displayName").get<std::string>();
            if (definition.displayName.empty() || definition.displayName.size()>48) throw std::runtime_error("Invalid weapon name: " + definition.id);
            definition.damage = Number(item.at("damage"),.001f,100000);
            definition.fireInterval = Number(item.at("fireInterval"),.001f,3600);
            definition.range = Number(item.at("range"),.001f,100000);
            definition.magazineSize = Integer(item.at("magazineSize"),1,100000);
            definition.reserveAmmo = Integer(item.at("reserveAmmo"),0,1000000);
            definition.maxReserveAmmo = item.contains("maxReserveAmmo") ? Integer(item.at("maxReserveAmmo"),0,1000000) : definition.reserveAmmo;
            if (definition.reserveAmmo > definition.maxReserveAmmo) throw std::runtime_error("Reserve exceeds maximum: " + definition.id);
            definition.reloadTime = Number(item.at("reloadTime"),0,3600);
            if (item.contains("pelletCount")) definition.pelletCount = Integer(item.at("pelletCount"),1,64);
            if (item.contains("spreadDegrees")) definition.spreadDegrees = Number(item.at("spreadDegrees"),0,45);
            if (item.contains("pickupScale")) definition.pickupScale = Vector(item.at("pickupScale"));
            if (definition.pickupScale.x<=0 || definition.pickupScale.y<=0 || definition.pickupScale.z<=0) throw std::runtime_error("Invalid pickup scale");
            if (item.contains("pickupColor")) definition.pickupColor = Vector(item.at("pickupColor"));
            const auto color = definition.pickupColor;
            if (color.x<0 || color.y<0 || color.z<0 || color.x>1 || color.y>1 || color.z>1) throw std::runtime_error("Invalid pickup color");
            loaded.definitions_.push_back(std::move(definition));
        }
        loaded.initialWeaponId_ = source.value("initialWeapon",std::string("pistol"));
        if (!loaded.Find(loaded.initialWeaponId_)) throw std::runtime_error("Unknown initial weapon");
        if (level.contains("weaponRandom")) {
            const auto& settings = level.at("weaponRandom");
            loaded.settings_.useFixedSeed = settings.value("useFixedSeed",false);
            if (settings.contains("seed")) {
                const auto& seed = settings.at("seed");
                if (!seed.is_number_integer() || seed.get<double>()<0 || seed.get<double>()>4294967295.0)
                    throw std::runtime_error("Invalid weapon seed");
                loaded.settings_.seed = seed.get<uint32_t>();
            }
        }
        if (settingsOverride) loaded.settings_ = *settingsOverride;
        loaded.actualSeed_ = loaded.settings_.useFixedSeed ? loaded.settings_.seed : std::random_device{}();
        std::mt19937 random(loaded.actualSeed_);
        ids.clear();
        if (level.contains("weaponSpawnPoints")) {
            if (!level.at("weaponSpawnPoints").is_array()) throw std::runtime_error("weaponSpawnPoints must be an array");
            for (const auto& item : level.at("weaponSpawnPoints")) {
                WeaponSpawnPoint point;
                point.id = item.at("id").get<std::string>();
                if (point.id.empty() || !ids.insert(point.id).second) throw std::runtime_error("Empty/duplicate weapon point: " + point.id);
                point.position = Vector(item.at("position"));
                if (item.contains("rotation")) point.rotation = Vector(item.at("rotation"));
                const auto& pool = item.at("weaponPool");
                if (!pool.is_array() || pool.empty()) throw std::runtime_error("Empty/invalid weapon pool: " + point.id);
                std::vector<double> weights;
                double total = 0;
                for (const auto& candidate : pool) {
                    WeaponPoolEntry entry;
                    entry.id = candidate.is_string() ? candidate.get<std::string>() : candidate.at("id").get<std::string>();
                    if (!loaded.Find(entry.id)) throw std::runtime_error("Unknown pool weapon: " + entry.id);
                    if (!candidate.is_string()) entry.weight = candidate.value("weight",1.0);
                    if (!std::isfinite(entry.weight) || entry.weight<0 || entry.weight>1e9) throw std::runtime_error("Invalid pool weight");
                    weights.push_back(entry.weight);
                    total += entry.weight;
                    point.weaponPool.push_back(std::move(entry));
                }
                if (total<=0 || !std::isfinite(total)) throw std::runtime_error("Weapon pool needs a positive weight");
                const size_t selected = std::discrete_distribution<size_t>(weights.begin(),weights.end())(random);
                loaded.pickups_.push_back({point.id,point.weaponPool[selected].id,point.position,point.rotation,true,false});
                loaded.points_.push_back(std::move(point));
            }
        }
        *this = std::move(loaded); // atomic commit; invalid data retains the previous valid state
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        return false;
    }
}
