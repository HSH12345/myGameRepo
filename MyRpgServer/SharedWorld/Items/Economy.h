#pragma once
#include "../Npc/NpcCatalog.h"
#include "../Combat/Skills.h"
#include <array>
#include <algorithm>
#include <limits>
#include <map>

namespace shared_world::economy
{
inline constexpr std::uint32_t BAG_CAPACITY = 32;
inline constexpr std::uint64_t GOLD_LIMIT = 1'000'000'000'000ULL, INITIAL_GOLD = 1000;
inline constexpr std::uint8_t NO_SLOT = 255;
enum class Slot : std::uint8_t
{
    MAIN_HAND = 0,
    OUTFIT = 1
};

enum class Action : std::uint32_t
{
    LOAD = 1,
    ACQUIRE = 2,
    BUY = 3,
    MOVE = 4,
    SPLIT = 5,
    EQUIP = 6,
    UNEQUIP = 7,
    QUERY = 8,
    HEALTH = 9,
    USE = 10,
    TRAIN_SKILL = 11,
    BIND_SKILL = 12,
    REGISTER_POTION = 13
};

enum class Result : std::uint32_t
{
    SUCCESS = 0,
    BUSY = 1,
    UNAVAILABLE = 2,
    UNKNOWN_OUTCOME = 3,
    NOT_OWNED = 4,
    STALE_REVISION = 5,
    STALE_SESSION = 6,
    INVALID_ITEM = 7,
    INSUFFICIENT_GOLD = 8,
    INVENTORY_FULL = 9,
    NOT_EQUIPPABLE = 10,
    EQUIPPED_ITEM = 11,
    OPERATION_CONFLICT = 12,
    NOT_APPLIED = 13,
    EXPIRED = 14,
    TOO_FAR = 15,
    INVALID_SHOP = 16,
    NOT_USABLE = 17,
    FULL_HEALTH = 18,
    COOLDOWN = 19,
    DEAD = 20
};

struct Consumable
{
    std::uint32_t item = 0, heal = 0, cooldownMs = 0, mana = 0;
};

struct Template
{
    std::uint32_t id = 0, appearance = 0, stack = 1, attack = 0, defense = 0;
    std::uint8_t slot = NO_SLOT;
    std::string name;
};

struct Offer
{
    std::uint32_t id = 0, shop = 0, item = 0, limit = 1;
    std::uint64_t price = 0;
    bool enabled = false;
};

struct Catalog
{
    skills::Catalog skills;
    std::vector<Template> items;
    std::vector<Offer> offers;
    std::map<std::uint32_t, Consumable> consumables;
    std::uint64_t revision = 0;

    const Template* Find(std::uint32_t id) const
    {
        auto it = std::find_if(this->items.begin(), this->items.end(),
                               [id](const auto& v)
                               {
                                   return v.id == id;
                               });

        return it == this->items.end() ? nullptr : &*it;
    }

    const Offer* FindOffer(std::uint32_t shop, std::uint32_t id) const
    {
        auto it = std::find_if(this->offers.begin(), this->offers.end(),
                               [=](const auto& v)
                               {
                                   return v.shop == shop && v.id == id;
                               });

        return it == this->offers.end() ? nullptr : &*it;
    }

    bool HasShop(std::uint32_t shop) const
    {
        return std::any_of(this->offers.begin(), this->offers.end(),
                           [=](const auto& v)
                           {
                               return v.shop == shop;
                           });

    }

