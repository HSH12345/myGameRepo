#include "World/GameSessionSubsystem.h"
#include "World/WorldPlayerPawn.h"
#include "World/WorldControlHUD.h"
#include "GameClientWorld.h"

bool UGameSessionSubsystem::TalkToNearestNpc()
{
    if (this->state != 5 || !this->map)
    {
        return false;
    }

    if (this->currentNpc || this->shopToken || this->shopOpening)
    {
        return true;
    }

    uint64 nearest = 0;
    std::uint64_t distance = UINT64_MAX;
    for (const auto& entry : this->npcs)
    {
        if (!IsValid(entry.Value) ||
            (entry.Value->npcRole != static_cast<int32>(shared_world::npc::Role::DIALOGUE_WARP) &&
             entry.Value->npcRole != 2))
        {
            continue;
        }

        const auto& catalog = *this->map->content->npcs;
        const auto RANGE = entry.Value->npcRole == 2 ? 300U : catalog.warps.at(entry.Value->npcRoleData).range;
        const auto PLACEMENT = std::find_if(catalog.placements.begin(), catalog.placements.end(),
                                            [&](const auto& p)
                                            {
                                                return p.Entity() == entry.Key;
                                            });

        if (PLACEMENT == catalog.placements.end() ||
            !shared_world::npc::InRange(this->predicted, PLACEMENT->feet, RANGE))
        {
            continue;
        }

        const auto CANDIDATE = shared_world::DistanceSquared2D(this->predicted, PLACEMENT->feet);
        if (CANDIDATE < distance)
        {
            distance = CANDIDATE;
            nearest = entry.Key;
        }

    }

    if (!nearest)
    {
        return false;
    }

    if (this->npcs[nearest]->npcRole == 2)
    {
        return OpenShop(nearest);
    }

    CancelClickMove();
    shared_world::net::Message talk;
    talk.kind = shared_world::net::Kind::NPC_TALK;
    talk.epoch = this->prepared.epoch;
    talk.request = this->prepared.request;
    talk.entity = nearest;
    talk.sequence = ++this->npcSequence;
    if (!Send(talk))
    {
        return false;
    }

    this->currentNpc = nearest;
    this->dialogueId = 0;
    this->npcAwaitingTalk = true;
    this->npcWarping = false;
    this->npcDeadline = FPlatformTime::Seconds() + 5;
    this->onStatusChanged.Broadcast(TEXT("Talking..."));
    return true;
}

bool UGameSessionSubsystem::ChooseNpcWarp()
{
    if (this->state != 5 || !this->currentNpc || !this->dialogueId || this->npcAwaitingTalk || this->npcWarping)
    {
        return false;
    }

    if (EconomyBusy())
    {
        this->onStatusChanged.Broadcast(TEXT("Confirming inventory. Please wait before travelling."));
        return false;
    }

    shared_world::net::Message warp;
    warp.kind = shared_world::net::Kind::NPC_WARP;
    warp.epoch = this->prepared.epoch;
    warp.request = this->prepared.request;
    warp.entity = this->currentNpc;
    warp.sequence = this->npcSequence;
    warp.interaction = this->dialogueId;
    warp.choice = 1;
    if (!Send(warp))
    {
        this->onStatusChanged.Broadcast(TEXT("Could not send travel request. Check the connection."));
        return false;
    }

    this->npcWarping = true;
    this->npcDeadline = FPlatformTime::Seconds() + 8;
    if (this->controlHud)
    {
        this->controlHud->SetNpcWarpPending();
    }

    this->onStatusChanged.Broadcast(TEXT("Requesting dungeon travel..."));
    return true;
}

void UGameSessionSubsystem::ClearNpcDialogue(bool notifyServer)
{
    if (notifyServer && this->state == 5 && this->currentNpc && this->dialogueId && !this->npcWarping)
    {
        shared_world::net::Message cancel;
        cancel.kind = shared_world::net::Kind::NPC_CANCEL;
        cancel.epoch = this->prepared.epoch;
        cancel.request = this->prepared.request;
        cancel.entity = this->currentNpc;
        cancel.sequence = this->npcSequence;
        cancel.interaction = this->dialogueId;
        Send(cancel);
    }

    this->currentNpc = 0;
    this->dialogueId = 0;
    this->npcAwaitingTalk = false;
    this->npcWarping = false;
    this->npcDeadline = 0;
    if (this->controlHud)
    {
        this->controlHud->HideNpcDialogue();
    }

}

void UGameSessionSubsystem::CloseNpcDialogue()
{
    if (this->npcWarping)
    {
        return;
    }

    ClearNpcDialogue(true);
    this->onStatusChanged.Broadcast(TEXT("World ready. Approach an NPC and press E to talk."));
}

bool UGameSessionSubsystem::SetDungeonReady(bool ready)
{
    if (this->state != 5 || !this->dungeonMatch.room)
    {
        return false;
    }

    auto message = shared_world::net::Message{};
    message.kind = shared_world::net::Kind::MATCH_ACTION;
    message.epoch = this->prepared.epoch;
    message.request = this->prepared.request;
    message.room = this->dungeonMatch.room;
    message.sequence = ++this->matchSequence;
    message.choice = static_cast<unsigned>(ready ? shared_world::party::MatchAction::READY
                                                 : shared_world::party::MatchAction::UNREADY);
    return Send(message);
}

bool UGameSessionSubsystem::LeaveDungeonMatch()
{
    if (this->state != 5 || !this->dungeonMatch.room)
    {
        return false;
    }

    auto message = shared_world::net::Message{};
    message.kind = shared_world::net::Kind::MATCH_ACTION;
    message.epoch = this->prepared.epoch;
    message.request = this->prepared.request;
    message.room = this->dungeonMatch.room;
    message.sequence = ++this->matchSequence;
    message.choice = static_cast<unsigned>(shared_world::party::MatchAction::LEAVE);
    return Send(message);
}

double UGameSessionSubsystem::DungeonCountdown() const
{
    return this->dungeonMatch.matchPhase == shared_world::party::MatchPhase::COUNTDOWN
               ? FMath::Max(0.,
                            this->dungeonMatch.countdownMs / 1000. - (FPlatformTime::Seconds() - this->matchReceivedAt))
               : 0.;
}

AWorldPlayerPawn* UGameSessionSubsystem::CombatPawn(uint64 entity) const
{
    return (entity & shared_world::npc::ENTITY_BIT) ? this->npcs.FindRef(entity).Get()
                                                    : this->players.FindRef(entity).Get();
}

bool UGameSessionSubsystem::IsPartyMember(uint64 entity) const
{
    return shared_world::party::CanSupport(this->prepared.epoch, entity,
                                           this->entityParties.FindRef(this->prepared.epoch),
                                           this->entityParties.FindRef(entity));
}

bool UGameSessionSubsystem::IsHostileEntity(uint64 entity) const
{
    const auto* pawn = CombatPawn(entity);
    if (!entity || entity == this->prepared.epoch || !IsValid(pawn) || pawn->dead || !pawn->attackable)
    {
        return false;
    }

    return shared_world::party::CanDamage(false, (entity & shared_world::npc::ENTITY_BIT) != 0,
                                          this->entityParties.FindRef(this->prepared.epoch),
                                          this->entityParties.FindRef(entity), true);
}

