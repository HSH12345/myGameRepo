#include "World/GameSessionSubsystem.h"

bool UGameSessionSubsystem::TryUseDungeonExit()
{
    if (this->state != 5 || !this->dungeonExits.Valid() || EconomyBusy())
    {
        return false;
    }

    if (this->exitWarping)
    {
        return true;
    }

    const shared_world::dungeon::Zone* nearest = nullptr;
    std::uint64_t distance = UINT64_MAX;
    for (unsigned i = 0; i < this->dungeonExits.count; ++i)
    {
        const auto& zone = this->dungeonExits.zones[i];
        if (!zone.Contains(this->predicted))
        {
            continue;
        }

        const auto CANDIDATE = shared_world::DistanceSquared2D(this->predicted, zone.feet);
        if (CANDIDATE < distance)
        {
            distance = CANDIDATE;
            nearest = &zone;
        }

    }

    if (!nearest)
    {
        return false;
    }

    CancelClickMove();
    this->pendingExit = {this->prepared.instance, this->dungeonExits.run, ++this->exitSequence, nearest->id,
                         nearest->type,           nearest->outcome};
    shared_world::net::Message use;
    use.kind = shared_world::net::Kind::EXIT_USE;
    use.epoch = this->prepared.epoch;
    use.request = this->prepared.request;
    use.exitReceipt = this->pendingExit;
    if (!Send(use))
    {
        this->pendingExit = {};
        return false;
    }

    this->exitWarping = true;
    this->exitDeadline = FPlatformTime::Seconds() + 8;
    this->onStatusChanged.Broadcast(TEXT("Returning to town..."));
    return true;
}