    static Catalog Load(const std::filesystem::path& root)
    {
        Catalog c;
        npc::Catalog reader;
        auto number = [](const std::string& s, int lo, int hi)
        {
            return static_cast<std::uint32_t>(npc::Catalog::Number(s, lo, hi));
        };

        for (const auto& r : reader.Read(root / "ItemTemplates.tsv",
                                         "templateId\tname\tstackLimit\tslot\tappearanceId\tattack\tdefense"))
        {
            if (r.size() != 7 || r[1].empty() || r[1].size() > 64 || c.items.size() >= 256)
            {
                throw std::invalid_argument("item schema/budget");
            }

            Template t;
            t.id = number(r[0], 1, INT32_MAX);
            t.name = r[1];
            t.stack = number(r[2], 1, 999);
            t.slot = static_cast<std::uint8_t>(number(r[3], 0, 255));
            t.appearance = number(r[4], 0, INT32_MAX);
            t.attack = number(r[5], 0, 100000);
            t.defense = number(r[6], 0, 100000);
            if (c.Find(t.id) || (t.slot != NO_SLOT && (t.slot > 1 || t.stack != 1 || !t.appearance)) ||
                (t.slot == NO_SLOT && (t.appearance || t.attack || t.defense)))
            {
                throw std::invalid_argument("invalid item contract");
            }

            c.items.push_back(t);
        }

        for (const auto& r :
             reader.Read(root / "ShopOffers.tsv", "shopId\tofferId\ttemplateId\tunitPrice\tquantityLimit\tenabled"))
        {
            if (r.size() != 6 || c.offers.size() >= 256)
            {
                throw std::invalid_argument("offer schema/budget");
            }

            Offer o;
            o.shop = number(r[0], 1, INT32_MAX);
            o.id = number(r[1], 1, INT32_MAX);
            o.item = number(r[2], 1, INT32_MAX);
            o.price = number(r[3], 1, INT32_MAX);
            o.limit = number(r[4], 1, 999);
            o.enabled = number(r[5], 0, 1) != 0;
            if (!c.Find(o.item) || c.FindOffer(o.shop, o.id))
            {
                throw std::invalid_argument("invalid offer reference");
            }

            c.offers.push_back(o);
            if (std::count_if(c.offers.begin(), c.offers.end(),
                              [&](const auto& value)
                              {
                                  return value.shop == o.shop;
                              }) > 8)

            {
                throw std::invalid_argument("shop offer packet budget");
            }

        }

        for (const auto& r : reader.Read(root / "Consumables.tsv", "templateId\thealHp\thealMp\tcooldownMs"))
        {
            if (r.size() != 4)
            {
                throw std::invalid_argument("consumable schema");
            }

            Consumable v{number(r[0], 1, INT32_MAX), number(r[1], 0, EntityStats::LIMIT), number(r[3], 1000, 60000),
                         number(r[2], 0, EntityStats::LIMIT)};
            const auto* item = c.Find(v.item);
            if (!item || item->slot != NO_SLOT || (!v.heal == !v.mana) || !c.consumables.emplace(v.item, v).second)
            {
                throw std::invalid_argument("consumable reference");
            }

        }

        if (c.items.empty() || c.offers.empty())
        {
            throw std::invalid_argument("empty economy catalog");
        }

        c.skills = skills::Catalog::Load(root.parent_path() / "Combat");
        c.revision = reader.checksum ^ c.skills.checksum;
        return c;
    }

};

struct Item
{
    std::uint64_t id = 0;
    std::uint32_t type = 0, quantity = 0;
    std::uint8_t bag = 0, equipped = NO_SLOT;
};

struct State
{
    std::uint64_t gold = INITIAL_GOLD, revision = 1, fence = 0, nextId = 1;
    skills::State skills;
    std::array<std::uint32_t, 10> quickSlots{3, 4};
    std::array<std::uint32_t, 2> potions{};
    std::vector<Item> items;

    bool Valid(const Catalog& c) const
    {
        if (this->gold > GOLD_LIMIT || !this->revision || !this->nextId || this->items.size() > BAG_CAPACITY)
        {
            return false;
        }

        for (auto skill : this->quickSlots)
        {
            if (skill && (!c.skills.definitions.count(skill) ||
                          (!this->skills.ranks[skill] && !c.skills.definitions.at(skill).initialRank)))
            {
                return false;
            }

        }

        for (unsigned i = 0; i < this->potions.size(); ++i)
        {
            if (this->potions[i] &&
                (!c.consumables.count(this->potions[i]) || bool(c.consumables.at(this->potions[i]).mana) != (i == 1)))
            {
                return false;
            }

        }

        std::array<bool, BAG_CAPACITY> bags{};
        std::array<bool, 2> equipped{};
        for (std::size_t i = 0; i < this->items.size(); ++i)
        {
            const auto& v = this->items[i];
            const auto* t = c.Find(v.type);
            if (!t || !v.id || v.id >= this->nextId || !v.quantity || v.quantity > t->stack || v.bag >= BAG_CAPACITY ||
                bags[v.bag])
            {
                return false;
            }

            bags[v.bag] = true;
            for (std::size_t j = 0; j < i; ++j)
            {
                if (this->items[j].id == v.id)
                {
                    return false;
                }

            }

            if (v.equipped != NO_SLOT)
            {
                if (v.equipped > 1 || v.equipped != t->slot || equipped[v.equipped] || v.quantity != 1)
                {
                    return false;
                }

                equipped[v.equipped] = true;
            }

        }

        return true;
    }

