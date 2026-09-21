#include "WeaponSystem.h"
#include "StageProgress.h"
#include "EnemyParts.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

using nlohmann::json;
static json Read(const char* path) { std::ifstream file(path); return json::parse(file); }
static void Save(const json& weapons, const json& level) {
    std::ofstream("weapon-test.json") << weapons;
    std::ofstream("weapon-level-test.json") << level;
}
static void TestExtendedModes(const WeaponSystem& system) {
    const auto pistol=*system.Find("pistol");
    const auto smg=*system.Find("smg");
    const auto burst=*system.Find("burst_rifle");
    const auto pump=*system.Find("pump_shotgun");
    const auto automatic=*system.Find("auto_shotgun");
    assert(pump.reloadMode==WeaponReloadMode::PerRound && pump.pelletCount==8);
    assert(automatic.fireMode==WeaponFireMode::FullAuto && automatic.pelletCount==6);
    assert(burst.fireMode==WeaponFireMode::Burst && burst.burstCount==3);
    WeaponRuntime weapon;
    weapon.Equip(pistol);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(1,false,true,false)==0); // SemiAuto held
    assert(weapon.Step(0,true,true,false)==1);
    weapon.Equip(smg);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(.04f,false,true,false)==0);
    assert(weapon.Step(.04f,false,true,false)==1);
    assert(weapon.Step(1,false,false,false)==0);
    weapon.Equip(burst);
    assert(weapon.Step(0,true,true,false)==1 && weapon.BurstRemaining()==2);
    assert(weapon.Step(.03f,true,true,false)==0); // spam cannot overlap
    assert(weapon.Step(.04f,false,false,false)==1 && weapon.BurstRemaining()==1);
    assert(weapon.Step(.07f,false,false,false)==1 && weapon.BurstRemaining()==0);
    assert(weapon.Magazine()==27 && std::abs(weapon.Cooldown()-.45)<1e-5);
    assert(weapon.Step(.44f,true,true,false)==0);
    assert(weapon.Step(.02f,false,true,false)==0); // held does not start a burst
    assert(weapon.Step(0,true,true,false)==1);
    weapon.CancelBurst(); assert(weapon.BurstRemaining()==0 && weapon.Update(1)==0);
    auto scarce=burst; scarce.magazineSize=5; scarce.ammoPerShot=2;
    weapon.Equip(scarce);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(.07f,false,false,false)==1);
    assert(weapon.Magazine()==1 && weapon.BurstRemaining()==0);
    assert(weapon.Step(1,true,true,false)==0);
    auto instant=burst; instant.burstInterval=0;
    weapon.Equip(instant);
    assert(weapon.Step(0,true,true,false)==3 && weapon.Magazine()==27);
    // Burst scheduling and final cooldown agree across different frame rates.
    for (float dt : {1.0f/30,1.0f/144}) {
        weapon.Equip(burst);
        int shots=weapon.Step(0,true,true,false);
        float elapsed=0;
        while (elapsed<.3f) { shots+=weapon.Step(dt,false,false,false); elapsed+=dt; }
        assert(shots==3 && std::abs(weapon.Cooldown()-(.14+.45-elapsed))<1e-5);
    }
    auto emptyPump=pump; emptyPump.magazineSize=2; emptyPump.reserveAmmo=3;
    weapon.Equip(emptyPump);
    assert(weapon.TryFire()); weapon.Update(1); assert(weapon.TryFire()); weapon.Update(1);
    assert(weapon.Magazine()==0 && weapon.StartReload());
    assert(weapon.ReloadState()==WeaponReloadState::Starting && !weapon.TryFire());
    weapon.Update(.24f); assert(weapon.Magazine()==0);
    weapon.Update(.01f); assert(weapon.Magazine()==1 && weapon.Reserve()==2);
    assert(weapon.ReloadState()==WeaponReloadState::InsertingRound);
    weapon.Update(.55f); assert(weapon.Magazine()==2 && weapon.Reserve()==1);
    assert(weapon.ReloadState()==WeaponReloadState::Finishing);
    weapon.Update(.29f); assert(weapon.Reloading());
    weapon.Update(.01f); assert(!weapon.Reloading() && !weapon.StartReload());
    weapon.Equip(pump);
    for (int i=0;i<6;++i) { assert(weapon.TryFire()); weapon.Update(1); }
    assert(weapon.Magazine()==2 && weapon.StartReload());
    weapon.Update(.8f); assert(weapon.Magazine()==4);
    assert(weapon.TryFire()); assert(weapon.Magazine()==3 && !weapon.Reloading());
    const auto reserve=weapon.Reserve(); weapon.Update(10); assert(weapon.Reserve()==reserve);
    auto locked=pump; locked.reloadCanInterrupt=false;
    weapon.Equip(locked); assert(weapon.TryFire()); weapon.Update(1); assert(weapon.StartReload());
    assert(!weapon.TryFire()); weapon.Update(.25f); assert(!weapon.TryFire());
    weapon.Update(.3f); assert(weapon.TryFire());
    auto lowReserve=pump; lowReserve.reserveAmmo=1;
    weapon.Equip(lowReserve); assert(weapon.TryFire()); weapon.Update(1); assert(weapon.TryFire()); weapon.Update(1);
    assert(weapon.StartReload()); weapon.Update(.25f);
    assert(weapon.Reserve()==0 && weapon.Magazine()==7 && weapon.ReloadState()==WeaponReloadState::Finishing);
    weapon.Update(.3f); assert(!weapon.Reloading() && !weapon.StartReload());
    auto zero=pump; zero.reloadStartTime=zero.reloadPerRoundTime=zero.reloadEndTime=0;
    weapon.Equip(zero); assert(weapon.TryFire()); assert(weapon.StartReload());
    assert(!weapon.Reloading() && weapon.Magazine()==8 && weapon.Reserve()==39);
    // Eight generated rays, one ammo payment; change ammo cost without changing pellet count.
    weapon.Equip(pump);
    std::mt19937 random(1);
    int rays=0;
    for (int shots=weapon.Step(0,true,true,false); shots>0; --shots)
        for (int pellet=0; pellet<weapon.Definition().pelletCount; ++pellet) {
            const auto direction=WeaponPelletDirection({0,0,1},{1,0,0},{0,1,0},pump.hipSpreadDegrees,random);
            assert(direction.z>0); ++rays;
        }
    assert(rays==8 && weapon.Magazine()==7);
    auto doubleShot=pump; doubleShot.ammoPerShot=2;
    weapon.Equip(doubleShot); assert(weapon.Step(0,true,true,false)==1 && weapon.Magazine()==6 && weapon.Definition().pelletCount==8);
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    weapon.Equip(pistol); assert(weapon.BurstRemaining()==0 && weapon.BurstTimer()==0 && weapon.Cooldown()==0 && !weapon.Reloading());
    assert(weapon.Update(1)==0 && weapon.Magazine()==12);
    weapon.Equip(pump); assert(weapon.TryFire()); assert(weapon.StartReload());
    weapon.Equip(smg); assert(weapon.ReloadState()==WeaponReloadState::None && weapon.ReloadRemaining()==0 && weapon.Cooldown()==0);
    WeaponSystem pickups=system;
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    assert(pickups.TryPickup(pickups.Pickups()[0].position,weapon));
    assert(weapon.BurstRemaining()==0 && weapon.Cooldown()==0 && weapon.BurstTimer()==0);
    weapon.Equip(pump); assert(weapon.TryFire()); assert(weapon.StartReload());
    assert(pickups.TryPickup(pickups.Pickups()[1].position,weapon));
    assert(!weapon.Reloading() && weapon.ReloadRemaining()==0 && weapon.Cooldown()==0);
    const int freshMagazine=weapon.Magazine();
    assert(weapon.Update(5)==0 && weapon.Magazine()==freshMagazine);
    // Stage clear leaves the entire runtime untouched; no Step call after Cleared.
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    const int remaining=weapon.BurstRemaining(); const auto timer=weapon.BurstTimer();
    StageProgress stage; assert(stage.LoadGoals("../../resources/levels/fps_spawns.json"));
    assert(stage.Update(0,{3,0,52}));
    if (stage.IsPlaying()) weapon.Step(1,false,false,false);
    assert(weapon.BurstRemaining()==remaining && weapon.BurstTimer()==timer);
    std::cout << "Extended weapon tests passed: Semi/Full/Burst, burst timing and exhaustion, ammo/pellet independence, PerRound phases, interruption, reserve/full stop, zero timings, equip reset, stage freeze.\n";
}
int main() {
    const auto definitions=Read("../../resources/Data/weapons.json");
    const auto level=Read("../../resources/levels/fps_spawns.json");
    WeaponSystem system;
    assert(system.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json",WeaponRandomSettings{true,12345}));
    assert(system.Points().size()==3 && system.InitialWeapon()->id=="pistol");
    TestExtendedModes(system);
    const auto pistol = *system.Find("pistol");
    const auto smg = *system.Find("smg");
    const auto rifle = *system.Find("rifle");
    const auto shotgun = *system.Find("shotgun");

    {
        // FireMode
        assert(pistol.fireMode == WeaponFireMode::SemiAuto);
        assert(smg.fireMode == WeaponFireMode::FullAuto);
        assert(rifle.fireMode == WeaponFireMode::FullAuto);
        assert(shotgun.fireMode == WeaponFireMode::SemiAuto);

        // SemiAuto:
        // 押した瞬間だけ発射要求
        assert(WeaponWantsFire(
            WeaponFireMode::SemiAuto,
            true,
            true));

        // 長押しだけでは次弾を撃たない
        assert(!WeaponWantsFire(
            WeaponFireMode::SemiAuto,
            false,
            true));

        // FullAuto:
        // 最初のクリックでも撃つ
        assert(WeaponWantsFire(
            WeaponFireMode::FullAuto,
            true,
            true));

        // クリックTriggerがなくても長押し中なら撃つ
        assert(WeaponWantsFire(
            WeaponFireMode::FullAuto,
            false,
            true));

        // 離したら撃たない
        assert(!WeaponWantsFire(
            WeaponFireMode::FullAuto,
            false,
            false));

        // ADS設定
        assert(pistol.adsFovDegrees == 50.0f);
        assert(smg.adsFovDegrees == 45.0f);
        assert(rifle.adsFovDegrees == 35.0f);
        assert(shotgun.adsFovDegrees == 48.0f);

        assert(pistol.adsSensitivityMultiplier == 0.90f);
        assert(smg.adsSensitivityMultiplier == 0.80f);
        assert(rifle.adsSensitivityMultiplier == 0.70f);
        assert(shotgun.adsSensitivityMultiplier == 0.85f);

        // ADS時のSpreadはHipより狭い
        assert(pistol.adsSpreadDegrees <= pistol.hipSpreadDegrees);
        assert(smg.adsSpreadDegrees <= smg.hipSpreadDegrees);
        assert(rifle.adsSpreadDegrees <= rifle.hipSpreadDegrees);
        assert(shotgun.adsSpreadDegrees <= shotgun.hipSpreadDegrees);
    }

    {
        WeaponRuntime weapon;
        assert(!weapon.TryFire() && !weapon.StartReload());
        weapon.Equip(pistol);
        assert(weapon.Magazine()==12 && weapon.Reserve()==60);
        assert(!weapon.StartReload());
        int shots=0;
        for (int i=0; i<12; ++i) {
            if (weapon.TryFire()) ++shots;
            assert(!weapon.TryFire());
            weapon.Update(.125f); assert(!weapon.TryFire());
            weapon.Update(.125f);
        }
        assert(shots==12 && weapon.Magazine()==0 && !weapon.TryFire());
        assert(weapon.StartReload() && !weapon.StartReload() && !weapon.TryFire());
        weapon.Update(1); assert(weapon.Reloading() && weapon.Magazine()==0 && weapon.Reserve()==60);
        weapon.Update(.5f); assert(!weapon.Reloading() && weapon.Magazine()==12 && weapon.Reserve()==48);
        assert(weapon.TryFire());
        assert(weapon.StartReload());
        weapon.Equip(smg); // drops old weapon and cancels reload/cooldown
        assert(!weapon.Reloading() && weapon.Cooldown()==0 && weapon.Magazine()==30 && weapon.Reserve()==120);
        assert(weapon.TryFire()); weapon.Update(.08f); assert(weapon.TryFire());
        WeaponRuntime other; other.Equip(smg); assert(other.Magazine()==30);
    }
    {
        auto scarce=smg; scarce.reserveAmmo=10;
        WeaponRuntime weapon; weapon.Equip(scarce);
        for (int i=0;i<25;++i) { assert(weapon.TryFire()); weapon.Update(1); }
        assert(weapon.Magazine()==5 && weapon.StartReload());
        weapon.Update(2); assert(weapon.Magazine()==15 && weapon.Reserve()==0 && !weapon.StartReload());
        auto quick=pistol; quick.reloadTime=0;
        weapon.Equip(quick); assert(weapon.TryFire()); assert(weapon.StartReload());
        assert(!weapon.Reloading() && weapon.Magazine()==12 && weapon.Reserve()==59);
        const double cooldown=weapon.Cooldown();
        weapon.Update(-1); weapon.Update(std::numeric_limits<float>::quiet_NaN());
        assert(weapon.Cooldown()==cooldown);
    }
    {
        const auto selection=system.Pickups();
        WeaponRuntime weapon; weapon.Equip(pistol);
        assert(!system.TryPickup({100,0,100},weapon));
        for (size_t i=0;i<selection.size();++i) {
            assert(system.Nearest(selection[i].position)==i);
            assert(system.TryPickup(selection[i].position,weapon));
            assert(weapon.Definition().id==selection[i].weaponId);
            assert(!system.Pickups()[i].visible && system.Pickups()[i].pickedUp);
            assert(!system.TryPickup(selection[i].position,weapon));
        }
        assert(system.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json",WeaponRandomSettings{true,12345}));
        for (size_t i=0;i<selection.size();++i) {
            assert(system.Pickups()[i].weaponId==selection[i].weaponId);
            assert(system.Pickups()[i].visible && !system.Pickups()[i].pickedUp);
            bool member=false;
            for (const auto& entry:system.Points()[i].weaponPool) member |= entry.id==selection[i].weaponId;
            assert(member);
        }
    }
    {
        auto map=level;
        map["weaponSpawnPoints"][0]["position"]={1,0,0};
        map["weaponSpawnPoints"][1]["position"]={.5,0,0};
        map["weaponSpawnPoints"][2]["position"]={.5,0,0};
        map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",0}},{{"id","smg"},{"weight",1}}};
        Save(definitions,map); assert(system.Load("weapon-test.json","weapon-level-test.json"));
        assert(system.Pickups()[0].weaponId=="smg");
        assert(system.Nearest({0,0,0})==1); // nearest, stable tie resolution
        WeaponRuntime weapon; assert(system.TryPickup({0,0,0},weapon));
        assert(system.Nearest({0,0,0})==2);
        map.erase("weaponSpawnPoints"); Save(definitions,map);
        assert(system.Load("weapon-test.json","weapon-level-test.json") && system.Pickups().empty());
    }
    {
        std::mt19937 random(42);
        const Vector3 forward{0,0,1}, right{1,0,0}, up{0,1,0};
        for (int i=0;i<100;++i) {
            const auto ray=WeaponPelletDirection(forward,right,up,shotgun.hipSpreadDegrees,random);
            assert(std::abs(ray.x*ray.x+ray.y*ray.y+ray.z*ray.z-1)<1e-5f);
            assert(ray.z>=std::cos(shotgun.hipSpreadDegrees *.01745329252f)-1e-5f);
        }
        const auto straight=WeaponPelletDirection(forward,right,up,0,random);
        assert(straight.x==0 && straight.y==0 && straight.z==1);
        WeaponRuntime weapon; weapon.Equip(shotgun);
        assert(weapon.TryFire());
        assert(weapon.Magazine()==7 && shotgun.pelletCount==8); // eight rays cost one shell
        const AABB bounds{{-.38f,.016f,-1.02f},{.22f,2.482f,1.02f}};
        auto parts=MakeEnemyParts(bounds);
        const auto& arm=parts[3];
        const auto center=(arm.bounds.min+arm.bounds.max)*.5f;
        EnemyPartHit hit;
        assert(!RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),{bounds.min.x-2,center.y,center.z},{1,0,0},1,hit));
        assert(RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),{bounds.min.x-2,center.y,center.z},{1,0,0},pistol.range,hit));
        assert(DamageEnemyPart(parts,hit.part,pistol.damage)==25 && arm.hp==35);
        assert(DamageEnemyPart(parts,hit.part,smg.damage)==15 && arm.hp==20);
    }
    {
        Save(definitions,level);
        StageProgress stage; assert(stage.LoadGoals("weapon-level-test.json"));
        assert(system.Load("weapon-test.json","weapon-level-test.json"));
        WeaponRuntime weapon; weapon.Equip(pistol); assert(weapon.TryFire()); assert(weapon.StartReload());
        assert(stage.Update(1,{3,0,52}));
        const auto frozenReload=weapon.ReloadRemaining();
        int shots=0;
        if (stage.IsPlaying()) {
            weapon.Update(100);
            system.TryPickup(system.Pickups()[0].position,weapon);
            weapon.StartReload();
            if (weapon.TryFire()) ++shots;
        }
        assert(shots==0 && weapon.Magazine()==11 && weapon.ReloadRemaining()==frozenReload);
        assert(system.Pickups()[0].visible);
    }
    {
        const auto reject=[&](json data,json map) {
            Save(data,map);
            assert(!system.Load("weapon-test.json","weapon-level-test.json"));
            assert(!system.Error().empty() && system.Points().size()==3);
        };
        auto data=definitions; 
        data["weapons"][0]["magazineSize"]=0; 
        reject(data,level);

        data = definitions;
        data["weapons"][0]["fireMode"] = "Unknown";
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsFov"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsTransitionTime"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsSensitivityMultiplier"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["hipSpreadDegrees"] = -1;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsSpreadDegrees"] = -1;
        reject(data, level);

        data=definitions; data["weapons"][0]["fireInterval"]=-1; reject(data,level);
        data=definitions; data["weapons"][0]["reloadTime"]=-1; reject(data,level);
        data=definitions; data["weapons"][0]["magazineSize"]=1.5; reject(data,level);
        data=definitions; data["weapons"][0]["pelletCount"]=100; reject(data,level);
        data=definitions; data["weapons"][1]["id"]="pistol"; reject(data,level);
        for (const auto* field : {"ammoPerShot","pelletCount","burstCount","magazineSize"}) {
            data=definitions; data["weapons"][0][field]=0; reject(data,level);
            data=definitions; data["weapons"][0][field]=-1; reject(data,level);
        }
        for (const auto* field : {"burstInterval","reloadTime","reloadStartTime","reloadPerRoundTime","reloadEndTime"}) {
            data=definitions; data["weapons"][0][field]=-1; reject(data,level);
        }
        data=definitions; data["weapons"][0]["reloadMode"]="Unknown"; reject(data,level);

        auto map=level; 
        map["weaponSpawnPoints"][0]["weaponPool"]={"missing"};
        reject(definitions,map);

        map=level; map["weaponSpawnPoints"][0]["weaponPool"]=json::array(); reject(definitions,map);
        map=level; map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",0}}}; reject(definitions,map);
        map=level; map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",-1}}}; reject(definitions,map);
        map=level; map["weaponSpawnPoints"][1]["id"]="WeaponSpawn_01"; reject(definitions,map);
        map=level; map["weaponRandom"]["seed"]=-1; reject(definitions,map);
    }
    std::cout << "Weapon tests passed: ammo, dry fire, cooldown, reload/partial/empty reserve, equip cancellation, independent state, nearest/ties, consumed pickup, fixed-seed restart, weighted pools, legacy level, pellets/cone, fire modes, ADS config, weapon damage/range through part raycasts, cleared-stage freeze, invalid data.\n";
}
