#include "EnemySpawnSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

bool EnemySpawnSystem::Load(const std::string& path, const EnemyDefinitions& definitions) {
    try {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot open " + path);
        nlohmann::json data;
        file >> data;
        const auto vector = [](const nlohmann::json& value) {
            if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected a 3-component vector");
            Vector3 result{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
                throw std::runtime_error("Vector must be finite");
            return result;
        };
        const auto integer = [](const nlohmann::json& value) {
            if (!value.is_number_integer()) throw std::runtime_error("Counts must be integers");
            const double number = value.get<double>();
            if (number < 1 || number > 10000) throw std::runtime_error("Counts must be in [1, 10000]");
            return static_cast<int>(number);
        };
        std::vector<EnemySpawnPoint> points;
        std::vector<EnemySpawnTrigger> triggers;
        std::set<std::string> pointIds, triggerIds;
        if (!data.at("spawnPoints").is_array() || !data.at("spawnTriggers").is_array())
            throw std::runtime_error("spawnPoints and spawnTriggers must be arrays");
        for (const auto& item : data.at("spawnPoints")) {
            EnemySpawnPoint point;
            point.id = item.at("id").get<std::string>();
            if (point.id.empty() || !pointIds.insert(point.id).second) throw std::runtime_error("Empty/duplicate point ID: " + point.id);
            point.position = vector(item.at("position"));
            if (item.contains("rotation")) point.rotation = vector(item.at("rotation"));
            if (item.contains("enemyPool")) {
                const auto& pool=item.at("enemyPool");
                if (!pool.is_array()) throw std::runtime_error("enemyPool must be an array");
                double total=0;
                for (const auto& entry : pool) {
                    EnemyPoolEntry value{entry.at("id").get<std::string>(),entry.at("weight").get<double>()};
                    if (!definitions.Find(value.id)) throw std::runtime_error("Unknown enemy id: " + value.id);
                    if (!std::isfinite(value.weight) || value.weight<0) throw std::runtime_error("Invalid enemy weight");
                    total+=value.weight; point.enemyPool.push_back(value);
                }
                if (!std::isfinite(total) || total<=0) throw std::runtime_error("enemyPool requires positive total weight");
            }
            points.push_back(std::move(point));
        }
        for (const auto& item : data.at("spawnTriggers")) {
            EnemySpawnTrigger trigger;
            trigger.id = item.at("id").get<std::string>();
            if (trigger.id.empty() || !triggerIds.insert(trigger.id).second) throw std::runtime_error("Empty/duplicate trigger ID: " + trigger.id);
            trigger.position = vector(item.at("position"));
            trigger.size = vector(item.at("size"));
            if (trigger.size.x <= 0 || trigger.size.y <= 0 || trigger.size.z <= 0) throw std::runtime_error("Invalid size: " + trigger.id);
            trigger.spawnPointIds = item.at("spawnPointIds").get<std::vector<std::string>>();
            if (trigger.spawnPointIds.empty()) throw std::runtime_error("No spawn points: " + trigger.id);
            for (const auto& id : trigger.spawnPointIds)
                if (!pointIds.count(id)) throw std::runtime_error("Unknown spawn point: " + id);
            trigger.spawnCount = integer(item.at("spawnCount"));
            trigger.maxAlive = item.contains("maxAlive") ? integer(item.at("maxAlive")) : trigger.spawnCount;
            trigger.spawnInterval = item.value("spawnInterval", 0.0);
            trigger.initialDelay = item.value("initialDelay", 0.0);
            if (!std::isfinite(trigger.spawnInterval) || !std::isfinite(trigger.initialDelay) || trigger.spawnInterval < 0 || trigger.initialDelay < 0)
                throw std::runtime_error("Invalid timing: " + trigger.id);
            const auto selection = item.value("selection", std::string("Random"));
            if (selection == "RoundRobin" || selection == "Sequential") trigger.selection = SpawnPointSelection::RoundRobin;
            else if (selection != "Random") throw std::runtime_error("Unknown selection: " + selection);
            trigger.oneShot = item.value("oneShot", true);
            triggers.push_back(std::move(trigger));
        }
        auto nextRandom=random_;
        if (data.contains("enemyRandom")) {
            const auto& config=data.at("enemyRandom");
            if (config.value("useFixedSeed",false)) {
                const auto& seed=config.at("seed");
                if (!seed.is_number_integer() || seed.get<double>()<0 || seed.get<double>()>4294967295.0)
                    throw std::runtime_error("Invalid enemy seed");
                nextRandom.seed(seed.get<uint32_t>());
            }
        }
        random_=nextRandom;
        points_ = std::move(points);
        triggers_ = std::move(triggers);
        error_.clear();
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        return false;
    }
}
