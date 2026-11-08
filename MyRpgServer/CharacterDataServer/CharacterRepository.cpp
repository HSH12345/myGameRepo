#include "CharacterRepository.h"
#include "EconomyRepository.h"
#include "CharacterCreationRules.h"
#include "../DatabaseCore/MySql.h"
#include "../ServerCore/StructuredLogger.h"

namespace
{
std::vector<RpgProtocol::CharacterSummary> ReadSummaries(MySql::Connection& connection, const std::string& accountUid)
{
    const char* sql =
        "SELECT "
        "c.character_id,c.character_name,c.race,c.gender,c.level,e.equipment_slot,e.item_template_id,e.appearance_id "
        "FROM (SELECT character_id,character_name,race,gender,level FROM characters "
        "WHERE account_uid=? AND deleted_at IS NULL ORDER BY character_id LIMIT 8) c "
        "LEFT JOIN character_equipment e ON e.character_id=c.character_id ORDER BY c.character_id,e.equipment_slot";

    MySql::Statement statement(connection, sql);
    statement.Execute({accountUid});
    unsigned long long characterId = 0;
    char name[97] = {};
    unsigned long nameLength = 0;
    unsigned char race = 0, gender = 0, slot = 0;
    unsigned int level = 0, itemTemplateId = 0, appearanceId = 0;
    bool slotNull = false, templateNull = false, appearanceNull = false;
    MYSQL_BIND outputs[8] = {};
    outputs[0].buffer_type = MYSQL_TYPE_LONGLONG;
    outputs[0].buffer = &characterId;
    outputs[0].is_unsigned = true;
    outputs[1].buffer_type = MYSQL_TYPE_STRING;
    outputs[1].buffer = name;
    outputs[1].buffer_length = sizeof(name) - 1;
    outputs[1].length = &nameLength;
    outputs[2].buffer_type = MYSQL_TYPE_TINY;
    outputs[2].buffer = &race;
    outputs[2].is_unsigned = true;
    outputs[3].buffer_type = MYSQL_TYPE_TINY;
    outputs[3].buffer = &gender;
    outputs[3].is_unsigned = true;
    outputs[4].buffer_type = MYSQL_TYPE_LONG;
    outputs[4].buffer = &level;
    outputs[4].is_unsigned = true;
    outputs[5].buffer_type = MYSQL_TYPE_TINY;
    outputs[5].buffer = &slot;
    outputs[5].is_unsigned = true;
    outputs[5].is_null = &slotNull;
    outputs[6].buffer_type = MYSQL_TYPE_LONG;
    outputs[6].buffer = &itemTemplateId;
    outputs[6].is_unsigned = true;
    outputs[6].is_null = &templateNull;
    outputs[7].buffer_type = MYSQL_TYPE_LONG;
    outputs[7].buffer = &appearanceId;
    outputs[7].is_unsigned = true;
    outputs[7].is_null = &appearanceNull;

    statement.BindResult(outputs);
    std::vector<RpgProtocol::CharacterSummary> characters;
    while (statement.Fetch())
    {
        if (characterId == 0 || nameLength > sizeof(name) - 1)
        {
            throw MySql::Error(1406);
        }

        if (characters.empty() || characters.back().characterId != characterId)
        {
            RpgProtocol::CharacterSummary value;
            value.characterId = characterId;
            value.characterName.assign(name, nameLength);
            value.race = race;
            value.gender = gender;
            value.level = level;
            characters.push_back(std::move(value));
        }

        if (!slotNull && !templateNull && !appearanceNull)
        {
            if (characters.back().equipment.size() >= RpgProtocol::MAX_EQUIPMENT_PER_CHARACTER)
            {
                throw MySql::Error(1406);
            }

            characters.back().equipment.push_back({slot, itemTemplateId, appearanceId});
        }

    }

    return characters;
}

bool ReplayOperation(MySql::Connection& connection, const RpgProtocol::InternalCreateCharacterRequestPacket& request,
                     RpgProtocol::CreateCharacterResponsePacket& response)
{
    MySql::Statement statement(
        connection, "SELECT request_hash=UNHEX(SHA2(?,256)),response_blob FROM character_creation_operations "
                    "WHERE account_uid=? AND operation_id=?");
    statement.Execute({request.FingerprintInput(), request.accountUid, request.operationId});
    unsigned int matches = 0;
    char blob[4096] = {};
    unsigned long length = 0;
    MYSQL_BIND outputs[2] = {};
    outputs[0].buffer_type = MYSQL_TYPE_LONG;
    outputs[0].buffer = &matches;
    outputs[0].is_unsigned = true;
    outputs[1].buffer_type = MYSQL_TYPE_BLOB;
    outputs[1].buffer = blob;
    outputs[1].buffer_length = sizeof(blob);
    outputs[1].length = &length;
    statement.BindResult(outputs);
    if (!statement.Fetch())
    {
        return false;
    }

    if (matches == 0)
    {
        response.result = RpgProtocol::CreateCharacterResult::OPERATION_CONFLICT;
    }
    else
    {
        if (!response.Deserialize(reinterpret_cast<const std::uint8_t*>(blob), length))
        {
            throw MySql::Error(1406);
        }

        response.requestId = request.requestId;
    }

    return true;
}

void StoreOperation(MySql::Connection& connection, const RpgProtocol::InternalCreateCharacterRequestPacket& request,
                    const RpgProtocol::CreateCharacterResponsePacket& response)
{
    RpgProtocol::ByteBuffer blob;
    if (!response.Serialize(blob) || blob.size() > 4096)
    {
        throw MySql::Error(1406);
    }

    MySql::Execute(connection,
                   "INSERT INTO character_creation_operations(account_uid,operation_id,request_hash,response_blob) "
                   "VALUES(?,?,UNHEX(SHA2(?,256)),?)",
                   {request.accountUid, request.operationId, request.FingerprintInput(),
                    std::string(reinterpret_cast<const char*>(blob.data()), blob.size())});
}

}

