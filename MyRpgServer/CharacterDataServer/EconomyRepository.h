#pragma once
#include "../DatabaseCore/MySql.h"
#include "../SharedWorld/Items/EconomyCodec.h"
#include "../ServerCore/StructuredLogger.h"

// All calls run on CharacterService DB workers. The character row serializes every mutation.
class EconomyRepository final
{
    using State = shared_world::economy::State;
    using Result = shared_world::economy::Result;
    using Action = shared_world::economy::Action;

public:
    static EconomyRepository& Get()
    {
        static EconomyRepository r;
        return r;
    }

    bool Configure(const std::string& root)
    {
        try
        {
            this->catalog =
                std::make_shared<const shared_world::economy::Catalog>(shared_world::economy::Catalog::Load(root));
            MySql::Connection c;
            if (MySql::Scalar(c, "SELECT COUNT(*) FROM schema_migrations WHERE version=3 AND "
                                 "checksum=SHA2('character_economy_v3',256)") != 1)
            {
                return false;
            }

            if (MySql::Scalar(c, "SELECT COUNT(*) FROM schema_migrations WHERE version=4 AND "
                                 "checksum=SHA2('character_skills_v4',256)") != 1)
            {
                return false;
            }

            CheckActionBar(c);
            MySql::Statement skills(c, "SELECT character_id,spent_points FROM character_skill_progression LIMIT 0");
            MySql::Statement ranks(c, "SELECT character_id,ability_id,skill_rank FROM character_skill_ranks LIMIT 0");
            MySql::Statement a(c, "SELECT character_id,gold,economy_revision,session_fence,next_instance_id FROM "
                                  "character_economy LIMIT 0");
            MySql::Statement b(
                c, "SELECT character_id,instance_id,template_id,quantity,bag_slot FROM character_items LIMIT 0");
            MySql::Statement d(c,
                               "SELECT character_id,equipment_slot,instance_id FROM character_equipped_items LIMIT 0");
            MySql::Statement e(
                c, "SELECT character_id,operation_id,request_hash,action,session_fence,result,result_revision FROM "
                   "character_economy_operations LIMIT 0");
            return true;
        }

        catch (...)
        {
            this->catalog.reset();
            return false;
        }

    }

    bool Enabled() const
    {
        return bool(this->catalog);
    }

    void Initialize(MySql::Connection& c, std::uint64_t id)
    {
        if (!this->catalog)
        {
            return;
        }

        const auto CHARACTER = std::to_string(id);
        MySql::Statement create(
            c, "INSERT INTO character_economy(character_id,gold,economy_revision,session_fence,next_instance_id) "
               "VALUES(?,1000,1,0,1) ON DUPLICATE KEY UPDATE character_id=character_id");
        create.Execute({CHARACTER});
        if (mysql_stmt_affected_rows(create.Get()) != 1)
        {
            return;
        }

        State s;
        s.skills = this->catalog->skills.Initial(static_cast<std::uint32_t>(
            MySql::Scalar(c, "SELECT level FROM characters WHERE character_id=?", {CHARACTER})));
        MySql::Statement legacy(c, "SELECT equipment_slot,item_template_id,appearance_id FROM character_equipment "
                                   "WHERE character_id=? ORDER BY equipment_slot");
        legacy.Execute({CHARACTER});
        std::array<unsigned long long, 3> values{};
        std::array<MYSQL_BIND, 3> binding{};
        Bind(binding, values);
        legacy.BindResult(binding.data());
        while (legacy.Fetch())
        {
            const auto* t = this->catalog->Find(static_cast<std::uint32_t>(values[1]));
            if (!t || values[0] != t->slot || values[2] != t->appearance || s.items.size() >= 32)
            {
                throw MySql::Error(1644);
            }

            s.items.push_back({s.nextId++, t->id, 1, static_cast<std::uint8_t>(s.items.size()), t->slot});
        }

        Save(c, id, s);
    }

