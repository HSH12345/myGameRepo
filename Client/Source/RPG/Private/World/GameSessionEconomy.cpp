#include "World/GameSessionSubsystem.h"
#include "World/WorldInventoryUI.h"
#include "World/WorldPlayerPawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

namespace
{
FString EconomyReason(shared_world::economy::Result result)
{
    static const TCHAR* text[] = {TEXT("Confirmed."),
                                  TEXT("Busy. Try again shortly."),
                                  TEXT("Inventory service unavailable."),
                                  TEXT("Confirming the previous operation. Please wait."),
                                  TEXT("You do not own that item."),
                                  TEXT("Inventory changed. Try again."),
                                  TEXT("Session changed. Reconnect to refresh."),
                                  TEXT("Invalid item or slot."),
                                  TEXT("Not enough Gold."),
                                  TEXT("Inventory full."),
                                  TEXT("This item cannot be equipped."),
                                  TEXT("Unequip the item before moving it."),
                                  TEXT("Operation conflict."),
                                  TEXT("The previous operation was not applied."),
                                  TEXT("Shop expired. Talk to the merchant again."),
                                  TEXT("Move closer to the merchant."),
                                  TEXT("Shop catalog changed or offer unavailable."),
                                  TEXT("This item cannot be used."),
                                  TEXT("HP / MP is already full. Potion was not consumed."),
                                  TEXT("Potion is cooling down."),
                                  TEXT("You cannot use items while dead.")};
    return text[static_cast<unsigned>(result)];
}

}

const shared_world::economy::Item* UGameSessionSubsystem::BagItem(int32 slot) const
{
    for (const auto& item : this->economyState.items)
    {
        if (item.bag == slot)
        {
            return &item;
        }

    }

    return nullptr;
}

bool UGameSessionSubsystem::InventoryVisible() const
{
    return this->inventoryUi && this->inventoryUi->GetVisibility() == ESlateVisibility::Visible;
}

AWorldPlayerPawn* UGameSessionSubsystem::GetInventoryCharacter() const
{
    const auto* pawn = this->players.Find(this->prepared.epoch);
    return pawn && IsValid(*pawn) ? pawn->Get() : nullptr;
}

AWorldPlayerPawn* UGameSessionSubsystem::GetShopMerchant() const
{
    const auto* pawn = this->npcs.Find(this->shopNpc);
    return pawn && IsValid(*pawn) ? pawn->Get() : nullptr;
}

void UGameSessionSubsystem::RefreshInventory()
{
    if (this->inventoryUi)
    {
        this->inventoryUi->Refresh();
    }

}

