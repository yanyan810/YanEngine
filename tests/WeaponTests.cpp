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
int main() {
    const auto definitions=Read("../../resources/Data/weapons.json");
    const auto level=Read("../../resources/levels/fps_spawns.json");
    WeaponSystem system;
    assert(system.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json",WeaponRandomSettings{true,12345}));
    assert(system.Points().size()==3 && system.InitialWeapon()->id=="pistol");
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
