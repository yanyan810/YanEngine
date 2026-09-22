#include "DebugTimeline.h"
#include "DebugJsonEditor.h"
#include "EnemyAI.h"
#include "EnemyProjectile.h"
#include "EnemySpawnSystem.h"
#include "DetachedEnemyPart.h"
#include "WeaponSystem.h"
#include "StageProgress.h"
#include <cassert>
#include <iostream>
#include <limits>
using nlohmann::json;
struct State {
    EnemyAI ai;
    EnemyParts parts=MakeEnemyParts({{0,0,0},{1,2,1}});
    EnemyProjectileSystem bullets;
    EnemySpawnSystem spawns;
    WeaponRuntime weapon;
    StageProgress stage;
    DetachedPartMotion fragment;
    Vector3 position{},rotation{};
    std::mt19937 random{123};
    uint64_t frame=0;
};
static std::string Read(const std::string& path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()}; }
int main() {
    DebugTimeline<State> history;
    State state;
    EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
    assert(state.spawns.Load("../../resources/levels/fps_spawns.json",definitions));
    assert(state.stage.LoadGoals("../../resources/levels/fps_spawns.json"));
    WeaponSystem weapons; assert(weapons.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json"));
    state.weapon.Equip(*weapons.InitialWeapon());
    state.bullets.projectiles.push_back(MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,10}));
    state.fragment.Initialize({{0,0,0},{1,1,1}},{0,3,0},{},{1,1,1},{1,0,0},EnemyPartType::Head,{1,1,1},{});
    history.Push(state);
    const auto initialMagazine=state.weapon.Magazine();
    const auto initialRandom=state.random;
    state.ai.Update(state.position,state.rotation,{0,0,5},1.0f/60,false);
    state.bullets.Update(1.0f/60,{100,0,0});
    state.weapon.Step(1.0f/60,true,true,false);
    state.fragment.Update(1.0f/60);
    DamageEnemyPart(state.parts,EnemyPartType::Head,1000);
    state.stage.Update(1.0f/60,{3,0,52});
    state.spawns.Update(0,{3,0,4},[](const auto&,const auto&){return uint64_t{1};},[](uint64_t){return true;});
    (void)state.random(); state.frame=1; history.Push(state);
    const auto* back=history.Back(); assert(back && back->frame==0);
    assert(!EnemyPartsDead(back->parts) && back->stage.IsPlaying());
    assert(back->weapon.Magazine()==initialMagazine && back->fragment.age==0);
    assert(back->bullets.projectiles[0].position.z==0 && !back->spawns.Triggers()[0].activated);
    assert(back->random==initialRandom && back->position.z==0);
    assert(history.Back()==nullptr);
    const auto* forward=history.Forward(); assert(forward && forward->frame==1 && EnemyPartsDead(forward->parts));
    assert(!forward->stage.IsPlaying() && forward->spawns.Triggers()[0].activated);
    assert(forward->weapon.Magazine()<initialMagazine && forward->fragment.age>0);
    assert(forward->bullets.projectiles[0].position.z>0 && forward->position.z>0);
    assert(history.Forward()==nullptr);
    state=*history.Back(); state.frame=99; history.Push(state);
    assert(history.Size()==2 && history.Forward()==nullptr && history.Cursor()==1);
    for (uint64_t i=0;i<500;++i) { state.frame=i; history.Push(state); }
    assert(history.Size()==300);
    int count=0; while (history.Back()) ++count; assert(count==299);
    history.Clear(); assert(history.Size()==0 && !history.Back() && !history.Forward());

    const std::string path="debug-editor-test.json";
    const auto original=Read("../../resources/Data/enemies.json");
    { std::ofstream f(path); f<<original; }
    DebugJsonEditor editor; assert(editor.Open(path));
    json number=3; DebugJsonEditor::SetNumber(number,3.25); assert(number.is_number_float() && number==3.25);
    json countValue=5; DebugJsonEditor::SetNumber(countValue,8); assert(countValue.is_number_integer() && countValue==8);
    DebugJsonEditor::SetNumber(number,std::numeric_limits<double>::infinity()); assert(number==3.25);
    const auto validate=[](const std::string& file) { EnemyDefinitions d; return d.Load(file) ? std::string{} : d.Error(); };
    editor.document["enemies"][0]["collisionRadius"]=-1;
    assert(!editor.Save(validate) && Read(path)==original && !std::filesystem::exists(path+".debug-tmp"));
    editor.document["enemies"][0]["collisionRadius"]=.4;
    assert(editor.Save(validate));
    assert(Read(path+".debug-backup")==original);
    EnemyDefinitions edited; assert(edited.Load(path) && edited.Find("normal")->collisionRadius==.4f);
    const auto changed=Read(path);
    { std::ofstream f(path); f<<original; }
    assert(!editor.Save(validate) && Read(path)==original); // External edits are never overwritten.
    assert(editor.Open(path));
    editor.document["enemies"][0]["moveSpeed"]=3.5;
    assert(editor.Save(validate) && Read(path)!=original && Read(path)!=changed);
    assert(editor.Open(path) && !editor.dirty);
    assert(!editor.Open("absent-debug-editor-file.json") && editor.path==path);
    std::cout<<"Debug tools tests passed: rewind/forward, death/AI/spawn/projectile/fragment/weapon/stage/random restoration, branch/capacity, numeric edits, validation/atomic save/backup/external conflict.\n";
}
