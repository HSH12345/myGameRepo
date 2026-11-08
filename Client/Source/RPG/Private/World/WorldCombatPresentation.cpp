#include "World/WorldCombatPresentation.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
struct FArtCatalog
{
    TMap<uint32, FCombatArt> rows;
    bool ready = false;

    FArtCatalog()
    {
        FString text;
        const auto PATH = FPaths::ProjectContentDir() / TEXT("RPG/WorldData/Presentation/Skills.tsv");
        if (!FFileHelper::LoadFileToString(text, *PATH) || text.Len() > 16384)
        {
            UE_LOG(LogTemp, Error, TEXT("Combat presentation table missing or over budget"));
            return;
        }

        const TArray<FString> STYLES = {TEXT("Arcane"), TEXT("Fire"),  TEXT("Steel"), TEXT("Rush"),
                                        TEXT("Shadow"), TEXT("Lance"), TEXT("Nova"),  TEXT("Beam"),
                                        TEXT("Shock"),  TEXT("Acid"),  TEXT("Roar")};
        TArray<FString> lines;
        text.ParseIntoArrayLines(lines);
        if (lines.Num() < 2 || lines.Num() > 33)
        {
            return;
        }

        for (int32 i = 1; i < lines.Num(); ++i)
        {
            TArray<FString> c;
            lines[i].ParseIntoArray(c, TEXT("\t"), false);
            if (c.Num() != 12)
            {
                this->rows.Empty();
                return;
            }

            const uint32 ID = FCString::Atoi(*c[0]);
            const int32 STYLE = STYLES.IndexOfByKey(c[1]);
            if (!ID || ID > 64 || this->rows.Contains(ID) || STYLE == INDEX_NONE || c[2].Len() > 24)
            {
                this->rows.Empty();
                return;
            }

            float v[9];
            for (int32 n = 0; n < 9; ++n)
            {
                if (!c[n + 3].IsNumeric())
                {
                    this->rows.Empty();
                    return;
                }

                v[n] = FCString::Atof(*c[n + 3]);
                if (!FMath::IsFinite(v[n]) || v[n] < 0 || v[n] > 8)
                {
                    this->rows.Empty();
                    return;
                }

            }

            FCombatArt a;
            a.style = ECombatArt(STYLE);
            a.icon = FName(*c[2]);
            a.color = FLinearColor(v[0], v[1], v[2]);
            a.core = FLinearColor(v[3], v[4], v[5]);
            a.life = FMath::Clamp(v[6], .2f, 1.5f);
            a.width = FMath::Clamp(v[7], .25f, 3.f);
            a.power = FMath::Clamp(v[8], .5f, 6.f);
            this->rows.Add(ID, a);
        }

        this->ready = this->rows.Num() == 17;
        if (!this->ready)
        {
            UE_LOG(LogTemp, Error, TEXT("Combat presentation table requires all 17 abilities"));
        }

    }

};

const FArtCatalog& Catalog()
{
    static const FArtCatalog VALUE;
    return VALUE;
}

}

const TMap<uint32, FCombatArt>& RPGCombatArt::All()
{
    return Catalog().rows;
}

bool RPGCombatArt::CatalogReady()
{
    return Catalog().ready;
}

const FCombatArt& RPGCombatArt::Get(uint32 skill)
{
    static const FCombatArt FALLBACK;
    if (const auto* row = Catalog().rows.Find(skill))
    {
        return *row;
    }

    return FALLBACK;
}

