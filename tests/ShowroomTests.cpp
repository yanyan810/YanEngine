#include "StageLoader.h"
#include "EnemySpawnSystem.h"
#include "EnemyAI.h"
#include "WeaponSystem.h"
#include "StageProgress.h"
#include <cassert>
#include <iostream>
#include <set>
int main() {
    const std::string path="../../resources/levels/showroom/showroom.json";
    StageLoader level; assert(level.Load(path)); assert(level.ValidateAssets("../../resources"));
    assert(level.id=="showroom" && level.collision.colliders.size()==5);
    EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
    EnemySpawnSystem spawns; assert(spawns.Load(path,definitions));
    StageProgress progress; assert(progress.LoadGoals(path));
    assert(spawns.Triggers().empty() && progress.Goals().empty());
    assert(!progress.Update(10000,{0,0,14}) && progress.IsPlaying());
    const std::array<std::string,5> order{"normal","ranged","fast","tank","bomber"};
    assert(spawns.Points().size()==order.size()+1);
    for (size_t i=0;i<order.size();++i) {
        const auto& p=spawns.Points()[i];
        assert(p.enemyPool.size()==1 && p.enemyPool[0].id==order[i]);
        assert(p.position.y==0 && p.position.z==14);
        if (i>0) assert(p.position.x-spawns.Points()[i-1].position.x>=4);
        assert(std::abs(p.rotation.y+1.57079632679f)<1e-5f);
        const auto* d=definitions.Find(order[i]); assert(d);
        auto parts=MakeEnemyParts({{-1,0,-1},{1,2,1}});
        ApplyEnemyHpMultiplier(parts,d->hpMultiplier);
        assert(DamageEnemyPart(parts,EnemyPartType::Head,10000)>0 && EnemyPartsDead(parts));
    }
    const auto& testPoint=spawns.Points().back();
    assert(testPoint.enemyPool.size()==1 && testPoint.enemyPool[0].id=="normal_test");
    assert(testPoint.position.x==-14 && testPoint.position.y==0 && testPoint.position.z==24);
    const auto* testDefinition=definitions.Find("normal_test");
    assert(testDefinition && testDefinition->type==EnemyType::Normal && testDefinition->partAsset);
    assert(testDefinition->partAsset->path.find("normal.test.enemy.json")!=std::string::npos);
    auto damaged=testDefinition->partAsset->Instantiate();
    assert(DamageEnemyPart(damaged,EnemyPartType::Head,10000)>0 && EnemyPartsDead(damaged));
    const auto restored=testDefinition->partAsset->Instantiate();
    assert(!EnemyPartsDead(restored));
    for (const auto& part:restored) assert(part.hp==part.maxHp);
    int triggered=0;
    spawns.Update(10000,level.playerPosition,[&](const auto&,const auto&){++triggered;return uint64_t{0};},[](uint64_t){return true;});
    assert(triggered==0);
    WeaponSystem weapons; assert(weapons.Load("../../resources/Data/weapons.json",path));
    assert(weapons.Pickups().size()==weapons.Points().size());
    std::set<std::string> ids;
    for (const auto& pickup : weapons.Pickups()) {
        assert(ids.insert(pickup.weaponId).second);
        const auto* d=weapons.Find(pickup.weaponId); assert(d);
        WeaponRuntime weapon;
        for (int repeat=0;repeat<3;++repeat) {
            assert(weapons.TryPickup(pickup.position,weapon));
            assert(weapon.Definition().id==pickup.weaponId && weapon.Magazine()==d->magazineSize);
            weapons.ResetPickups();
            assert(weapons.Nearest(pickup.position));
        }
        assert(weapon.Step(1,true,true,false)>0);
        assert(weapon.Magazine()<d->magazineSize);
    }
    for (const auto& p : weapons.Points()) assert(!p.weaponPool.empty() && ids.contains(p.weaponPool.front().id));
    // Same reusable-pickup API remains opt-in; main-level pickups are still consumed.
    WeaponSystem main; assert(main.Load("../../resources/Data/weapons.json","../../resources/levels/stage01/stage01.json"));
    WeaponRuntime gun; const auto position=main.Pickups().front().position;
    assert(main.TryPickup(position,gun)); assert(main.Pickups().front().pickedUp);
    // Floor permits level walking; room wall blocks travel and bullet rays.
    const auto moved=level.collision.Move({0,0,0},{1,0,1});
    assert(std::abs(moved.x-1)<.01f && std::abs(moved.z-1)<.01f);
    const auto blocked=level.collision.Move({0,0,0},{30,0,0}); assert(blocked.x<24);
    StageHit wall; assert(level.collision.Raycast({0,1.6f,0},{0,0,1},100,wall));
    assert(std::abs(wall.distance-40)<.01f);
    std::cout<<"Showroom tests passed: assets, lineup/order/spacing/facing, no triggers/goals, all weapons, repeat pickups, damage, room collision, main pickup isolation.\n";
}