    EntityStats Stats(const Catalog& c, EntityStats base = EntityStats::PlayerDefaults()) const
    {
        for (const auto& i : this->items)
        {
            if (i.equipped != NO_SLOT)
            {
                if (const auto* t = c.Find(i.type))
                {
                    base.attack = std::min(EntityStats::LIMIT, base.attack + t->attack);
                    base.defense = std::min(EntityStats::LIMIT, base.defense + t->defense);
                }

            }

        }

        return base;
    }

};

struct Request
{
    Action action = Action::LOAD;
    std::uint64_t revision = 0, fence = 0, catalog = 0, instance = 0;
    std::uint32_t shop = 0, offer = 0, quantity = 0, destination = 0;
    std::string operation;

    bool Mutation() const
    {
        return (this->action >= Action::BUY && this->action <= Action::UNEQUIP) ||
               (this->action >= Action::USE && this->action <= Action::REGISTER_POTION);
    }

    bool Valid() const
    {
        if (this->action < Action::LOAD || this->action > Action::REGISTER_POTION || this->destination >= BAG_CAPACITY)
        {
            return false;
        }

        if (this->action == Action::BIND_SKILL &&
            (this->instance > 64 || this->destination >= 10 || this->quantity || this->shop || this->offer))
        {
            return false;
        }

        if (this->action == Action::REGISTER_POTION &&
            (this->instance > INT32_MAX || this->destination >= 2 || this->quantity || this->shop || this->offer))
        {
            return false;
        }

        if (this->action == Action::TRAIN_SKILL &&
            (this->instance < 3 || this->instance > 64 || this->quantity < 1 || this->quantity > 5 || this->shop ||
             this->offer || this->destination))
        {
            return false;
        }

        if (this->action == Action::USE &&
            (!this->instance || this->quantity != 1 || this->shop || this->offer || this->destination))
        {
            return false;
        }

        if (Mutation() || this->action == Action::ACQUIRE || this->action == Action::QUERY)
        {
            if (this->operation.size() != 32)
            {
                return false;
            }

            for (char ch : this->operation)
            {
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                {
                    return false;
                }

            }

        }

        return true;
    }

};

// Work on a copy. Callers publish only after the durable transaction commits.
inline Result Apply(State& state, const Catalog& c, const Request& r, std::uint32_t race = 0, std::uint32_t level = 1)
{
    if (!r.Valid() || !r.Mutation() || !state.Valid(c))
    {
        return Result::INVALID_ITEM;
    }

    if (r.fence != state.fence)
    {
        return Result::STALE_SESSION;
    }

    if (r.revision != state.revision)
    {
        return Result::STALE_REVISION;
    }

    if (r.catalog != c.revision)
    {
        return Result::INVALID_SHOP;
    }

    if (state.revision == UINT64_MAX)
    {
        return Result::UNAVAILABLE;
    }

    State next = state;
    auto item = [&](std::uint64_t id)
    {
        return std::find_if(next.items.begin(), next.items.end(),
                            [=](const auto& v)
                            {
                                return v.id == id;
                            });

    };

    auto bag = [&](std::uint32_t slot)
    {
        return std::find_if(next.items.begin(), next.items.end(),
                            [=](const auto& v)
                            {
                                return v.bag == slot;
                            });

    };

    if (r.action == Action::BIND_SKILL)
    {
        if (r.instance &&
            (!c.skills.definitions.count(static_cast<std::uint32_t>(r.instance)) || !next.skills.ranks[r.instance]))
        {
            return Result::NOT_USABLE;
        }

        next.quickSlots[r.destination] = static_cast<std::uint32_t>(r.instance);
    }
    else if (r.action == Action::REGISTER_POTION)
    {
        const auto TYPE = static_cast<std::uint32_t>(r.instance);
        if (TYPE)
        {
            const auto IT = c.consumables.find(TYPE);
            if (IT == c.consumables.end() || bool(IT->second.mana) != (r.destination == 1))
            {
                return Result::NOT_USABLE;
            }

            if (std::none_of(next.items.begin(), next.items.end(),
                             [TYPE](const auto& v)
                             {
                                 return v.type == TYPE && v.quantity;
                             }))

            {
                return Result::NOT_OWNED;
            }

        }

        next.potions[r.destination] = TYPE;
    }
    else if (r.action == Action::TRAIN_SKILL)
    {
        if (next.skills.level != level ||
            !c.skills.Train(next.skills, static_cast<std::uint32_t>(r.instance), r.quantity))
        {
            return Result::NOT_USABLE;
        }

    }
    else if (r.action == Action::BUY)
    {
        const auto* o = c.FindOffer(r.shop, r.offer);
        if (!o || !o->enabled || !r.quantity || r.quantity > o->limit)
        {
            return Result::INVALID_SHOP;
        }

        const auto* t = c.Find(o->item);
        if (!t || (t->slot != NO_SLOT && (race != 0 || !level)))
        {
            return Result::NOT_EQUIPPABLE;
        }

        if (o->price > GOLD_LIMIT / r.quantity)
        {
            return Result::INVALID_SHOP;
        }

        const auto COST = o->price * r.quantity;
        if (next.gold < COST)
        {
            return Result::INSUFFICIENT_GOLD;
        }

        auto remaining = r.quantity;
        for (auto& v : next.items)
        {
            if (v.type == t->id && v.equipped == NO_SLOT)
            {
                const auto ADD = std::min(t->stack - v.quantity, remaining);
                v.quantity += ADD;
                remaining -= ADD;
            }

        }

        for (std::uint32_t slot = 0; remaining && slot < BAG_CAPACITY; ++slot)
        {
            if (bag(slot) == next.items.end())
            {
                if (next.nextId == UINT64_MAX)
                {
                    return Result::UNAVAILABLE;
                }

                const auto ADD = std::min(t->stack, remaining);
                next.items.push_back({next.nextId++, t->id, ADD, static_cast<std::uint8_t>(slot), NO_SLOT});
                remaining -= ADD;
            }

        }

        if (remaining)
        {
            return Result::INVENTORY_FULL;
        }

        next.gold -= COST;
    }
    else
    {
        auto from = item(r.instance);
        if (from == next.items.end())
        {
            return Result::NOT_OWNED;
        }

        const auto* t = c.Find(from->type);
        if (r.action == Action::USE)
        {
            if (!c.consumables.count(from->type))
            {
                return Result::NOT_USABLE;
            }

            if (from->equipped != NO_SLOT)
            {
                return Result::EQUIPPED_ITEM;
            }

            if (!--from->quantity)
            {
                next.items.erase(from);
            }

        }
        else if (r.action == Action::EQUIP)
        {
            if (!t || t->slot == NO_SLOT || race != 0 || !level || from->quantity != 1)
            {
                return Result::NOT_EQUIPPABLE;
            }

            for (auto& v : next.items)
            {
                if (v.equipped == t->slot)
                {
                    v.equipped = NO_SLOT;
                }

            }

            from->equipped = t->slot;
        }
        else if (r.action == Action::UNEQUIP)
        {
            from->equipped = NO_SLOT;
        }
        else
        {
            if (from->equipped != NO_SLOT)
            {
                return Result::EQUIPPED_ITEM;
            }

            auto to = bag(r.destination);
            if (to != next.items.end() && to->equipped != NO_SLOT)
            {
                return Result::EQUIPPED_ITEM;
            }

            if (to == from)
            {
                return Result::INVALID_ITEM;
            }

            if (r.action == Action::SPLIT)
            {
                if (!r.quantity || r.quantity >= from->quantity || to != next.items.end())
                {
                    return Result::INVALID_ITEM;
                }

                if (next.nextId == UINT64_MAX)
                {
                    return Result::UNAVAILABLE;
                }

                from->quantity -= r.quantity;
                next.items.push_back(
                    {next.nextId++, from->type, r.quantity, static_cast<std::uint8_t>(r.destination), NO_SLOT});
            }
            else if (to == next.items.end())
            {
                from->bag = static_cast<std::uint8_t>(r.destination);
            }
            else if (to->type == from->type && t->stack > 1)
            {
                const auto ADD = std::min(from->quantity, t->stack - to->quantity);
                if (!ADD)
                {
                    return Result::INVENTORY_FULL;
                }

                to->quantity += ADD;
                from->quantity -= ADD;
                if (!from->quantity)
                {
                    next.items.erase(from);
                }

            }
            else
            {
                std::swap(from->bag, to->bag);
            }

        }

    }

    ++next.revision;
    if (!next.Valid(c))
    {
        return Result::INVALID_ITEM;
    }

    state = std::move(next);
    return Result::SUCCESS;
}

}

