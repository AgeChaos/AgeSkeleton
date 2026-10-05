using UnrealBuildTool;
using System.IO;
public class AgeSkeleton : ModuleRules {
    public AgeSkeleton(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;CppStandard=CppStandardVersion.Cpp17;
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","ProceduralMeshComponent"});
        string Shared=Path.Combine(PluginDirectory,"Source","ThirdParty","AgeSkeleton","include");
        if(!Directory.Exists(Shared)) Shared=Path.GetFullPath(Path.Combine(PluginDirectory,"..","..","Native","include"));
        PublicIncludePaths.Add(Shared);
    }
}
