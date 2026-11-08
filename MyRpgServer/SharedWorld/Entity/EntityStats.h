#pragma once
#include <algorithm>
#include <cstdint>

namespace shared_world
{
// Integer percentages. These are base values; hit resolution/equipment are separate systems.
struct EntityStats
{
    static constexpr std::uint32_t LIMIT = 1'000'000;
    static constexpr std::uint32_t ATTACKABLE = 1, MORTAL = 2;
    std::uint32_t hp = 0, maxHp = 0, mp = 0, maxMp = 0, attack = 0, defense = 0, evasion = 0, accuracy = 0, flags = 0;

    static EntityStats PlayerDefaults()
    {
        return {5000, 5000, 50, 50, 10, 0, 0, 50, ATTACKABLE | MORTAL};
    }

    bool Valid() const
    {
        if (this->hp > this->maxHp || this->mp > this->maxMp || this->maxHp > LIMIT || this->maxMp > LIMIT ||
            this->attack > LIMIT || this->defense > LIMIT || this->evasion > 100 || this->accuracy > 100)
        {
            return false;
        }

        if (this->flags == 0)
        {
            return this->hp == 0 && this->maxHp == 0 && this->mp == 0 && this->maxMp == 0 && this->attack == 0 &&
                   this->defense == 0 && this->evasion == 0 && this->accuracy == 0;
        }

        return this->flags == (ATTACKABLE | MORTAL) && this->maxHp > 0;
    }

    bool CanBeAttacked() const
    {
        return (this->flags & ATTACKABLE) != 0 && this->hp > 0;
    }

    bool CanDie() const
    {
        return (this->flags & MORTAL) != 0;
    }

    bool IsDead() const
    {
        return CanDie() && this->hp == 0;
    }

    // Called only by the map owner after combat validation. No client-supplied damage.
    std::uint32_t ApplyDamage(std::uint32_t amount)
    {
        if (!CanBeAttacked())
        {
            return 0;
        }

        const auto APPLIED = std::min(this->hp, amount);
        this->hp -= APPLIED;
        return APPLIED;
    }

    bool SpendMp(std::uint32_t amount)
    {
        if (!CanBeAttacked() || amount > this->mp)
        {
            return false;
        }

        this->mp -= amount;
        return true;
    }

    std::uint32_t RestoreHp(std::uint32_t amount)
    {
        if (!CanBeAttacked())
        {
            return 0;
        }

        const auto RESTORED = std::min(amount, this->maxHp - this->hp);
        this->hp += RESTORED;
        return RESTORED;
    }

    std::uint32_t RestoreMp(std::uint32_t amount)
    {
        if (!CanBeAttacked())
        {
            return 0;
        }

        const auto RESTORED = std::min(amount, this->maxMp - this->mp);
        this->mp += RESTORED;
        return RESTORED;
    }

    bool operator==(const EntityStats& other) const
    {
        return this->hp == other.hp && this->maxHp == other.maxHp && this->mp == other.mp &&
               this->maxMp == other.maxMp && this->attack == other.attack && this->defense == other.defense &&
               this->evasion == other.evasion && this->accuracy == other.accuracy && this->flags == other.flags;
    }

    bool operator!=(const EntityStats& other) const
    {
        return !(*this == other);
    }

};

}