void UGameSessionSubsystem::ToggleInventory()
{
    if (this->state != 5)
    {
        return;
    }

    if (InventoryVisible())
    {
        CloseShop();
        return;
    }

    if (!this->itemCatalog)
    {
        try
        {
            this->itemCatalog =
                std::make_shared<const shared_world::economy::Catalog>(shared_world::economy::Catalog::Load(
                    TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Items")))));
        }

        catch (...)
        {
            this->onStatusChanged.Broadcast(TEXT("Item catalog unavailable."));
            return;
        }

    }

    if (!this->inventoryUi)
    {
        auto* controller = GetGameInstance()->GetFirstLocalPlayerController();
        if (!controller)
        {
            return;
        }

        this->inventoryUi = CreateWidget<UWorldInventoryUI>(controller, UWorldInventoryUI::StaticClass());
        if (!this->inventoryUi)
        {
            return;
        }

        this->inventoryUi->AddToViewport(15);
    }

    CancelClickMove();
    this->inventoryUi->SetVisibility(ESlateVisibility::Visible);
    RefreshInventory();
    for (const auto& entry : this->players)
    {
        if (IsValid(entry.Value) && entry.Value->EquipmentFailed())
        {
            entry.Value->RetryEquipment();
        }

    }

    if (!this->economyReady && !EconomyBusy())
    {
        shared_world::economy::Request query;
        query.action = shared_world::economy::Action::LOAD;
        RequestEconomy(query);
    }

}

bool UGameSessionSubsystem::OpenShop(uint64 entity)
{
    if (this->state != 5)
    {
        return false;
    }

    if (EconomyBusy())
    {
        this->onStatusChanged.Broadcast(TEXT("Confirming inventory. Please wait before trading."));
        return false;
    }

    shared_world::net::Message message;
    message.kind = shared_world::net::Kind::SHOP_OPEN;
    message.epoch = this->prepared.epoch;
    message.request = this->prepared.request;
    message.entity = entity;
    message.sequence = ++this->npcSequence;
    if (!Send(message))
    {
        this->onStatusChanged.Broadcast(TEXT("Could not open the shop. Check the connection."));
        return false;
    }

    CancelClickMove();
    this->shopNpc = entity;
    this->shopOpening = true;
    this->shopDeadline = FPlatformTime::Seconds() + 5;
    this->onStatusChanged.Broadcast(TEXT("Opening merchant shop..."));
    return true;
}

void UGameSessionSubsystem::CloseShop()
{
    if (this->state == 5 && this->shopToken)
    {
        shared_world::net::Message message;
        message.kind = shared_world::net::Kind::SHOP_CLOSE;
        message.epoch = this->prepared.epoch;
        message.request = this->prepared.request;
        message.entity = this->shopNpc;
        message.interaction = this->shopToken;
        Send(message);
    }

    this->shopNpc = this->shopToken = this->shopId = 0;
    this->shopOffers.clear();
    this->shopOpening = false;
    if (this->inventoryUi)
    {
        this->inventoryUi->StopPreviews();
        this->inventoryUi->SetVisibility(ESlateVisibility::Collapsed);
    }

}

bool UGameSessionSubsystem::RequestEconomy(shared_world::economy::Request query)
{
    using namespace shared_world::economy;
    if (this->state != 5 || this->economyPending || (this->economyUnknown && query.action != Action::QUERY))
    {
        return false;
    }

    if (query.Mutation() && !this->economyReady)
    {
        return false;
    }

    if (query.Mutation())
    {
        query.operation = TCHAR_TO_UTF8(*FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        query.revision = this->economyState.revision;
        query.catalog = this->itemCatalog ? this->itemCatalog->revision : 0;
    }

    shared_world::net::Message message;
    message.kind = shared_world::net::Kind::ECONOMY_REQUEST;
    message.epoch = this->prepared.epoch;
    message.request = this->prepared.request;
    message.entity = this->shopNpc;
    message.interaction = this->shopToken;
    message.economyReply.requestId = ++this->economyRequest;
    message.economyRequest = query;
    if (!Send(message))
    {
        this->onStatusChanged.Broadcast(TEXT("Could not submit inventory request. Check the connection."));
        return false;
    }

    this->pendingEconomyRequest = message.economyReply.requestId;
    this->economyPending = true;
    this->economyDeadline = FPlatformTime::Seconds() + 12;
    if (query.Mutation())
    {
        this->pendingEconomy = query;
        this->pendingEconomyCharacter = this->economyCharacter;
        this->pendingPotionCooldownMs = 0;
        if (query.action == Action::USE && this->itemCatalog)
        {
            for (const auto& item : this->economyState.items)
            {
                if (item.id == query.instance)
                {
                    if (const auto EFFECT = this->itemCatalog->consumables.find(item.type);
                        EFFECT != this->itemCatalog->consumables.end())
                    {
                        this->pendingPotionCooldownMs = EFFECT->second.cooldownMs;
                    }

                }

            }

        }

    }

    RefreshInventory();
    return true;
}

bool UGameSessionSubsystem::BuyOffer(int32 offer, int32 quantity)
{
    if (!this->shopToken)
    {
        this->onStatusChanged.Broadcast(TEXT("Shop is closed. Talk to the merchant again."));
        return false;
    }

    if (quantity < 1)
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::BUY;
    query.shop = static_cast<uint32>(this->shopId);
    query.offer = offer;
    query.quantity = quantity;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::EquipBagItem(int32 bag)
{
    const auto* item = BagItem(bag);
    if (!item)
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::EQUIP;
    query.instance = item->id;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::UseBagItem(int32 bag)
{
    if (EconomyBusy())
    {
        return false;
    }

    const auto* item = BagItem(bag);
    if (!item || !this->itemCatalog || !this->itemCatalog->consumables.count(item->type))
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::USE;
    query.instance = item->id;
    query.quantity = 1;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::UseHealthPotion()
{
    return UseRegisteredPotion(0);
}

bool UGameSessionSubsystem::UseManaPotion()
{
    return UseRegisteredPotion(1);
}

bool UGameSessionSubsystem::UseRegisteredPotion(int32 slot)
{
    if (slot < 0 || slot > 1)
    {
        return false;
    }

    for (int32 bag = 0; bag < 32; ++bag)
    {
        if (const auto* item = BagItem(bag); item && item->type == this->registeredPotions[slot])
        {
            return UseBagItem(bag);
        }

    }

    this->onStatusChanged.Broadcast(
        slot ? TEXT("No registered mana potions. Select a potion in the bag and press Register.")
             : TEXT("No registered health potions. Select a potion in the bag and press Register."));
    return false;
}

bool UGameSessionSubsystem::RegisterPotion(int32 bag)
{
    const auto* item = BagItem(bag);
    if (!item || !this->itemCatalog)
    {
        return false;
    }

    const auto EFFECT = this->itemCatalog->consumables.find(item->type);
    if (EFFECT == this->itemCatalog->consumables.end())
    {
        return false;
    }

    return RegisterPotionType(EFFECT->second.mana ? 1 : 0, item->type);
}

bool UGameSessionSubsystem::UnequipBagItem(int32 bag)
{
    const auto* item = BagItem(bag);
    if (!item)
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::UNEQUIP;
    query.instance = item->id;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::MoveBagItem(int32 bag, int32 destination)
{
    const auto* item = BagItem(bag);
    if (!item || destination < 0 || destination >= 32)
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::MOVE;
    query.instance = item->id;
    query.destination = destination;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::SplitBagItem(int32 bag, int32 quantity, int32 destination)
{
    const auto* item = BagItem(bag);
    if (!item || quantity < 1 || destination < 0 || destination >= 32)
    {
        return false;
    }

    shared_world::economy::Request query;
    query.action = shared_world::economy::Action::SPLIT;
    query.instance = item->id;
    query.quantity = quantity;
    query.destination = destination;
    return RequestEconomy(query);
}

bool UGameSessionSubsystem::ReceiveEconomy(const shared_world::net::Message& message)
{
    using namespace shared_world::net;
    using namespace shared_world::economy;
    if (message.kind == Kind::EQUIPMENT)
    {
        if (auto* pawn = this->players.Find(message.entity); pawn && IsValid(*pawn))
        {
            (*pawn)->LoadEquipment(message.equipment, message.sequence);
        }

        return true;
    }

    if (message.kind != Kind::SHOP_SNAPSHOT && message.kind != Kind::ECONOMY_SNAPSHOT)
    {
        return false;
    }

    if (!this->itemCatalog)
    {
        try
        {
            this->itemCatalog = std::make_shared<const Catalog>(Catalog::Load(
                TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RPG/WorldData/Items")))));
        }

        catch (...)
        {
            Fail(TEXT("Item catalog unavailable"));
            return true;
        }

    }

    if (message.kind == Kind::SHOP_SNAPSHOT)
    {
        if (!this->shopOpening || message.entity != this->shopNpc)
        {
            return true;
        }

        if (message.catalogChecksum != this->itemCatalog->revision)
        {
            Fail(TEXT("Shop catalog mismatch"));
            return true;
        }

        this->shopId = message.targetMap;
        this->shopToken = message.interaction;
        this->shopOffers = message.offers;
        this->shopOpening = false;
        this->shopDeadline = FPlatformTime::Seconds() + 30;
        if (!InventoryVisible())
        {
            ToggleInventory();
        }

        RefreshInventory();
        this->onStatusChanged.Broadcast(TEXT("Shop opened. Select a Buy offer."));
        return true;
    }

    const auto& reply = message.economyReply;
    if (reply.requestId != 1 && reply.requestId != this->pendingEconomyRequest)
    {
        return true;
    }

    if (reply.result == Result::SUCCESS && !reply.state.Valid(*this->itemCatalog))
    {
        Fail(TEXT("Invalid authoritative inventory snapshot"));
        return true;
    }

    if (reply.state.revision && reply.state.Valid(*this->itemCatalog) &&
        reply.state.revision >= this->economyState.revision)
    {
        this->economyState = reply.state;
        this->economyReady = true;
        std::copy(this->economyState.quickSlots.begin(), this->economyState.quickSlots.end(), this->quickSlotBindings);
        std::copy(this->economyState.potions.begin(), this->economyState.potions.end(), this->registeredPotions);
    }

    if (reply.requestId == this->pendingEconomyRequest)
    {
        if (reply.result == Result::SUCCESS && this->pendingEconomy.action == Action::USE &&
            reply.operation == this->pendingEconomy.operation &&
            reply.committedRevision == this->pendingEconomy.revision + 1)
        {
            this->potionHudReadyAt = FPlatformTime::Seconds() + this->pendingPotionCooldownMs / 1000.;
        }

        this->economyPending = false;
        this->pendingEconomyRequest = 0;
        if (reply.result == Result::UNKNOWN_OUTCOME || reply.result == Result::UNAVAILABLE ||
            (this->economyUnknown && reply.result == Result::BUSY))
        {
            if (!this->pendingEconomy.operation.empty())
            {
                this->economyUnknown = true;
            }

            this->economyDeadline = FPlatformTime::Seconds() + 2;
        }
        else
        {
            this->economyUnknown = false;
            this->pendingEconomy = {};
            this->pendingEconomyCharacter = 0;
        }

    }

    if (reply.requestId == 1 && this->shopOpening)
    {
        this->shopOpening = false;
        this->shopNpc = 0;
    }

    this->onStatusChanged.Broadcast(EconomyReason(reply.result));
    RefreshInventory();
    UE_LOG(LogTemp, Display, TEXT("ECONOMY result=%u revision=%llu gold=%llu items=%llu"),
           static_cast<unsigned>(reply.result), this->economyState.revision, this->economyState.gold,
           static_cast<uint64>(this->economyState.items.size()));
    return true;
}

void UGameSessionSubsystem::TickEconomy()
{
    const double NOW = FPlatformTime::Seconds();
    for (const auto& entry : this->players)
    {
        if (IsValid(entry.Value) && entry.Value->ConsumeEquipmentError())
        {
            this->onStatusChanged.Broadcast(TEXT("Equipment display unavailable. Reopen inventory (I) to retry."));
        }

    }

    if (this->economyPending && NOW > this->economyDeadline)
    {
        this->economyPending = false;
        this->pendingEconomyRequest = 0;
        this->economyUnknown = !this->pendingEconomy.operation.empty();
        this->economyDeadline = NOW + 2;
        this->onStatusChanged.Broadcast(this->economyUnknown ? TEXT("Confirming the previous operation...")
                                                             : TEXT("Inventory request timed out."));
        RefreshInventory();
    }

    if (this->economyUnknown && !this->economyPending && this->economyReady && NOW > this->economyDeadline &&
        this->pendingEconomyCharacter == this->economyCharacter)
    {
        shared_world::economy::Request query;
        query.action = shared_world::economy::Action::QUERY;
        query.operation = this->pendingEconomy.operation;
        RequestEconomy(query);
    }

    if ((this->shopOpening || this->shopToken) && NOW > this->shopDeadline)
    {
        CloseShop();
        this->onStatusChanged.Broadcast(TEXT("Shop expired. Talk to the merchant again."));
    }

}

void UGameSessionSubsystem::ClearEconomy()
{
    if (this->economyPending && !this->pendingEconomy.operation.empty())
    {
        this->economyUnknown = true;
    }

    this->economyPending = false;
    this->economyReady = false;
    this->pendingEconomyRequest = 0;
    this->economyDeadline = 0;
    this->economyState = {};
    this->economyState.revision = 0;
    std::copy(this->economyState.quickSlots.begin(), this->economyState.quickSlots.end(), this->quickSlotBindings);
    std::copy(this->economyState.potions.begin(), this->economyState.potions.end(), this->registeredPotions);
    this->selectedQuickSlot = -1;
    this->shopNpc = this->shopToken = this->shopId = 0;
    this->shopOffers.clear();
    this->shopOpening = false;
    if (this->inventoryUi)
    {
        this->inventoryUi->RemoveFromParent();
        this->inventoryUi = nullptr;
    }

}

