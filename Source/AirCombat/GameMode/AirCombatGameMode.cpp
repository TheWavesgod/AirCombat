// Fill out your copyright notice in the Description page of Project Settings.

#include "AirCombatGameMode.h"

#include "Input/AirCombatPlayerController.h"
#include "Pawns/Fighter/Fighter.h"

AAirCombatGameMode::AAirCombatGameMode()
{
	// 飞行输入由 PlayerController 拥有。
	PlayerControllerClass = AAirCombatPlayerController::StaticClass();

	// 默认生成一架 AFighter。
	// 关卡里可以用 BP_F16A 之类的飞机蓝图覆盖（Default Pawn Class）。
	DefaultPawnClass = AFighter::StaticClass();
}
