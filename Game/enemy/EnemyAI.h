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
struct EnemyAISettings {
    float detectionRange = 20;
    float attackRange = 1.5f;
    float moveSpeed = 2.5f;
    float attackDamage = 10;
    float attackInterval = 1;
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