CharacterRepository& CharacterRepository::GetInstance()
{
    static CharacterRepository value;
    return value;
}

bool CharacterRepository::ValidateSchema()
{
    try
    {
        MySql::Connection connection;
        if (MySql::Scalar(connection, "SELECT COUNT(*) FROM schema_migrations WHERE version=2 AND "
                                      "checksum=SHA2('character_creation_v2',256)") != 1)
        {
            return false;
        }

        // Prepare all new table shapes as well; a version marker alone is insufficient.
        MySql::Statement roster(connection, "SELECT account_uid FROM character_rosters LIMIT 0");
        MySql::Statement operation(
            connection,
            "SELECT account_uid,operation_id,request_hash,response_blob FROM character_creation_operations LIMIT 0");
        MySql::Statement characters(
            connection,
            "SELECT character_id,account_uid,character_name,race,gender,level,deleted_at FROM characters LIMIT 0");
        MySql::Statement equipment(
            connection,
            "SELECT character_id,equipment_slot,item_template_id,appearance_id FROM character_equipment LIMIT 0");
        return true;
    }

    catch (...)
    {
        return false;
    }

}

bool CharacterRepository::FindByAccount(const std::string& accountUid,
                                        std::vector<RpgProtocol::CharacterSummary>& characters)
{
    try
    {
        MySql::Connection connection;
        characters = ReadSummaries(connection, accountUid);
        return true;
    }

    catch (const MySql::Error& error)
    {
        characters.clear();
        RPG_LOG_ERROR("Database", "Character list query failed. code=" << error.code);
        return false;
    }

}

RpgProtocol::CreateCharacterResponsePacket CharacterRepository::Create(
    const RpgProtocol::InternalCreateCharacterRequestPacket& request)
{
    using namespace RpgProtocol;
    CreateCharacterResponsePacket response;
    response.requestId = request.requestId;
    response.operationId = request.operationId;
    try
    {
        MySql::Connection connection;
        MySql::Transaction transaction(connection);
        // All character create/delete operations must take this account-scoped
        // database lock. It also coordinates different server processes.
        // Duplicate-key UPDATE takes an exclusive lock immediately. INSERT IGNORE
        // would take shared locks first and deadlock when concurrent requests
        // both upgraded them with SELECT FOR UPDATE.
        MySql::Execute(connection,
                       "INSERT INTO character_rosters(account_uid) VALUES(?) "
                       "ON DUPLICATE KEY UPDATE account_uid=account_uid",
                       {request.accountUid});
        MySql::Statement lock(connection, "SELECT account_uid FROM character_rosters WHERE account_uid=? FOR UPDATE");
        lock.Execute({request.accountUid});
        // Buffer the lock-query result before issuing another statement.
        char uid[513] = {};
        MYSQL_BIND output = {};
        output.buffer_type = MYSQL_TYPE_STRING;
        output.buffer = uid;
        output.buffer_length = sizeof(uid);
        lock.BindResult(&output);
        if (!lock.Fetch())
        {
            throw MySql::Error(1329);
        }

        if (ReplayOperation(connection, request, response))
        {
            return response;
        }

        const auto* defaults = CharacterCreationRules::GetInstance().Find(request.race, request.gender);
        if (!CharacterCreationRules::IsValidName(request.characterName))
        {
            response.result = CreateCharacterResult::INVALID_NAME;
        }
        else if (!defaults)
        {
            response.result = CreateCharacterResult::INVALID_APPEARANCE;
        }
        else if (MySql::Scalar(connection, "SELECT COUNT(*) FROM characters WHERE account_uid=? AND deleted_at IS NULL",
                               {request.accountUid}) >= MAX_CHARACTERS_PER_ACCOUNT)
        {
            response.result = CreateCharacterResult::LIMIT_REACHED;
        }
        else
        {
            CharacterSummary created = *defaults;
            created.characterName = request.characterName;
            bool inserted = false;
            try
            {
                MySql::Statement insert(
                    connection,
                    "INSERT INTO characters(account_uid,character_name,race,gender,level) VALUES(?,?,?,?,?)");
                insert.Execute({request.accountUid, created.characterName, std::to_string(created.race),
                                std::to_string(created.gender), std::to_string(created.level)});
                created.characterId = mysql_stmt_insert_id(insert.Get());
                inserted = true;
            }

            catch (const MySql::Error& error)
            {
                if (error.code != 1062)
                {
                    throw;
                }

                response.result = CreateCharacterResult::NAME_TAKEN;
            }

            if (inserted)
            {
                for (const auto& item : created.equipment)
                {
                    MySql::Execute(
                        connection,
                        "INSERT INTO character_equipment(character_id,equipment_slot,item_template_id,appearance_id) "
                        "VALUES(?,?,?,?)",
                        {std::to_string(created.characterId), std::to_string(item.slot),
                         std::to_string(item.itemTemplateId), std::to_string(item.appearanceId)});
                }

                EconomyRepository::Get().Initialize(connection, created.characterId);
                response.result = CreateCharacterResult::SUCCESS;
                response.characters.push_back(std::move(created));
            }

        }

        StoreOperation(connection, request, response);
        if (!transaction.Commit())
        {
            response.result = CreateCharacterResult::UNKNOWN_OUTCOME;
            response.characters.clear();
        }

    }

    catch (const MySql::Error& error)
    {
        RPG_LOG_ERROR("Database", "Character creation transaction failed. code=" << error.code);
        response.result = CreateCharacterResult::UNAVAILABLE;
        response.characters.clear();
    }

    return response;
}

