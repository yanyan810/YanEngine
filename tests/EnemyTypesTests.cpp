#include "EnemyDefinition.h"
#include "EnemyAI.h"
#include "EnemyProjectile.h"
#include "EnemySpawnSystem.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using nlohmann::json;
static void Save(const json& j) { std::ofstream("enemy-types-test.json")<<j; }
static bool Near(float a,float b) { return std::abs(a-b)<.001f; }
int main() {
    EnemyDefinitions definitions;
    assert(definitions.Load("../../resources/Data/enemies.json"));
    json valid; std::ifstream("../../resources/Data/enemies.json")>>valid;
    for (auto type : {EnemyType::Normal,EnemyType::Ranged,EnemyType::Fast,EnemyType::Tank,EnemyType::Bomber}) {
        std::string id=EnemyTypeName(type); id[0]=static_cast<char>(std::tolower(id[0]));
        const auto* d=definitions.Find(id); assert(d && d->type==type);
    }
    const auto reject=[&](json data) {
        Save(data); assert(!definitions.Load("enemy-types-test.json"));
        assert(definitions.Find("tank") && definitions.Find("tank")->hpMultiplier==2.5f);
    };
    auto bad=valid; bad["enemies"][0]["type"]="Unknown"; reject(bad);
    bad=valid; bad["enemies"][0]["id"]=""; reject(bad);
    bad=valid; bad["enemies"].push_back(bad["enemies"][0]); reject(bad);
    for (auto key : {"detectionRange","moveSpeed","attackRange","attackDamage","minRange","preferredRange","maxRange","explosionDamage","fuseTime"}) {
        bad=valid; bad["enemies"][0][key]=-1; reject(bad);
    }
    for (auto key : {"attackInterval","hpMultiplier","projectileSpeed","projectileRadius","projectileLifetime","explosionRadius","bombGravity"}) {
        bad=valid; bad["enemies"][0][key]=0; reject(bad);
    }
    bad=valid; bad["enemies"][1]["minRange"]=100; reject(bad);
    bad=valid; bad["enemies"][0]["moveSpeed"]="bad"; reject(bad);
    assert(!definitions.Load("missing-enemy-definition.json"));
    for (const auto& item : valid["enemies"]) {
        const auto& d=*definitions.Find(item["id"].get<std::string>());
        auto parts=MakeEnemyParts({{0,0,0},{1,1,1}});
        ApplyEnemyHpMultiplier(parts,d.hpMultiplier);
        for (const auto& p : parts) assert(Near(p.hp,EnemyPartMaxHp(p.type)*d.hpMultiplier) && p.hp==p.maxHp);
        assert(!EnemyPartsDead(parts));
        DamageEnemyPart(parts,EnemyPartType::Head,10000); assert(EnemyPartsDead(parts));
        ApplyEnemyHpMultiplier(parts,d.hpMultiplier);
        DamageEnemyPart(parts,EnemyPartType::Body,10000); assert(EnemyPartsDead(parts));
    }
    assert(definitions.Find("fast")->moveSpeed>definitions.Find("normal")->moveSpeed);
    assert(definitions.Find("tank")->hpMultiplier>definitions.Find("normal")->hpMultiplier);
    for (auto id : {"ranged","bomber"}) {
        const auto& d=*definitions.Find(id);
        EnemyAI ai; ai.settings={d.detectionRange,d.attackRange,d.moveSpeed,d.attackDamage,d.attackInterval,true,d.minRange,d.maxRange};
        Vector3 pos{}, rot{};
        ai.Update(pos,rot,{0,0,d.maxRange+2},.1f,false); assert(pos.z>0 && ai.attacksThisUpdate==0);
        pos={}; ai.Update(pos,rot,{0,0,d.minRange-2},.1f,false); assert(pos.z<0 && ai.attacksThisUpdate==0);
        pos={}; ai.Update(pos,rot,{0,0,d.preferredRange},.1f,false); assert(pos.z==0 && ai.attacksThisUpdate==1);
        ai.Update(pos,rot,{0,0,d.preferredRange},.01f,false); assert(ai.attacksThisUpdate==0);
        ai.Update(pos,rot,{0,0,d.preferredRange},10,true); assert(ai.attacksThisUpdate==0 && ai.state==EnemyState::Dead);
    }
    auto bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,12});
    assert(StepEnemyProjectile(bullet,.25f,{100,0,0})==0 && Near(bullet.position.z,3));
    assert(StepEnemyProjectile(bullet,1,{0,0,9})==8 && !bullet.active);
    assert(StepEnemyProjectile(bullet,1,{0,0,9})==0);
    bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,12});
    StepEnemyProjectile(bullet,10,{1000,0,0}); assert(!bullet.active && Near(bullet.position.z,60));
    auto bomb=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{0,0,14});
    const float vy=bomb.velocity.y;
    assert(StepEnemyProjectile(bomb,.1f,{0,0,14})==0 && bomb.position.y>1.2f && bomb.velocity.y<vy);
    StepEnemyProjectile(bomb,1.65f,{100,0,0}); assert(bomb.grounded && Near(bomb.position.y,bomb.radius));
    assert(Near(bomb.position.z,14) && Near(bomb.fuseRemaining,1.2f));
    const auto outside=bomb;
    assert(StepEnemyProjectile(bomb,1,{0,0,14})==0 && bomb.active && Near(bomb.position.z,14));
    assert(StepEnemyProjectile(bomb,.21f,{0,0,14})==25 && !bomb.active);
    assert(StepEnemyProjectile(bomb,1,{0,0,14})==0);
    bomb=outside; assert(StepEnemyProjectile(bomb,1.21f,{0,0,30})==0 && !bomb.active);
    bomb=outside; bomb.lifetime=.1f; assert(StepEnemyProjectile(bomb,10,{0,0,14})==0 && !bomb.active);
    EnemyProjectileSystem projectiles; projectiles.projectiles.push_back(outside); projectiles.Clear();
    assert(projectiles.projectiles.empty() && projectiles.Update(10,{0,0,14})==0);
    EnemySpawnSystem spawns;
    json map={{"spawnPoints",json::array({{{"id","A"},{"position",{0,0,0}}}})},{"spawnTriggers",json::array()}};
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    assert(spawns.SelectEnemyId(spawns.Points()[0])=="normal");
    map["spawnPoints"][0]["enemyPool"]=json::array({{{"id","normal"},{"weight",5}},{{"id","fast"},{"weight",2}},{{"id","tank"},{"weight",0}}});
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    spawns.SetSeed(42); std::vector<std::string> sequence; int normal=0;
    for (int i=0;i<7000;++i) { auto id=spawns.SelectEnemyId(spawns.Points()[0]); assert(id!="tank"); normal+=id=="normal"; sequence.push_back(id); }
    assert(normal>4700 && normal<5300);
    spawns.SetSeed(42); for (const auto& id : sequence) assert(id==spawns.SelectEnemyId(spawns.Points()[0]));
    const auto rejectPool=[&](json data) { Save(data); assert(!spawns.Load("enemy-types-test.json",definitions)); assert(spawns.Points()[0].enemyPool.size()==3); };
    bad=map; bad["spawnPoints"][0]["enemyPool"][0]["id"]="missing"; rejectPool(bad);
    bad=map; bad["spawnPoints"][0]["enemyPool"][0]["weight"]=-1; rejectPool(bad);
    bad=map; for (auto& e : bad["spawnPoints"][0]["enemyPool"]) e["weight"]=0; rejectPool(bad);
    bad=map; bad["spawnPoints"][0]["enemyPool"]=json::array(); rejectPool(bad);
    map["enemyRandom"]={{"useFixedSeed",true},{"seed",12345}};
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    sequence.clear();
    for (int i=0;i<100;++i) sequence.push_back(spawns.SelectEnemyId(spawns.Points()[0]));
    assert(spawns.Load("enemy-types-test.json",definitions));
    for (const auto& id : sequence) assert(id==spawns.SelectEnemyId(spawns.Points()[0]));
    bad=map; bad["enemyRandom"]["seed"]=-1; rejectPool(bad);
    assert(spawns.Load("../../resources/levels/fps_spawns.json",definitions));
    std::map<std::string,int> counts;
    for (int i=0;i<2000;++i) ++counts[spawns.SelectEnemyId(spawns.Points()[0])];
    assert(counts.size()==5);
    auto single=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{0,0,14});
    auto sliced=single;
    StepEnemyProjectile(single,2,{100,0,0});
    for (int i=0;i<200;++i) StepEnemyProjectile(sliced,.01f,{100,0,0});
    assert(Near(single.position.z,sliced.position.z) && Near(single.fuseRemaining,sliced.fuseRemaining));
    std::cout<<"Enemy types: definitions, validation, HP/death, ranged AI, bullet sweep/lifetime, bomb gravity/ground/fuse/explosion, pools and cleanup passed\n";
}
