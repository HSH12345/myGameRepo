#pragma once
#include "ProceduralMeshComponent.h"
#include "World/WorldCombatPresentation.h"

// One draw section per effect, built on activation. Animation uses transforms
// and material parameters, with no vertex or collision rebuild on frame ticks.
struct FCombatRibbonGeometry
{
    TArray<FVector> v, n;
    TArray<int32> i;
    TArray<FVector2D> uv;
    TArray<FLinearColor> c;

    FCombatRibbonGeometry()
    {
        this->v.Reserve(1536);
        this->n.Reserve(1536);
        this->i.Reserve(2304);
        this->uv.Reserve(1536);
        this->c.Reserve(1536);
    }

    void Quad(FVector a, FVector b, FVector d, FVector e, FLinearColor color = FLinearColor::White)
    {
        const int32 BASE = this->v.Num();
        const FVector NORMAL = FVector::CrossProduct(b - a, e - a).GetSafeNormal();
        this->v.Append({a, b, d, e});
        this->n.Append({NORMAL, NORMAL, NORMAL, NORMAL});
        this->uv.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
        this->c.Append({color, color, color, color});
        this->i.Append({BASE, BASE + 1, BASE + 2, BASE, BASE + 2, BASE + 3});
    }

    void Ribbon(FVector a, FVector b, float width, FVector normal = FVector::UpVector, float alpha = 1)
    {
        FVector side = FVector::CrossProduct((b - a).GetSafeNormal(), normal).GetSafeNormal() * width;
        if (side.IsNearlyZero())
        {
            side = FVector::RightVector * width;
        }

        Quad(a - side, b - side, b + side, a + side, FLinearColor(1, 1, 1, alpha));
    }

    void Arc(float radius, float width, float start, float end, FVector offset = FVector::ZeroVector,
             bool vertical = false, float alpha = 1)
    {
        const int32 COUNT = FMath::Clamp(FMath::RoundToInt(FMath::Abs(end - start) / 7.f), 6, 52);
        const auto P = [&](float r, float a)
        {
            const float T = FMath::DegreesToRadians(a);
            return offset + (vertical ? FVector(FMath::Cos(T) * r, 0, FMath::Sin(T) * r)
                                      : FVector(FMath::Cos(T) * r, FMath::Sin(T) * r, 0));
        };

        for (int32 k = 0; k < COUNT; ++k)
        {
            const float A = FMath::Lerp(start, end, float(k) / COUNT),
                        B = FMath::Lerp(start, end, float(k + 1) / COUNT);
            Quad(P(radius - width, A), P(radius - width, B), P(radius + width, B), P(radius + width, A),
                 FLinearColor(1, 1, 1, alpha));
        }

    }

    void Sparks(int32 count, float radius, uint32 seed, FVector offset = FVector::ZeroVector)
    {
        FRandomStream random(seed);
        for (int32 k = 0; k < count; ++k)
        {
            const FVector D = random.VRand();
            const float R = random.FRandRange(radius * .35f, radius);
            Ribbon(offset + D * R * .4, offset + D * R, random.FRandRange(1, 3), FVector::UpVector,
                   random.FRandRange(.45, 1));
        }

    }

