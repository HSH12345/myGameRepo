using System;
using System.IO;
using UnrealBuildTool;

public class RPG : ModuleRules
{
    public RPG(ReadOnlyTargetRules target) : base(target)
    {
        this.PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        this.bEnableExceptions = true;
        if (target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("NOMINMAX=");
        }

        AddFirebaseDependencies();
        string sharedRoot = Environment.GetEnvironmentVariable("RPG_SHARED_WORLD_ROOT");
        string sharedRootConfig = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Config/SharedWorldRoot.txt"));
        if (String.IsNullOrWhiteSpace(sharedRoot) && File.Exists(sharedRootConfig))
        {
            sharedRoot = File.ReadAllText(sharedRootConfig).Trim();
        }

        if (String.IsNullOrWhiteSpace(sharedRoot) || !File.Exists(Path.Combine(sharedRoot, "Core/CoordinateSystem.h")))
        {
            throw new BuildException("Set RPG_SHARED_WORLD_ROOT or Config/SharedWorldRoot.txt to the server SharedWorld directory.");
        }

        // SharedWorld is editable project code. System includes are assumed
        // stable by dependency scanning and can leave old inline world logic.
        PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(sharedRoot, "..")));
        if (target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(new string[] { "ws2_32.lib", "wldap32.lib", "crypt32.lib", "userenv.lib", "iphlpapi.lib", "kernel32.lib", "psapi.lib", "schannel.lib", "advapi32.lib", });
        }

        this.PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Sockets", "Networking", "HTTP", "Json", "JsonUtilities" });
        // Native combat art uses bounded procedural ribbons and pooled screen-space damage widgets.
        this.PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "Niagara", "MoviePlayer", "RenderCore", "RHI", "ProceduralMeshComponent" });
        if (target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "NavigationSystem", "Landscape" });
        }

        this.PublicIncludePaths.AddRange(new string[] { "RPG/Packet" });
    }

    private void AddFirebaseDependencies()
    {
        string projectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        string firebasePath = Path.Combine(projectRoot, "Content", "Firebase");
        PublicIncludePaths.Add(Path.Combine(firebasePath, "include"));
        if (this.Target.Platform == UnrealTargetPlatform.Android)
        {
            string firebaseLibPath = Path.Combine(firebasePath, "libs", "android", "arm64-v8a");
            foreach (string library in new[]
            {
                "analytics",
                "app",
                "auth"
            }

            )
            {
                string libraryPath = Path.Combine(firebaseLibPath, "libfirebase_" + library + ".a");
                if (!File.Exists(libraryPath))
                {
                    throw new BuildException($"Missing Firebase Android arm64 library: {libraryPath}");
                }

                PublicAdditionalLibraries.Add(libraryPath);
            }

            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModuleDirectory, "Firebase_UPL.xml"));
        }
        else if (this.Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("FIREBASE_STATIC_LIBRARY=0");
            string configFolder = (Target.Configuration == UnrealTargetConfiguration.Debug) ? "Debug" : "Release";
            string firebaseLibPath = Path.Combine(firebasePath, "libs", "windows", "VS2019", "MD", "x64", configFolder);
            if (!Directory.Exists(firebaseLibPath))
            {
                throw new BuildException($"***Not Win64 Firebase Exist Path: {firebaseLibPath}");
            }

            this.PublicAdditionalLibraries.AddRange(new string[] { Path.Combine(firebaseLibPath, "firebase_analytics.lib"), Path.Combine(firebaseLibPath, "firebase_app.lib"), Path.Combine(firebaseLibPath, "firebase_auth.lib"), });
        }

    }

}

