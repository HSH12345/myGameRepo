#include "World/WorldEntityPool.h"
#include "World/WorldPlayerPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AWorldPlayerPawn* UWorldEntityPool::Acquire(const FString& appearanceKey)
{
    AWorldPlayerPawn* available = nullptr;
    for (const auto& pawn : this->actors)
    {
        if (IsValid(pawn) && !pawn->IsPoolActive())
        {
            if (pawn->GetAppearanceKey() == appearanceKey)
            {
                available = pawn;
                break;
            }

            if (!available)
            {
                available = pawn;
            }

        }

    }

    if (!available)
    {
        if (this->actors.Num() >= 288)
        {
            return nullptr; // 32 players + 256 NPCs, retained until world teardown.
        }

        available = GetWorld()->SpawnActor<AWorldPlayerPawn>();
        if (!available)
        {
            return nullptr;
        }

        this->actors.Add(available);
    }

    available->SetPooledActive(true);
    return available;
}

void UWorldEntityPool::Release(AWorldPlayerPawn* pawn)
{
    if (!IsValid(pawn) || !this->actors.Contains(pawn) || !pawn->IsPoolActive())
    {
        return;
    }

    if (auto* controller = pawn->GetController())
    {
        controller->UnPossess();
    }

    pawn->SetPooledActive(false);
}

int32 UWorldEntityPool::ActiveCount() const
{
    int32 count = 0;
    for (const auto& pawn : this->actors)
    {
        if (IsValid(pawn) && pawn->IsPoolActive())
        {
            ++count;
        }

    }

    return count;
}

void UWorldEntityPool::Deinitialize()
{
    for (const auto& pawn : this->actors)
    {
        if (IsValid(pawn))
        {
            pawn->Destroy();
        }

    }

    this->actors.Empty();
    Super::Deinitialize();
}

