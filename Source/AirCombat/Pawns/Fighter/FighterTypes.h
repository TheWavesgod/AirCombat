// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FighterTypes.generated.h"

/** 飞机的起始状态 */
UENUM(BlueprintType)
enum class EFighterStartMode : uint8
{
	/** 跑道起始：起落架放下、发动机运转、地面配平 */
	Ground UMETA(DisplayName = "地面起始（跑道）"),
	/** 空中起始：全状态配平（tFull） */
	Air    UMETA(DisplayName = "空中起始"),
};

/** 相机视角 */
UENUM(BlueprintType)
enum class EFighterCameraMode : uint8
{
	Chase   UMETA(DisplayName = "追尾视角"),
	Cockpit UMETA(DisplayName = "座舱视角"),
};
