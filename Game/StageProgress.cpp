#include "StageProgress.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

bool StageProgress::LoadGoals(const std::string& levelPath) {
    try {
        std::ifstream file(levelPath);
        if (!file) throw std::runtime_error("Cannot open " + levelPath);
        nlohmann::json data;
        file >> data;
        if (!data.is_object()) throw std::runtime_error("Level must be an object");
        std::vector<GoalTrigger> goals;
        std::set<std::string> ids;
        const auto vector = [](const nlohmann::json& value) {
            if (!value.is_array() || value.size() != 3) throw std::runtime_error("Goal vector must have 3 components");
            Vector3 result{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
                throw std::runtime_error("Goal vector must be finite");
            return result;
        };
        // Optional section preserves existing spawn-only JSON compatibility.
        if (data.contains("goalTriggers")) {
            if (!data.at("goalTriggers").is_array()) throw std::runtime_error("goalTriggers must be an array");
            for (const auto& item : data.at("goalTriggers")) {
                GoalTrigger goal;
                goal.id = item.at("id").get<std::string>();
                if (goal.id.empty() || !ids.insert(goal.id).second) throw std::runtime_error("Empty/duplicate goal ID: " + goal.id);
                goal.position = vector(item.at("position"));
                goal.size = vector(item.at("size"));
                if (goal.size.x <= 0 || goal.size.y <= 0 || goal.size.z <= 0) throw std::runtime_error("Invalid goal size: " + goal.id);
                goals.push_back(std::move(goal));
            }
        }
        goals_ = std::move(goals);
        Reset();
        error_.clear();
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        return false;
    }
}
