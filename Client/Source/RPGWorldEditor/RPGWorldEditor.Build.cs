using UnrealBuildTool;

public class RPGWorldEditor : ModuleRules
{
    public RPGWorldEditor(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bEnableExceptions = true;
        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "RPG", "UMG", "UMGEditor", "SlateCore", "AssetRegistry" });
    }

}