    shared_world::economy::RpcResponse Execute(const shared_world::economy::RpcRequest& q)
    {
        using namespace shared_world::economy;
        RpcResponse reply;
        reply.requestId = q.requestId;
        reply.operation = q.request.operation;
        if (!this->catalog)
        {
            return reply;
        }

        try
        {
            MySql::Connection c;
            if (q.request.action == Action::HEALTH)
            {
                if (MySql::Scalar(c, "SELECT COUNT(*) FROM schema_migrations WHERE version=3 AND "
                                     "checksum=SHA2('character_economy_v3',256)") != 1)
                {
                    return reply;
                }

                if (MySql::Scalar(c, "SELECT COUNT(*) FROM schema_migrations WHERE version=4 AND "
                                     "checksum=SHA2('character_skills_v4',256)") != 1)
                {
                    return reply;
                }

                CheckActionBar(c);
                MySql::Statement skills(c, "SELECT character_id,spent_points FROM character_skill_progression LIMIT 0");
                MySql::Statement ranks(c,
                                       "SELECT character_id,ability_id,skill_rank FROM character_skill_ranks LIMIT 0");
                MySql::Statement a(c, "SELECT character_id,gold,economy_revision,session_fence,next_instance_id FROM "
                                      "character_economy LIMIT 0");
                MySql::Statement b(
                    c, "SELECT character_id,instance_id,template_id,quantity,bag_slot FROM character_items LIMIT 0");
                MySql::Statement d(
                    c, "SELECT character_id,equipment_slot,instance_id FROM character_equipped_items LIMIT 0");
                MySql::Statement e(
                    c, "SELECT character_id,operation_id,request_hash,action,session_fence,result,result_revision FROM "
                       "character_economy_operations LIMIT 0");
                reply.result = Result::SUCCESS;
                return reply;
            }

            MySql::Transaction tx(c);
            const auto ID = std::to_string(q.character);
            try
            {
                (void)MySql::Scalar(c,
                                    "SELECT character_id FROM characters WHERE character_id=? AND account_uid=? AND "
                                    "deleted_at IS NULL FOR UPDATE",
                                    {ID, q.uid});
            }

            catch (const MySql::Error& e)
            {
                if (e.code != 1329)
                {
                    throw;
                }

                reply.result = Result::NOT_OWNED;
                return reply;
            }

            Initialize(c, q.character);
            InitializeSkills(c, q.character);
            MySql::Execute(c, "INSERT IGNORE INTO character_action_bar(character_id) VALUES(?)", {ID});
            reply.state = Load(c, q.character);
            const auto& r = q.request;
            if ((r.action == Action::LOAD || r.action == Action::ACQUIRE) && r.catalog &&
                r.catalog != this->catalog->revision)
            {
                reply.result = Result::INVALID_SHOP;
                return reply;
            }

            shared_world::Bytes fingerprintBytes;
            auto business = r;
            business.fence = 0;
            WriteRequest(fingerprintBytes, business);
            const std::string FINGERPRINT(fingerprintBytes.begin(), fingerprintBytes.end());
            if (r.action != Action::LOAD)
            {
                MySql::Statement op(c, "SELECT request_hash=UNHEX(SHA2(?,256)),result,result_revision,session_fence "
                                       "FROM character_economy_operations WHERE character_id=? AND operation_id=?");
                op.Execute({FINGERPRINT, ID, r.operation});
                std::array<unsigned long long, 4> values{};
                std::array<MYSQL_BIND, 4> binding{};
                Bind(binding, values);
                op.BindResult(binding.data());
                if (op.Fetch())
                {
                    reply.result = r.action == Action::QUERY || values[0] ? static_cast<Result>(values[1])
                                                                          : Result::OPERATION_CONFLICT;
                    reply.committedRevision = values[2];
                    if (r.action == Action::ACQUIRE && reply.result == Result::SUCCESS &&
                        values[3] != reply.state.fence)
                    {
                        reply.result = Result::STALE_SESSION;
                    }

                    return reply; // No writes; latest state accompanies the original business result.
                }

            }

            if (r.action == Action::LOAD)
            {
                reply.result = Result::SUCCESS;
            }
            else if (r.action == Action::QUERY)
            {
                reply.result = r.fence < reply.state.fence ? Result::NOT_APPLIED : Result::UNKNOWN_OUTCOME;
            }
            else
            {
                if (r.action == Action::ACQUIRE)
                {
                    reply.result = r.fence == reply.state.fence && reply.state.fence != UINT64_MAX
                                       ? Result::SUCCESS
                                       : Result::STALE_SESSION;
                    if (reply.result == Result::SUCCESS)
                    {
                        ++reply.state.fence;
                    }

                }
                else
                {
                    const auto RACE = MySql::Scalar(c, "SELECT race FROM characters WHERE character_id=?", {ID});
                    const auto LEVEL = MySql::Scalar(c, "SELECT level FROM characters WHERE character_id=?", {ID});
                    reply.result = Apply(reply.state, *this->catalog, r, static_cast<std::uint32_t>(RACE),
                                         static_cast<std::uint32_t>(LEVEL));
                }

                if (reply.result == Result::SUCCESS)
                {
                    if (r.action == Action::BIND_SKILL || r.action == Action::REGISTER_POTION)
                    {
                        SaveActionBar(c, q.character, reply.state);
                        MySql::Execute(c, "UPDATE character_economy SET economy_revision=? WHERE character_id=?",
                                       {std::to_string(reply.state.revision), ID});
                    }
                    else
                    {
                        Save(c, q.character, reply.state);
                    }

                }

                reply.committedRevision = reply.state.revision;
                MySql::Execute(c,
                               "INSERT INTO "
                               "character_economy_operations(character_id,operation_id,request_hash,action,session_"
                               "fence,result,result_revision) VALUES(?,?,UNHEX(SHA2(?,256)),?,?,?,?)",
                               {ID, r.operation, FINGERPRINT, std::to_string(static_cast<unsigned>(r.action)),
                                std::to_string(reply.state.fence), std::to_string(static_cast<unsigned>(reply.result)),
                                std::to_string(reply.committedRevision)});
            }

            if (!tx.Commit())
            {
                reply.result = Result::UNKNOWN_OUTCOME;
            }

        }

        catch (const MySql::Error& e)
        {
            RPG_LOG_ERROR("Economy", "Transaction failed. code=" << e.code);
            reply.result = q.request.Mutation() || q.request.action == Action::ACQUIRE ? Result::UNKNOWN_OUTCOME
                                                                                       : Result::UNAVAILABLE;
        }

        catch (...)
        {
            reply.result = Result::UNKNOWN_OUTCOME;
        }

        return reply;
    }

private:
    template <std::size_t N> static void Bind(std::array<MYSQL_BIND, N>& b, std::array<unsigned long long, N>& values)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            b[i].buffer_type = MYSQL_TYPE_LONGLONG;
            b[i].buffer = &values[i];
            b[i].is_unsigned = true;
        }

    }

    static void CheckActionBar(MySql::Connection& c)
    {
        if (MySql::Scalar(c, "SELECT COUNT(*) FROM schema_migrations WHERE version=5 AND "
                             "checksum=SHA2('character_action_bar_v5',256)") != 1)
        {
            throw MySql::Error(1644);
        }

        MySql::Statement fields(c, "SELECT "
                                   "character_id,skill_1,skill_2,skill_3,skill_4,skill_5,skill_6,skill_7,skill_8,skill_"
                                   "9,skill_10,hp_potion,mp_potion FROM character_action_bar LIMIT 0");
    }

    static void SaveActionBar(MySql::Connection& c, std::uint64_t id, const State& s)
    {
        std::vector<std::string> values{std::to_string(id)};
        for (auto value : s.quickSlots)
        {
            values.push_back(std::to_string(value));
        }

        for (auto value : s.potions)
        {
            values.push_back(std::to_string(value));
        }

        MySql::Execute(
            c,
            "INSERT INTO "
            "character_action_bar(character_id,skill_1,skill_2,skill_3,skill_4,skill_5,skill_6,skill_7,skill_8,skill_9,"
            "skill_10,hp_potion,mp_potion) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE "
            "skill_1=VALUES(skill_1),skill_2=VALUES(skill_2),skill_3=VALUES(skill_3),skill_4=VALUES(skill_4),skill_5="
            "VALUES(skill_5),skill_6=VALUES(skill_6),skill_7=VALUES(skill_7),skill_8=VALUES(skill_8),skill_9=VALUES("
            "skill_9),skill_10=VALUES(skill_10),hp_potion=VALUES(hp_potion),mp_potion=VALUES(mp_potion)",
            {values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8],
             values[9], values[10], values[11], values[12]});
    }

    void InitializeSkills(MySql::Connection& c, std::uint64_t id)
    {
        const auto CHARACTER = std::to_string(id);
        MySql::Statement create(c, "INSERT INTO character_skill_progression(character_id,spent_points) VALUES(?,0) ON "
                                   "DUPLICATE KEY UPDATE character_id=character_id");
        create.Execute({CHARACTER});
        if (mysql_stmt_affected_rows(create.Get()) != 1)
        {
            return;
        }

        for (const auto& [skill, d] : this->catalog->skills.definitions)
        {
            if (d.initialRank)
            {
                MySql::Execute(
                    c, "INSERT IGNORE INTO character_skill_ranks(character_id,ability_id,skill_rank) VALUES(?,?,?)",
                    {CHARACTER, std::to_string(skill), std::to_string(d.initialRank)});
            }

        }

    }

    State Load(MySql::Connection& c, std::uint64_t id)
    {
        const auto CHARACTER = std::to_string(id);
        State s;
        MySql::Statement header(c, "SELECT gold,economy_revision,session_fence,next_instance_id FROM character_economy "
                                   "WHERE character_id=? FOR UPDATE");
        header.Execute({CHARACTER});
        std::array<unsigned long long, 4> h{};
        std::array<MYSQL_BIND, 4> hb{};
        Bind(hb, h);
        header.BindResult(hb.data());
        if (!header.Fetch())
        {
            throw MySql::Error(1329);
        }

        s.gold = h[0];
        s.revision = h[1];
        s.fence = h[2];
        s.nextId = h[3];
        MySql::Statement rows(
            c, "SELECT i.instance_id,i.template_id,i.quantity,i.bag_slot,COALESCE(e.equipment_slot,255) FROM "
               "character_items i LEFT JOIN character_equipped_items e ON e.character_id=i.character_id AND "
               "e.instance_id=i.instance_id WHERE i.character_id=? ORDER BY i.bag_slot");
        rows.Execute({CHARACTER});
        std::array<unsigned long long, 5> v{};
        std::array<MYSQL_BIND, 5> b{};
        Bind(b, v);
        rows.BindResult(b.data());
        while (rows.Fetch())
        {
            if (s.items.size() >= 32 || v[1] > UINT32_MAX || v[2] > 999 || v[3] >= 32 || v[4] > 255)
            {
                throw MySql::Error(1644);
            }

            s.items.push_back({v[0], static_cast<std::uint32_t>(v[1]), static_cast<std::uint32_t>(v[2]),
                               static_cast<std::uint8_t>(v[3]), static_cast<std::uint8_t>(v[4])});
        }

        s.skills.level = static_cast<std::uint32_t>(
            MySql::Scalar(c, "SELECT level FROM characters WHERE character_id=?", {CHARACTER}));
        const auto SPENT = MySql::Scalar(
            c, "SELECT spent_points FROM character_skill_progression WHERE character_id=? FOR UPDATE", {CHARACTER});
        const auto BUDGET = this->catalog->skills.Budget(s.skills.level);
        if (SPENT > BUDGET)
        {
            throw MySql::Error(1644);
        }

        s.skills.spent = static_cast<std::uint32_t>(SPENT);
        s.skills.points = BUDGET - s.skills.spent;
        MySql::Statement skills(c, "SELECT ability_id,skill_rank FROM character_skill_ranks WHERE character_id=?");
        skills.Execute({CHARACTER});
        std::array<unsigned long long, 2> sk{};
        std::array<MYSQL_BIND, 2> skb{};
        Bind(skb, sk);
        skills.BindResult(skb.data());
        while (skills.Fetch())
        {
            if (sk[0] > 64 || sk[1] > 5)
            {
                throw MySql::Error(1644);
            }

            s.skills.ranks[sk[0]] = static_cast<std::uint8_t>(sk[1]);
        }

        MySql::Statement actionBar(c, "SELECT "
                                      "skill_1,skill_2,skill_3,skill_4,skill_5,skill_6,skill_7,skill_8,skill_9,skill_"
                                      "10,hp_potion,mp_potion FROM character_action_bar WHERE character_id=?");
        actionBar.Execute({CHARACTER});
        std::array<unsigned long long, 12> ab{};
        std::array<MYSQL_BIND, 12> abb{};
        Bind(abb, ab);
        actionBar.BindResult(abb.data());
        if (!actionBar.Fetch())
        {
            throw MySql::Error(1329);
        }

        for (unsigned i = 0; i < 10; ++i)
        {
            if (ab[i] > 64)
            {
                throw MySql::Error(1644);
            }

            s.quickSlots[i] = static_cast<std::uint32_t>(ab[i]);
        }

        for (unsigned i = 0; i < 2; ++i)
        {
            if (ab[10 + i] > INT32_MAX)
            {
                throw MySql::Error(1644);
            }

            s.potions[i] = static_cast<std::uint32_t>(ab[10 + i]);
        }

        if (!s.Valid(*this->catalog) || !this->catalog->skills.Valid(s.skills))
        {
            throw MySql::Error(1644);
        }

        return s;
    }

    void Save(MySql::Connection& c, std::uint64_t id, const State& s)
    {
        if (!s.Valid(*this->catalog))
        {
            throw MySql::Error(1644);
        }

        const auto CHARACTER = std::to_string(id);
        MySql::Execute(c, "DELETE FROM character_equipped_items WHERE character_id=?", {CHARACTER});
        MySql::Execute(c, "DELETE FROM character_items WHERE character_id=?", {CHARACTER});
        MySql::Execute(c, "DELETE FROM character_equipment WHERE character_id=?", {CHARACTER});
        for (const auto& i : s.items)
        {
            MySql::Execute(
                c,
                "INSERT INTO character_items(character_id,instance_id,template_id,quantity,bag_slot) VALUES(?,?,?,?,?)",
                {CHARACTER, std::to_string(i.id), std::to_string(i.type), std::to_string(i.quantity),
                 std::to_string(i.bag)});
            if (i.equipped != shared_world::economy::NO_SLOT)
            {
                MySql::Execute(
                    c, "INSERT INTO character_equipped_items(character_id,equipment_slot,instance_id) VALUES(?,?,?)",
                    {CHARACTER, std::to_string(i.equipped), std::to_string(i.id)});
                MySql::Execute(
                    c,
                    "INSERT INTO character_equipment(character_id,equipment_slot,item_template_id,appearance_id) "
                    "VALUES(?,?,?,?)",
                    {CHARACTER, std::to_string(i.equipped), std::to_string(i.type),
                     std::to_string(this->catalog->Find(i.type)->appearance)});
            }

        }

        MySql::Execute(c,
                       "INSERT INTO character_skill_progression(character_id,spent_points) VALUES(?,?) ON DUPLICATE "
                       "KEY UPDATE spent_points=VALUES(spent_points)",
                       {CHARACTER, std::to_string(s.skills.spent)});
        MySql::Execute(c, "DELETE FROM character_skill_ranks WHERE character_id=?", {CHARACTER});
        for (unsigned skillId = 3; skillId < s.skills.ranks.size(); ++skillId)
        {
            if (s.skills.ranks[skillId])
            {
                MySql::Execute(c, "INSERT INTO character_skill_ranks(character_id,ability_id,skill_rank) VALUES(?,?,?)",
                               {CHARACTER, std::to_string(skillId), std::to_string(s.skills.ranks[skillId])});
            }

        }

        SaveActionBar(c, id, s);
        MySql::Execute(c,
                       "UPDATE character_economy SET gold=?,economy_revision=?,session_fence=?,next_instance_id=? "
                       "WHERE character_id=?",
                       {std::to_string(s.gold), std::to_string(s.revision), std::to_string(s.fence),
                        std::to_string(s.nextId), CHARACTER});
    }

    std::shared_ptr<const shared_world::economy::Catalog> catalog;
};

