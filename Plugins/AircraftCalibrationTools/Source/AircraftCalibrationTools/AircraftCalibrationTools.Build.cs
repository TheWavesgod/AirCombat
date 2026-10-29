using UnrealBuildTool;
public class AircraftCalibrationTools : ModuleRules
{
    public AircraftCalibrationTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.Add("Json");
    }
}
