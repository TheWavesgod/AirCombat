// Fill out your copyright notice in the Description page of Project Settings.

#include "FighterInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"

void UFighterInputConfig::CollectProblems(TArray<FString>& OutProblems) const
{
	auto CheckAction = [&OutProblems](const TCHAR* PropertyName, const UInputAction* Action, EInputActionValueType Expected)
	{
		if (!Action)
		{
			OutProblems.Add(FString::Printf(TEXT("%s: 未指定资产"), PropertyName));
			return;
		}

		if (Action->ValueType != Expected)
		{
			OutProblems.Add(FString::Printf(
				TEXT("%s: 动作 '%s' 的值类型应为 %s，实际是 %s"),
				PropertyName,
				*Action->GetName(),
				*UEnum::GetValueAsString(Expected),
				*UEnum::GetValueAsString(Action->ValueType)));
		}
	};

	// ---- 映射上下文 ----
	if (!FlightKeyboardContext)
	{
		OutProblems.Add(TEXT("FlightKeyboardContext: IMC asset not available"));
	}

	// ---- 动作引用 + 值类型 ----
	CheckAction(TEXT("CameraYawAction"),   CameraYawAction,   EInputActionValueType::Axis1D);
	CheckAction(TEXT("CameraPitchAction"), CameraPitchAction, EInputActionValueType::Axis1D);

	CheckAction(TEXT("PitchAction"), PitchAction, EInputActionValueType::Axis1D);
	CheckAction(TEXT("RollAction"),  RollAction,  EInputActionValueType::Axis1D);
	CheckAction(TEXT("YawAction"),   YawAction,   EInputActionValueType::Axis1D);

	CheckAction(TEXT("TrimPitchAction"), TrimPitchAction, EInputActionValueType::Axis1D);
	CheckAction(TEXT("TrimYawAction"),   TrimYawAction,   EInputActionValueType::Axis1D);
	CheckAction(TEXT("TrimRollAction"),  TrimRollAction,  EInputActionValueType::Axis1D);

	CheckAction(TEXT("FlapAction"), FlapAction, EInputActionValueType::Axis1D);

	CheckAction(TEXT("ThrottleUpAction"),   ThrottleUpAction,   EInputActionValueType::Boolean);
	CheckAction(TEXT("ThrottleDownAction"), ThrottleDownAction, EInputActionValueType::Boolean);
	CheckAction(TEXT("SpeedBrakeAction"),   SpeedBrakeAction,   EInputActionValueType::Boolean);
	CheckAction(TEXT("BrakeAction"),        BrakeAction,        EInputActionValueType::Boolean);

	CheckAction(TEXT("GearAction"),      GearAction,      EInputActionValueType::Boolean);
	CheckAction(TEXT("CameraAction"),    CameraAction,    EInputActionValueType::Boolean);
	CheckAction(TEXT("ResetTrimAction"), ResetTrimAction, EInputActionValueType::Boolean);

	// ---- IMC 里必须真的有这些动作的键位 ----
	if (!FlightKeyboardContext)
	{
		return;
	}

	TArray<const UInputAction*> MappedActions;
	MappedActions.Reserve(16);
	for (const FEnhancedActionKeyMapping& Mapping : FlightKeyboardContext->GetMappings())
	{
		if (Mapping.Action)
		{
			MappedActions.Add(Mapping.Action.Get());
		}
	}

	auto CheckMapped = [&OutProblems, &MappedActions](const TCHAR* PropertyName, const UInputAction* Action)
	{
		if (!Action)
		{
			return;   // 上面已经报过了
		}

		if (!MappedActions.Contains(Action))
		{
			OutProblems.Add(FString::Printf(
				TEXT("%s: IMC '%s' 里没有 '%s' 的键位"),
				PropertyName,
				TEXT("FlightKeyboardContext"),
				*Action->GetName()));
		}
	};

	CheckMapped(TEXT("CameraYawAction"),   CameraYawAction);
	CheckMapped(TEXT("CameraPitchAction"), CameraPitchAction);
	CheckMapped(TEXT("PitchAction"), PitchAction);
	CheckMapped(TEXT("RollAction"),  RollAction);
	CheckMapped(TEXT("YawAction"),   YawAction);
	CheckMapped(TEXT("TrimPitchAction"), TrimPitchAction);
	CheckMapped(TEXT("TrimYawAction"),   TrimYawAction);
	CheckMapped(TEXT("TrimRollAction"),  TrimRollAction);
	CheckMapped(TEXT("FlapAction"), FlapAction);
	CheckMapped(TEXT("ThrottleUpAction"),   ThrottleUpAction);
	CheckMapped(TEXT("ThrottleDownAction"), ThrottleDownAction);
	CheckMapped(TEXT("SpeedBrakeAction"),   SpeedBrakeAction);
	CheckMapped(TEXT("BrakeAction"),        BrakeAction);
	CheckMapped(TEXT("GearAction"),      GearAction);
	CheckMapped(TEXT("CameraAction"),    CameraAction);
	CheckMapped(TEXT("ResetTrimAction"), ResetTrimAction);
}
