#pragma once
#include "EnemyParts.h"

enum class EnemyState { Idle, Chase, Attack, Dead };
inline const char* EnemyStateName(EnemyState state) {
    switch (state) {
    case EnemyState::Idle: return "Idle";
    case EnemyState::Chase: return "Chase";
    case EnemyState::Attack: return "Attack";
    default: return "Dead";
    }
}
inline bool EnemyPartsDead(const EnemyParts& parts) {
    for (const auto& part : parts)
        if ((part.type == EnemyPartType::Head || part.type == EnemyPartType::Body) &&
            part.DamageState() == EnemyPartDamageState::Destroyed) return true;
    return false;
}
// Symmetric correction per enemy; radius sum defines minimum XZ separation.
inline Vector3 EnemySeparationOffset(const Vector3& a,float radiusA,const Vector3& b,float radiusB,float dt) {
    const Vector3 delta{a.x-b.x,0,a.z-b.z};
    const float distance=std::hypot(delta.x,delta.z);
    const float minimum=radiusA+radiusB;
    if (distance>=minimum || !std::isfinite(dt) || dt<=0) return {};
    const auto direction=distance>1e-5f ? delta*(1/distance) : Vector3{1,0,0};
    return direction*std::min((minimum-distance)*.5f,dt*.6f);
}
struct EnemyAISettings {
    float detectionRange = 20;
    float attackRange = 1.5f;
    float moveSpeed = 2.5f;
    float attackDamage = 10;
    float attackInterval = 1;
    bool ranged = false;
    float minRange = 7, maxRange = 16;
};
struct EnemyAI {
    EnemyState state = EnemyState::Idle;
    EnemyAISettings settings{};
    float distance = 0;
    float cooldown = 0;
    unsigned int attacksThisUpdate = 0;
    // Returns pending damage. Scene applies it after shooting, so lethal shots win this frame.
    float Update(Vector3& position, Vector3& rotation, const Vector3& target, float dt, bool dead) {
        attacksThisUpdate = 0;
        const Vector3 delta{target.x-position.x,0,target.z-position.z};
        distance = std::hypot(delta.x,delta.z);
        if (dead) { state=EnemyState::Dead; return 0; }
        dt = std::isfinite(dt) ? std::max(dt,0.0f) : 0;
        const float attackRange=std::max(settings.attackRange,0.0f);
        if (distance > std::max(settings.detectionRange,attackRange)) {
            state=EnemyState::Idle; cooldown=std::max(0.0f,cooldown-dt); return 0;
        }
        float reachTime=0;
        if (distance > 1e-6f) {
            // Boss glTF front +X becomes engine -X during import.
            rotation={0,std::atan2(delta.x,delta.z)+1.57079632679f,0};
        }
        if (settings.ranged) {
            const float minimum=std::max(0.0f,settings.minRange);
            const float maximum=std::max(minimum,settings.maxRange);
            if (distance<minimum || distance>maximum) {
                const float desired=distance<minimum ? minimum : maximum;
                const float travel=std::min(std::max(settings.moveSpeed,0.0f)*dt,std::abs(distance-desired));
                const Vector3 direction=distance>1e-6f ? delta*(1.0f/distance) : Vector3{0,0,1};
                position+=direction*(distance<minimum ? -travel : travel);
                distance=std::hypot(target.x-position.x,target.z-position.z);
                state=EnemyState::Chase; cooldown=std::max(0.0f,cooldown-dt); return 0;
            }
            state=EnemyState::Attack;
            cooldown=std::max(0.0f,cooldown-dt);
            if (dt>0 && cooldown<=0 && distance<=attackRange) {
                attacksThisUpdate=1; cooldown=std::max(.05f,settings.attackInterval);
            }
            return 0; // Scene emits a projectile after resolving player shots.
        }
        if (distance > attackRange) {
            const float speed=std::max(settings.moveSpeed,0.0f);
            const float travel=std::min(speed*dt,distance-attackRange);
            position=position+delta*(travel/distance);
            reachTime = speed > 0 ? (distance-attackRange)/speed : std::numeric_limits<float>::infinity();
            distance-=travel;
            if (reachTime > dt) {
                state=EnemyState::Chase; cooldown=std::max(0.0f,cooldown-dt); return 0;
            }
        }
        state=EnemyState::Attack;
        const float first=std::max(reachTime,cooldown);
        if (dt<=0 || first>dt) { cooldown=std::max(0.0f,cooldown-dt); return 0; }
        const float interval=std::max(.05f,settings.attackInterval);
        const float remaining=dt-first;
        const float attacks=1+std::floor((remaining+1e-6f)/interval);
        cooldown=std::max(0.0f,first+attacks*interval-dt);
        attacksThisUpdate = static_cast<unsigned int>(attacks);
        return attacks*std::max(0.0f,settings.attackDamage);
    }
};
