// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AirCombat : ModuleRules
{
	public AirCombat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "JSBSimFlightDynamicsModel", "UMG", "Slate", "SlateCore" });

		// 这个模块没有 Public / Private 分目录，模块根目录本身要能当 include 路径用，
		// 这样跨目录引用（例如 "Input/FighterControlCommand.h"）才找得到。
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
