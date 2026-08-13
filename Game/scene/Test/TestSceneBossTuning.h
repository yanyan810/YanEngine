#pragma once

#include <cstddef>
#include <string>

class EnemyManager;
class Player;

class TestSceneBossTuning {
public:
    static bool Save(const std::string& path, const EnemyManager& enemyManager, const Player& player, std::string& status);
    static bool Load(const std::string& path, EnemyManager& enemyManager, Player& player, std::string& status);
    static bool SaveCustomAttack(const std::string& directory, const EnemyManager& enemyManager, size_t attackIndex, std::string& status);
    static bool LoadCustomAttacks(const std::string& directory, EnemyManager& enemyManager, std::string& status);
};
