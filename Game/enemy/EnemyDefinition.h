#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <stdexcept>

enum class EnemyType { Normal, Ranged, Fast, Tank, Bomber };
inline const char* EnemyTypeName(EnemyType type) {
    switch (type) {
    case EnemyType::Normal: return "Normal";
    case EnemyType::Ranged: return "Ranged";
    case EnemyType::Fast: return "Fast";
    case EnemyType::Tank: return "Tank";
    default: return "Bomber";
    }
}
struct EnemyDefinition {
    std::string id = "normal", displayName = "Normal";
    EnemyType type = EnemyType::Normal;
    float detectionRange=20, moveSpeed=2.5f, attackRange=1.5f, attackDamage=10, attackInterval=1;
    float hpMultiplier=1, preferredRange=12, minRange=7, maxRange=16;
    float projectileSpeed=12, projectileRadius=.2f, projectileLifetime=5;
    float explosionRadius=4, explosionDamage=25, fuseTime=1.2f;
    float bombGravity=9.8f;
    bool IsRanged() const { return type==EnemyType::Ranged || type==EnemyType::Bomber; }
};
class EnemyDefinitions {
public:
    bool Load(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("Cannot open " + path);
            nlohmann::json data; file >> data;
            if (!data.at("enemies").is_array()) throw std::runtime_error("enemies must be an array");
            std::map<std::string,EnemyDefinition> next;
            for (const auto& item : data.at("enemies")) {
                EnemyDefinition d;
                d.id=item.at("id").get<std::string>();
                d.displayName=item.value("displayName",d.id);
                const auto type=item.at("type").get<std::string>();
                bool known=false;
                for (auto t : {EnemyType::Normal,EnemyType::Ranged,EnemyType::Fast,EnemyType::Tank,EnemyType::Bomber})
                    if (type==EnemyTypeName(t)) { d.type=t; known=true; }
                if (!known) throw std::runtime_error("Unknown enemy type: " + type);
                const auto number=[&](const char* key,float& value,bool positive=false) {
                    value=item.value(key,value);
                    if (!std::isfinite(value) || (positive ? value<=0 : value<0))
                        throw std::runtime_error(d.id + ": invalid " + key);
                };
                number("detectionRange",d.detectionRange); number("moveSpeed",d.moveSpeed);
                number("attackRange",d.attackRange); number("attackDamage",d.attackDamage);
                number("attackInterval",d.attackInterval,true); number("hpMultiplier",d.hpMultiplier,true);
                number("minRange",d.minRange); number("preferredRange",d.preferredRange); number("maxRange",d.maxRange);
                number("projectileSpeed",d.projectileSpeed,true); number("projectileRadius",d.projectileRadius,true);
                number("projectileLifetime",d.projectileLifetime,true); number("explosionRadius",d.explosionRadius,true);
                number("explosionDamage",d.explosionDamage); number("fuseTime",d.fuseTime); number("bombGravity",d.bombGravity,true);
                if (d.minRange>d.maxRange) throw std::runtime_error("minRange exceeds maxRange: " + d.id);
                if (d.id.empty() || !next.emplace(d.id,d).second) throw std::runtime_error("Empty/duplicate enemy id: " + d.id);
            }
            if (!next.count("normal")) throw std::runtime_error("Missing normal fallback");
            definitions_=std::move(next); error_.clear(); return true;
        } catch (const std::exception& e) { error_=e.what(); return false; }
    }
    const EnemyDefinition* Find(const std::string& id) const {
        const auto it=definitions_.find(id); return it==definitions_.end() ? nullptr : &it->second;
    }
    const std::string& Error() const { return error_; }
private:
    std::map<std::string,EnemyDefinition> definitions_{{"normal",EnemyDefinition{}}};
    std::string error_;
};
