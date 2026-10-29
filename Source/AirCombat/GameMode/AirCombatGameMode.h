// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AirCombatGameMode.generated.h"

/**
 * 飞行模式的 GameMode。
 *
 * 唯一职责是把 **AAirCombatPlayerController** 设成默认控制器 ——
 * 飞行输入（IMC 注册、Enhanced Input 绑定、虚拟杆）全部由它拥有。
 *
 * 在 DefaultEngine.ini 里通过 `GlobalDefaultGameMode` 指向本类。
 */
UCLASS()
class AIRCOMBAT_API AAirCombatGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAirCombatGameMode();
};
