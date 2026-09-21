#include "StageProgress.h"
#include "EnemySpawnSystem.h"
#include "EnemyAI.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
static json Level() {
    std::ifstream file("../../resources/levels/fps_spawns.json");
    return json::parse(file);
}
static void Save(const json& data) { std::ofstream("stage-test.json") << data; }
int main() {
    EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
    const auto config = Level();
    {
        StageProgress stage;
        Save(config); assert(stage.LoadGoals("stage-test.json"));
        assert(stage.IsPlaying() && stage.Time() == 0 && stage.DefeatedCount() == 0);
        stage.Update(1, {3,0,-6}); assert(stage.Time() == 1);
        stage.ObserveEnemy(1, false); assert(stage.DefeatedCount() == 0);
        stage.ObserveEnemy(1, true); stage.ObserveEnemy(1, true);
        stage.ObserveEnemy(2, true); assert(stage.DefeatedCount() == 2);
        assert(!stage.Update(1, {20,0,52})); // X outside
        assert(!stage.Update(1, {3,8,52})); // Y outside
        assert(stage.Update(.25f, {3,0,50})); // boundary inside
        assert(stage.State() == StageState::Cleared && stage.Goals()[0].activated);
        assert(StageProgress::FormatTime(stage.Time()) == "00:03.25");
        assert(!stage.Update(100, {3,0,52}));
        assert(!stage.Update(10, {3,0,0})); assert(!stage.Update(10, {3,0,52}));
        stage.ObserveEnemy(3, true);
        assert(stage.Time() == 3.25 && stage.DefeatedCount() == 2);
        stage.Reset();
        assert(stage.IsPlaying() && stage.Time() == 0 && stage.DefeatedCount() == 0 && !stage.Goals()[0].activated);
        assert(stage.Update(0, {3,0,52}));
    }
    {
        auto data = config; data.erase("goalTriggers"); Save(data);
        StageProgress stage; assert(stage.LoadGoals("stage-test.json"));
        assert(stage.Goals().empty() && !stage.Update(50,{3,0,52}));
        assert(stage.IsPlaying()); // legacy spawn-only levels remain playable
        for (const auto& invalid : {json(nullptr),json::object(),json("bad")}) {
            data["goalTriggers"] = invalid; Save(data); assert(!stage.LoadGoals("stage-test.json"));
        }
        data = config; data["goalTriggers"][0]["size"] = {1,0,1}; Save(data);
        assert(!stage.LoadGoals("stage-test.json"));
        data = config; data["goalTriggers"].push_back(data["goalTriggers"][0]); Save(data);
        assert(!stage.LoadGoals("stage-test.json"));
        data = config; data["goalTriggers"][0]["position"] = {1,2}; Save(data);
        assert(!stage.LoadGoals("stage-test.json"));
        assert(!stage.LoadGoals("missing-goal-file.json"));
        Save(config); assert(stage.LoadGoals("stage-test.json")); assert(stage.Error().empty());
    }
    {
        // Real level progression with living enemies and B waiting for capacity.
        StageProgress stage; EnemySpawnSystem spawns;
        Save(config); assert(stage.LoadGoals("stage-test.json")); assert(spawns.Load("stage-test.json", definitions));
        uint64_t generated = 0;
        EnemyAI ai;
        Vector3 enemyPosition{3,0,8}, enemyRotation{};
        float hp = 100;
        int shots = 0;
        const auto tick = [&](float dt, Vector3 player, bool fire = false) {
            stage.Update(dt,player);
            if (!stage.IsPlaying()) return;
            spawns.Update(dt,player,[&](const EnemySpawnPoint&,const std::string&){ return generated++; },[](uint64_t){return true;});
            hp -= ai.Update(enemyPosition,enemyRotation,player,dt,false);
            if (fire) ++shots;
        };
        tick(0,{3,0,-6}); assert(generated == 0);
        tick(0,{3,0,4}); tick(1,{3,0,4}); assert(generated == 3);
        tick(0,{3,0,26}); tick(3.5f,{3,0,26}); assert(generated == 7);
        assert(spawns.Triggers()[1].active && spawns.Triggers()[1].spawned == 4);
        tick(0,{3,0,52},true); assert(!stage.IsPlaying() && shots == 0);
        const auto frozenTime = stage.Time();
        const auto frozenHP = hp;
        const auto frozenPosition = enemyPosition;
        const auto frozenCooldown = ai.cooldown;
        const auto frozenSpawn = spawns.Triggers()[1].nextSpawn;
        tick(100,{3,0,52},true);
        assert(generated == 7 && hp == frozenHP && stage.Time() == frozenTime && shots == 0);
        assert(enemyPosition.x == frozenPosition.x && enemyPosition.z == frozenPosition.z && ai.cooldown == frozenCooldown);
        assert(spawns.Triggers()[1].nextSpawn == frozenSpawn);
    }
    {
        // Shared goal/spawn bounds: goal wins even against interval-zero spawning.
        auto data = config;
        data["goalTriggers"][0]["position"] = data["spawnTriggers"][0]["position"];
        data["spawnTriggers"][0]["spawnInterval"] = 0;
        Save(data); StageProgress stage; EnemySpawnSystem spawns;
        assert(stage.LoadGoals("stage-test.json") && spawns.Load("stage-test.json", definitions));
        assert(stage.Update(1,{3,0,4}));
        int generated = 0;
        if (stage.IsPlaying()) spawns.Update(1,{3,0,4},[&](const EnemySpawnPoint&,const std::string&){return static_cast<uint64_t>(++generated);},[](uint64_t){return true;});
        assert(generated == 0);
        stage.Reset(); assert(spawns.Load("stage-test.json", definitions));
        assert(stage.IsPlaying() && !spawns.Triggers()[0].activated && spawns.Triggers()[0].spawned == 0);
    }
    assert(StageProgress::FormatTime(92.45) == "01:32.45");
    assert(StageProgress::FormatTime(-1) == "00:00.00");
    assert(StageProgress::FormatTime(60) == "01:00.00");
    std::cout << "Stage tests passed: goal bounds, one-shot transition, frozen timer/results, unique deaths, reset, legacy JSON, validation, A/B/Goal with enemies alive, spawn/attack freeze and goal-before-spawn ordering.\n";
}
