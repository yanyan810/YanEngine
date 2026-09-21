#include "EnemySpawnSystem.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>

using nlohmann::json;
static json Configuration() {
    return json::parse(R"({
      "spawnPoints": [
        {"id":"A","position":[1,0,0],"rotation":[0,1,0]},
        {"id":"B","position":[2,0,0]},
        {"id":"C","position":[3,0,0]}],
      "spawnTriggers":[{"id":"T","position":[0,0,0],"size":[2,2,2],
        "spawnPointIds":["A","B","C"],"spawnCount":8,"maxAlive":4,
        "spawnInterval":0.5,"initialDelay":0,"selection":"RoundRobin","oneShot":true}]
    })");
}
struct Harness {
    EnemySpawnSystem system;
    std::set<uint64_t> alive;
    std::vector<std::string> points;
    uint64_t next = 0;
    void Load(const json& config) {
        { std::ofstream file("spawn-test.json"); file << config; }
        assert(system.Load("spawn-test.json"));
    }
    void Tick(float dt, Vector3 position = {}) {
        system.Update(dt, position, [&](const EnemySpawnPoint& point, const std::string& trigger) {
            assert(!trigger.empty());
            points.push_back(point.id);
            alive.insert(next);
            return next++;
        }, [&](uint64_t id) { return alive.count(id) != 0; });
    }
};
int main() {
    {
        Harness h; h.Load(Configuration());
        h.Tick(10, {5,0,0}); assert(h.next == 0);
        h.Tick(10); assert(h.next == 1); // activate now, not ten seconds ago
        h.Tick(.25f); assert(h.next == 1);
        h.Tick(.25f); assert(h.next == 2);
        h.Tick(1); assert(h.next == 4);
        h.Tick(100); assert(h.next == 4);
        h.alive.erase(0); h.Tick(0); assert(h.next == 5 && h.alive.size() == 4);
        h.alive.erase(1); h.Tick(.25f); assert(h.next == 5);
        h.Tick(.25f); assert(h.next == 6);
        h.alive.clear(); h.Tick(1); assert(h.next == 8);
        assert(!h.system.Triggers()[0].active);
        assert((h.points == std::vector<std::string>{"A","B","C","A","B","C","A","B"}));
        h.Tick(0, {5,0,0}); h.Tick(10); assert(h.next == 8);
    }
    {
        auto config = Configuration(); config["spawnTriggers"][0]["initialDelay"] = 2;
        Harness h; h.Load(config); h.Tick(1); assert(h.next == 0);
        h.Tick(1.5f); assert(h.next == 0); h.Tick(.5f); assert(h.next == 1);
        h.Tick(-10); assert(h.next == 1);
        h.Tick(.5f, {100,0,0}); assert(h.next == 2); // leaving does not cancel
    }
    {
        auto config = Configuration(); auto& t = config["spawnTriggers"][0];
        t["spawnCount"] = 2; t["maxAlive"] = 3; t["spawnInterval"] = 0; t["oneShot"] = false;
        Harness h; h.Load(config); h.Tick(0); assert(h.next == 2);
        h.Tick(100); assert(h.next == 2); // no repeated spawning while remaining inside
        h.Tick(0, {5,0,0}); h.Tick(0); assert(h.next == 3); // previous living enemies count
        h.Tick(100); assert(h.next == 3);
        h.alive.erase(0); h.Tick(0); assert(h.next == 4);
    }
    {
        auto config = Configuration(); auto& t = config["spawnTriggers"][0];
        t["selection"] = "Random"; t["spawnInterval"] = 0; t["maxAlive"] = 8;
        Harness h; h.Load(config); h.Tick(0); assert(h.next == 8);
        for (const auto& id : h.points) assert(id == "A" || id == "B" || id == "C");
    }
    {
        auto config = Configuration(); config["spawnTriggers"].push_back(config["spawnTriggers"][0]);
        config["spawnTriggers"][1]["id"] = "U";
        Harness h; h.Load(config); h.Tick(0); h.Tick(10); assert(h.next == 8);
        assert(h.system.Triggers()[0].livingEnemies.size() == 4);
        assert(h.system.Triggers()[1].livingEnemies.size() == 4);
    }
    {
        Harness h; h.Load(Configuration());
        const auto reject = [&](json config) {
            { std::ofstream file("spawn-test.json"); file << config; }
            assert(!h.system.Load("spawn-test.json"));
            assert(!h.system.Error().empty());
            assert(h.system.Points().size() == 3); // previous valid map retained
        };
        auto c = Configuration(); c["spawnTriggers"][0]["spawnPointIds"] = json::array(); reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnPointIds"] = {"missing"}; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["maxAlive"] = 0; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnCount"] = 1.5; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnInterval"] = -1; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["initialDelay"] = -1; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["selection"] = "typo"; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["size"] = {1,0,1}; reject(c);
        c = Configuration(); c["spawnPoints"][1]["id"] = "A"; reject(c);
        c = Configuration(); c["spawnTriggers"].push_back(c["spawnTriggers"][0]); reject(c);
        assert(!h.system.Load("nonexistent-spawn-file.json"));
    }
    {
        Harness h;
        assert(h.system.Load("../../resources/levels/fps_spawns.json"));
        assert(h.system.Points().size() == 7 && h.system.Triggers().size() == 2);
        h.Tick(0, {3,0,-6}); assert(h.next == 0);
        h.Tick(0, {3,0,2}); h.Tick(1, {3,0,4}); assert(h.next == 3);
        h.Tick(20, {3,0,4}); assert(h.next == 3);
        h.alive.clear(); h.Tick(0, {3,0,4}); h.Tick(.5f, {3,0,4}); assert(h.next == 5);
        h.Tick(0, {3,0,24}); assert(h.next == 5);
        h.Tick(2, {3,0,26}); assert(h.next == 6);
        h.Tick(1.5f, {3,0,26}); assert(h.next == 9);
        h.Tick(20, {3,0,26}); assert(h.next == 9);
        h.alive.clear(); h.Tick(0, {3,0,26}); h.Tick(1.5f, {3,0,26}); assert(h.next == 13);
        assert(h.system.Triggers()[0].spawned == 5 && h.system.Triggers()[1].spawned == 8);
        assert(!h.system.Triggers()[0].active && !h.system.Triggers()[1].active);
    }
    std::cout << "Enemy spawn tests passed: timing, capacity, 8/4 completion, selection, repeat entry, independent triggers, validation, sample map.\n";
}