    void Build(UProceduralMeshComponent* mesh, const FCombatArt& art, int32 kind, float radius, float range,
               float angle, uint32 seed)
    {
        const bool FLAME = art.style == ECombatArt::FIRE;
        const float W = art.width;
        if (kind == 1)
        {
            const float LENGTH = art.style == ECombatArt::LANCE ? 210.f : FLAME ? 125.f : 95.f;
            Ribbon(FVector(-LENGTH, 0, 0), FVector(22, 0, 0), 12 * W);
            Ribbon(FVector(-LENGTH * .7, 0, 0), FVector(28, 0, 0), 8 * W, FVector::RightVector);
            for (int32 k = 0; k < 5; ++k)
            {
                const float A = k * UE_TWO_PI / 5;
                const FVector O(0, FMath::Cos(A) * 8, FMath::Sin(A) * 8);
                Ribbon(FVector(-LENGTH * 1.15, 0, 0) + O * .2, FVector(10, 0, 0) + O, 3 * W, FVector::UpVector, .6);
            }

            Sparks(5, 28, seed);
        }
        else if (kind == 5)
        {
            const bool SLASH = art.style == ECombatArt::STEEL || art.style == ECombatArt::RUSH;
            if (SLASH)
            {
                Arc(58 * W, 8, -70, 80);
                Arc(42 * W, 5, -65, 65, FVector::ZeroVector, true, .7);
            }
            else
            {
                Arc(30 * W, 4, 0, 360);
                Arc(24 * W, 3, 0, 360, FVector::ZeroVector, true, .75);
            }

            Ribbon(FVector(0, -45, 0), FVector(0, 45, 0), 4, FVector::ForwardVector);
            Ribbon(FVector(0, 0, -35), FVector(0, 0, 50), 3, FVector::ForwardVector);
            Sparks(SLASH ? 18 : 12, SLASH ? 95 : 65, seed);
        }
        else if (art.style == ECombatArt::BEAM)
        {
            Ribbon(FVector(0, 0, 85), FVector(range, 0, 85), 20 * W);
            Ribbon(FVector(0, 0, 85), FVector(range, 0, 85), 12 * W, FVector::RightVector);
            for (int32 k = 0; k < 5; ++k)
            {
                Arc(20 + k * 3, 3, 0, 360, FVector(range * k / 5, 0, 85), true, .65);
            }

            Sparks(16, 90, seed, FVector(range, 0, 85));
        }
        else if (art.style == ECombatArt::RUSH || art.style == ECombatArt::SHADOW)
        {
            const float L = art.style == ECombatArt::SHADOW ? -350.f : FMath::Min(600.f, range);
            Ribbon(FVector(0, 0, 75), FVector(L, 0, 75), 25 * W);
            for (int32 k = 1; k <= 3; ++k)
            {
                Arc(42, 5, -65, 65, FVector(L * k / 3, 0, 75), true, .8);
                Ribbon(FVector(L * k / 3, -30, 70), FVector(L * k / 3 + 55, 30, 95), 6);
            }

            Sparks(14, 100, seed, FVector(L, 0, 85));
        }
        else if (art.style == ECombatArt::SHOCK || art.style == ECombatArt::STEEL)
        {
            const float A = FMath::Clamp(angle, 45.f, 150.f);
            for (int32 k = 1; k <= 3; ++k)
            {
                Arc(radius * k / 3, 7 * W, -A / 2, A / 2, FVector(0, 0, 10 + k * 4));
            }

            for (int32 k = 0; k < 12; ++k)
            {
                const float T = FMath::DegreesToRadians(FMath::Lerp(-A / 2, A / 2, k / 11.f));
                const FVector D(FMath::Cos(T), FMath::Sin(T), 0);
                Ribbon(D * radius * .25 + FVector(0, 0, 8), D * radius + FVector(0, 0, 15), 3 * W, FVector::UpVector,
                       .6);
            }

            Sparks(18, 100, seed, FVector(radius * .6, 0, 35));
        }
        else
        {
            radius = FMath::Clamp(radius, 45.f, 1000.f);
            for (int32 k = 1; k <= 3; ++k)
            {
                Arc(radius * k / 3, 5 * W, 0, 360, FVector(0, 0, 8 + k * 5), false, k == 3 ? 1 : .6);
            }

            const int32 COUNT = FLAME ? 16 : 12;
            if (FLAME)
            {
                // Broad crossed central plumes keep the explosion readable
                // from an overhead camera; smaller perimeter wisps add detail.
                for (int32 k = 0; k < 5; ++k)
                {
                    const float A = k * UE_TWO_PI / 5;
                    const FVector D(FMath::Cos(A), FMath::Sin(A), 0);
                    Ribbon(D * 18 + FVector(0, 0, 5), D * 48 + FVector(0, 0, 250 + k * 22), 48 * W, D, .9);
                }

            }

            for (int32 k = 0; k < COUNT; ++k)
            {
                const float A = k * UE_TWO_PI / COUNT;
                const FVector D(FMath::Cos(A), FMath::Sin(A), 0);
                if (FLAME || art.style == ECombatArt::ACID)
                {
                    const FVector BASE = D * radius * .6;
                    Ribbon(BASE + FVector(0, 0, 3), BASE + D * radius * .13 + FVector(0, 0, FLAME ? 170 : 85),
                           FLAME ? 15 * W : 9 * W, D.RotateAngleAxis(90, FVector::UpVector));
                }
                else
                {
                    Ribbon(D * radius * .18 + FVector(0, 0, 10), D * radius * .95 + FVector(0, 0, 18), 3 * W,
                           FVector::UpVector, .75);
                }

            }

            Sparks(18, FMath::Min(radius * .7, 180.f), seed, FVector(0, 0, 35));
        }

        check(this->v.Num() <= 1536);
        mesh->CreateMeshSection_LinearColor(0, this->v, this->i, this->n, this->uv, this->c, TArray<FProcMeshTangent>(),
                                            false);
    }

};

