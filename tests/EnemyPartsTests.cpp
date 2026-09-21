#include "EnemyParts.h"
#include "EnemyAI.h"
#include "DetachedEnemyPart.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
int main() {
    // Off-center Boss-like mesh bounds: depth X, height Y, span Z.
    const AABB bounds{{-.38f,.016f,-1.02f},{.22f,2.482f,1.02f}};
    const auto parts=MakeEnemyParts(bounds);
    const std::array<Transform,4> transforms{{{{1,1,1},{},{0,0,0}},
        {{2,2,2},{0,1.5707963f,0},{3,0,2}},
        {{.5f,3,1.4f},{.3f,-.7f,.2f},{-8,4,10}},
        {{-2,1,3},{-.4f,1.2f,.1f},{5,-2,-7}}}};
    for(const auto& transform:transforms) {
        const auto world=Matrix4x4::MakeAffineMatrix(transform.scale,transform.rotate,transform.translate);
        for(const auto& part:parts) {
            const auto center=(part.bounds.min+part.bounds.max)*.5f;
            const Vector3 originLocal{bounds.min.x-2,center.y,center.z};
            const auto origin=EnemyPartTransformPoint(originLocal,world);
            const auto target=EnemyPartTransformPoint(center,world);
            EnemyPartHit hit;
            assert(RaycastEnemyParts(parts,world,origin,target-origin,100,hit));
            assert(hit.hit && hit.part==part.type);
            assert(std::abs(hit.distance-2*std::abs(transform.scale.x))<.0001f);
            assert(!RaycastEnemyParts(parts,world,origin,target-origin,hit.distance*.9f,hit));
            assert(!hit.hit && hit.part==EnemyPartType::None);
        }
    }
    auto damaged = parts;
    const float expected[] = {35, 10, 0, 0};
    const float lost[] = {25, 25, 10, 0};
    for (int shot = 0; shot < 3; ++shot) {
        const auto& arm = damaged[3];
        const auto center = (arm.bounds.min + arm.bounds.max) * .5f;
        EnemyPartHit shotHit;
        assert(RaycastEnemyParts(damaged, Matrix4x4::MakeIdentity4x4(),
            {bounds.min.x - 2, center.y, center.z}, {1,0,0}, 100, shotHit));
        assert(shotHit.part == EnemyPartType::RightArm);
        assert(DamageEnemyPart(damaged, shotHit.part, 25) == lost[shot]);
        assert(arm.hp == expected[shot]);
        for (size_t i = 0; i < parts.size(); ++i)
            if (i != 3) assert(damaged[i].hp == parts[i].hp);
    }
    assert(damaged[3].DamageState() == EnemyPartDamageState::Destroyed);
    assert(damaged[3].DamageRate() == 1);
    assert(DamageEnemyPart(damaged, EnemyPartType::RightArm, 25) == 0);
    auto stages = parts;
    assert(stages[1].hp == 100 && stages[1].DamageRate() == 0);
    const EnemyPartDamageState states[] = {EnemyPartDamageState::Normal,
        EnemyPartDamageState::LightDamage, EnemyPartDamageState::HeavyDamage,
        EnemyPartDamageState::Critical, EnemyPartDamageState::Destroyed};
    for (int i = 0; i < 5; ++i) {
        assert(stages[1].DamageState() == states[i]);
        if (i < 4) assert(DamageEnemyPart(stages, EnemyPartType::Body, 25) == 25);
    }
    auto invalid = parts;
    assert(DamageEnemyPart(invalid, EnemyPartType::Head, -25) == 0);
    assert(DamageEnemyPart(invalid, EnemyPartType::Head, std::numeric_limits<float>::quiet_NaN()) == 0);
    assert(DamageEnemyPart(invalid, EnemyPartType::None, 25) == 0);
    assert(invalid[0].hp == 50);
    std::puts("Damage tests passed: independent RightArm 60/35/10/0/0, overkill, destroyed damage clamp, all five states, invalid damage.");
    for (const auto& part : parts) {
        DetachedPartMotion motion;
        DetachedPartSettings config;
        motion.Initialize(part.bounds, {3,10,2}, {.2f,.7f,.1f}, {2,3,1}, {0,0,10},
            part.type, {2,-4,6}, config);
        const auto initial = motion.Translation();
        assert(std::hypot(initial.x-3,initial.y-10,initial.z-2) < .0001f);
        assert(std::abs(motion.velocity.z-6*DetachedPartWeight(part.type)) < .0001f);
        auto slow = motion;
        for (int i=0; i<30; ++i) motion.Update(1.0f/30);
        for (int i=0; i<144; ++i) slow.Update(1.0f/144);
        assert(std::hypot(motion.position.x-slow.position.x,motion.position.y-slow.position.y,
            motion.position.z-slow.position.z) < .001f);
        for (int i=0; i<240; ++i) {
            motion.Update(1.0f/60);
            const auto world = Matrix4x4::MakeAffineMatrix(motion.scale,motion.rotation,motion.Translation());
            assert(TransformAABB(motion.bounds,world).min.y >= -.0001f);
        }
        motion.Update(.1f);
        assert(!motion.Active() && motion.settled);
    }
    std::puts("Detached tests passed: six parts, spawn continuity, weights, 30/144Hz ballistic agreement, ground support, settle and expiry.");
    // Asymmetric triangle: centroid, not AABB center, is the rotation pivot.
    DetachedPartMotion face;
    const std::array<Vector3,3> offsets{{{-2,-1,0},{2,-1,0},{0,2,0}}};
    DetachedPartSettings faceConfig;
    face.Initialize({{-2,-1,0},{2,2,0}}, {3,8,5}, {}, {1,1,1}, {0,0,1},
        EnemyPartType::RightArm,{2,3,4},faceConfig);
    face.pivot={}; face.position={3,8,5};
    auto triangleWorld=Matrix4x4::MakeAffineMatrix({1,1,1},face.rotation,face.Translation());
    auto sum=Vector3{};
    for (auto v:offsets) sum=sum+EnemyPartTransformPoint(v,triangleWorld);
    assert(std::hypot(sum.x/3-3,sum.y/3-8,sum.z/3-5)<.0001f);
    face.Update(.1f);
    triangleWorld=Matrix4x4::MakeAffineMatrix({1,1,1},face.rotation,face.Translation());
    sum={};
    for (auto v:offsets) sum=sum+EnemyPartTransformPoint(v,triangleWorld);
    assert(std::hypot(sum.x/3-face.position.x,sum.y/3-face.position.y,sum.z/3-face.position.z)<.0001f);
    std::puts("Face centroid-pivot test passed.");
    EnemyAI ai;
    Vector3 aiPosition{0,0,24}, aiRotation{};
    assert(ai.Update(aiPosition,aiRotation,{},.1f,false)==0 && ai.state==EnemyState::Idle);
    aiPosition={0,0,10};
    ai.Update(aiPosition,aiRotation,{},1,false);
    assert(ai.state==EnemyState::Chase && std::abs(aiPosition.z-7.5f)<.0001f && aiPosition.y==0);
    const auto facing=Matrix4x4::MakeAffineMatrix({1,1,1},aiRotation,{});
    const auto forward=EnemyPartTransformPoint({-1,0,0},facing);
    assert(std::abs(forward.z+1)<.0001f);
    aiPosition={0,0,1.5f};
    const auto beforeAttack=aiPosition;
    float damage=0;
    for(int i=0;i<120;++i)damage+=ai.Update(aiPosition,aiRotation,{},1.0f/60,false);
    assert(damage>=20 && damage<=30 && aiPosition.z==beforeAttack.z);
    assert(ai.Update(aiPosition,aiRotation,{},1,true)==0 && ai.state==EnemyState::Dead);
    auto deadParts=parts;
    DamageEnemyPart(deadParts,EnemyPartType::RightArm,1000); assert(!EnemyPartsDead(deadParts));
    DamageEnemyPart(deadParts,EnemyPartType::Head,1000); assert(EnemyPartsDead(deadParts));
    deadParts=parts; DamageEnemyPart(deadParts,EnemyPartType::Body,1000); assert(EnemyPartsDead(deadParts));
    for(int fps : {30,60,144}) {
        EnemyAI chase; Vector3 pos{0,0,10}, rot{}; float dealt=0;
        for(int i=0;i<fps*5;++i)dealt+=chase.Update(pos,rot,{},1.0f/static_cast<float>(fps),false);
        assert(std::abs(pos.z-1.5f)<.0001f && dealt==20);
    }
    EnemyAI far; Vector3 zero{}, rotate{};
    assert(far.Update(zero,rotate,{},.01f,false)==10);
    assert(std::isfinite(rotate.y));
    assert(far.Update(zero,rotate,{0,0,30},.1f,false)==0 && far.state==EnemyState::Idle);
    assert(far.Update(zero,rotate,{},.1f,false)==0); // range reentry cannot reset cooldown
    EnemyAI moving;
    Vector3 movingPosition{7,0,12}, movingRotation{};
    moving.Update(movingPosition,movingRotation,{-3,0,-4},2,false);
    const auto movedWorld=Matrix4x4::MakeAffineMatrix({2,2,2},movingRotation,movingPosition);
    for(const auto& part:parts) {
        const auto center=(part.bounds.min+part.bounds.max)*.5f;
        const auto rayOrigin=EnemyPartTransformPoint({bounds.min.x-2,center.y,center.z},movedWorld);
        const auto target=EnemyPartTransformPoint(center,movedWorld);
        EnemyPartHit movedHit;
        assert(RaycastEnemyParts(parts,movedWorld,rayOrigin,target-rayOrigin,100,movedHit));
        assert(movedHit.part==part.type);
        DetachedPartMotion piece;
        piece.Initialize(part.bounds,movingPosition,movingRotation,{2,2,2},target-rayOrigin,part.type,{2,3,4},{});
        const auto spawn=piece.Translation();
        assert(std::hypot(spawn.x-movingPosition.x,spawn.y-movingPosition.y,spawn.z-movingPosition.z)<.0001f);
    }
    std::puts("Moving AI transform -> six part raycasts -> detached spawn continuity passed.");
    std::puts("AI tests passed: idle/chase/attack/dead, model facing, stop radius, cadence across 30/60/144Hz, death conditions, range reentry.");
    // Two independently ticking attackers; no per-frame damage and no shared cooldown.
    std::array<EnemyAI,2> attackers{};
    std::array<Vector3,2> attackPositions{{{0,0,1},{1,0,0}}};
    std::array<Vector3,2> attackRotations{};
    attackers[1].cooldown=.5f;
    std::array<int,2> attackCounts{};
    std::array<int,2> previousTick{{-10000,-10000}};
    for (int tick=0;tick<180;++tick) {
        for(size_t i=0;i<2;++i) {
            const auto amount=attackers[i].Update(attackPositions[i],attackRotations[i],{},1.0f/60,false);
            if(amount>0) {
                assert(tick-previousTick[i]>=59);
                previousTick[i]=tick;
                attackCounts[i]+=static_cast<int>(attackers[i].attacksThisUpdate);
                assert(amount==10);
            }
        }
    }
    assert(attackCounts[0]>=3 && attackCounts[0]<=4 && attackCounts[1]==3);
    std::array<EnemyParts,2> individuals{{parts,parts}};
    const auto armCenter=(parts[3].bounds.min+parts[3].bounds.max)*.5f;
    for (bool reverse : {false,true}) {
        float closest=100; int selected=-1; EnemyPartHit nearest;
        for(int n=0;n<2;++n) {
            const int i=reverse?1-n:n;
            EnemyPartHit candidate;
            const auto world=Matrix4x4::MakeAffineMatrix({1,1,1},{},{static_cast<float>(i*5),0,0});
            if(RaycastEnemyParts(individuals[i],world,{-3,armCenter.y,armCenter.z},{1,0,0},closest,candidate)) {
                closest=candidate.distance; selected=i; nearest=candidate;
            }
        }
        assert(selected==0 && nearest.part==EnemyPartType::RightArm);
    }
    DamageEnemyPart(individuals[0],EnemyPartType::RightArm,60);
    assert(individuals[0][3].hp==0 && individuals[1][3].hp==60);
    DamageEnemyPart(individuals[0],EnemyPartType::Head,50);
    assert(EnemyPartsDead(individuals[0]) && !EnemyPartsDead(individuals[1]));
    std::puts("Multiple enemy tests passed: separate attack cooldowns/counts, nearest target independent of iteration order, isolated HP/death.");
    // Every destroyed part stops occluding a second enemy, including after transforms.
    for (const auto& transform : transforms) for (const auto& part : parts) {
        auto front = parts;
        const auto world = Matrix4x4::MakeAffineMatrix(transform.scale,transform.rotate,transform.translate);
        const auto behind = Matrix4x4::Multiply(Matrix4x4::MakeAffineMatrix({1,1,1},{},{5,0,0}),world);
        const auto center=(part.bounds.min+part.bounds.max)*.5f;
        const auto origin=EnemyPartTransformPoint({bounds.min.x-2,center.y,center.z},world);
        const auto direction=EnemyPartTransformPoint(center,world)-origin;
        EnemyPartHit frontHit, backHit;
        assert(RaycastEnemyParts(front,world,origin,direction,100,frontHit));
        assert(RaycastEnemyParts(parts,behind,origin,direction,100,backHit));
        assert(frontHit.part==part.type && frontHit.distance<backHit.distance);
        DamageEnemyPart(front,part.type,1000);
        assert(!RaycastEnemyParts(front,world,origin,direction,100,frontHit));
        assert(!frontHit.hit && frontHit.part==EnemyPartType::None);
        assert(RaycastEnemyParts(parts,behind,origin,direction,100,backHit) && backHit.part==part.type);
        for (const auto& remaining : front) if (remaining.type!=part.type) {
            const auto c=(remaining.bounds.min+remaining.bounds.max)*.5f;
            const auto o=EnemyPartTransformPoint({bounds.min.x-2,c.y,c.z},world);
            assert(RaycastEnemyParts(front,world,o,EnemyPartTransformPoint(c,world)-o,100,frontHit));
            assert(frontHit.part==remaining.type);
        }
    }
    std::puts("Destroyed-part pass-through tests passed: six parts, transformed front/back enemies, intact parts still block.");
    EnemyParts stacked{};
    for(auto& p:stacked)p={EnemyPartType::Head,{{5,0,0},{6,1,1}}};
    stacked[5]={EnemyPartType::Body,{{2,0,0},{3,1,1}}};
    EnemyPartHit hit;
    const auto identity=Matrix4x4::MakeIdentity4x4();
    assert(RaycastEnemyParts(stacked,identity,{0,.5f,.5f},{1,0,0},100,hit));
    assert(hit.part==EnemyPartType::Body && hit.distance==2);
    std::reverse(stacked.begin(),stacked.end());
    assert(RaycastEnemyParts(stacked,identity,{0,.5f,.5f},{1,0,0},100,hit) && hit.part==EnemyPartType::Body);
    assert(!RaycastEnemyParts(parts,identity,{0,10,10},{1,0,0},100,hit));
    assert(!RaycastEnemyParts(parts,identity,{},{},100,hit));
    const auto singular=Matrix4x4::MakeAffineMatrix({0,1,1},{},{});
    assert(!RaycastEnemyParts(parts,singular,{},{1,0,0},100,hit));
    std::puts("Enemy part tests passed: all six parts, translation, rotation, nonuniform/mirrored scale, world distance, range, nearest-order independence, misses, singular transform.");
}
