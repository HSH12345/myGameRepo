#include "WorldRepository.h"
#include "../DatabaseCore/MySql.h"
#include "../ServerCore/StructuredLogger.h"

bool WorldRepository::ValidateSchema()
{
    try
    {
        MySql::Connection db;
        if (MySql::Scalar(db, "SELECT version FROM world_schema_version WHERE id=1") != 1)
        {
            return false;
        }

        MySql::Scalar(db, "SELECT COUNT(*) FROM world_servers WHERE game_port=0 AND boot_id=0 AND lease_until IS NULL");
        MySql::Scalar(db, "SELECT COUNT(*) FROM world_maps WHERE game_port=0 AND instance_id=0 AND map_id=0 AND "
                          "map_version=0 AND map_checksum=0");
        if (MySql::Scalar(db, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() "
                              "AND table_name IN ('world_servers','world_maps') AND engine='InnoDB'") != 2)
        {
            return false;
        }

        return true;
    }

    catch (const MySql::Error& error)
    {
        RPG_LOG_ERROR("Database", "World schema v1 validation failed. code=" << error.code);
        return false;
    }

    catch (...)
    {
        RPG_LOG_ERROR("Database", "World schema v1 validation failed.");
        return false;
    }

}

RpgProtocol::WorldDataResponse WorldRepository::Execute(const RpgProtocol::WorldDataRequest& request)
{
    using namespace RpgProtocol;
    WorldDataResponse reply{request.requestId, request.operation, WorldDataResult::UNAVAILABLE};
    if (!request.IsValid())
    {
        return reply;
    }

    try
    {
        MySql::Connection db;
        const auto PORT = std::to_string(request.gamePort), BOOT = std::to_string(request.boot);
        if (request.operation == WorldDataOperation::LOOKUP)
        {
            reply.result =
                MySql::Scalar(db,
                              "SELECT COUNT(*) FROM world_servers s JOIN world_maps m ON m.game_port=s.game_port "
                              "WHERE s.game_port=? AND m.instance_id=? AND s.lease_until>UTC_TIMESTAMP(6)",
                              {PORT, std::to_string(request.instance)})
                    ? WorldDataResult::SUCCESS
                    : WorldDataResult::NOT_FOUND;
            return reply;
        }

        MySql::Transaction transaction(db);
        if (request.operation == WorldDataOperation::RELEASE)
        {
            MySql::Execute(db, "UPDATE world_servers SET lease_until=UTC_TIMESTAMP(6) WHERE game_port=? AND boot_id=?",
                           {PORT, BOOT});
        }
        else
        {
            // The row lock serializes competing boots. A live boot cannot be stolen.
            MySql::Execute(
                db, "INSERT IGNORE INTO world_servers(game_port,boot_id,lease_until) VALUES(?,0,'2000-01-01')", {PORT});
            const auto CURRENT =
                MySql::Scalar(db, "SELECT boot_id FROM world_servers WHERE game_port=? FOR UPDATE", {PORT});
            if (CURRENT != request.boot &&
                MySql::Scalar(db,
                              "SELECT COUNT(*) FROM world_servers WHERE game_port=? AND lease_until>UTC_TIMESTAMP(6)",
                              {PORT}))
            {
                reply.result = WorldDataResult::CONFLICT;
                return reply;
            }

            MySql::Execute(
                db,
                "UPDATE world_servers SET boot_id=?,lease_until=UTC_TIMESTAMP(6)+INTERVAL 20 SECOND WHERE game_port=?",
                {BOOT, PORT});
            MySql::Execute(db, "DELETE FROM world_maps WHERE game_port=?", {PORT});
            for (const auto& map : request.maps)
            {
                MySql::Execute(
                    db,
                    "INSERT INTO world_maps(game_port,instance_id,map_id,map_version,map_checksum) VALUES(?,?,?,?,?)",
                    {PORT, std::to_string(map.instance), std::to_string(map.map), std::to_string(map.version),
                     std::to_string(map.checksum)});
            }

        }

        if (transaction.Commit())
        {
            reply.result = WorldDataResult::SUCCESS;
        }

    }

    catch (const MySql::Error& error)
    {
        RPG_LOG_ERROR("Database", "World operation failed. code=" << error.code);
    }

    catch (...)
    {
        RPG_LOG_ERROR("Database", "World operation failed.");
    }

    return reply;
}

