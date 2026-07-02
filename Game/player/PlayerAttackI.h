#pragma once

#include <cstdint>

struct Vector3;
class Player;

enum class PlayerIAttackState : uint8_t {
    None,
    SideSlide_Windup,
    SideSlide_Move,
    SideSlide_Recover,
    UpRise_Windup,
    UpRise_Move,
    UpRise_Recover,
    DownCounter_Active,
    DownCounter_Success,
    DownCounter_Recover,
    NeutralFinish_Active,
    NeutralFinish_Recover,
};

enum class PlayerIAttackType : uint8_t {
    None,
    NeutralSpecial,
    SideSpecial,
    UpSpecial,
	DownSpecial,
};

class PlayerIAttack {
public:
    static void StartNeutralSpecial(Player& player);
    static void StartSideSpecial(Player& player);
    static void StartUpSpecial(Player& player);
    static void StartDownSpecial(Player& player);

    static void UpdateNeutralSpecial(Player& player, float dt);
    static void UpdateSideSpecial(Player& player, float dt);
    static void UpdateUpSpecial(Player& player, float dt);
    static void UpdateDownSpecial(Player& player, float dt);

    static bool GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);
    static bool GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize);

    static int GetNeutralSpecialDamage(const Player& player);
    static int GetSideSpecialDamage(const Player& player);
    static int GetUpSpecialDamage(const Player& player);
    static int GetDownSpecialDamage(const Player& player);

    static void ChangeState(Player& player, PlayerIAttackState state);
};
