// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/FighterControlCommand.h"

class UJSBSimMovementComponent;

/**
 * 有状态的飞行员指令处理器。
 *
 * 玩家杆量直接进入 JSBSim；F-16 模型拥有 FBW / 方向舵反馈。
 * 本层负责向 FDM 提交指令，以及油门、手动配平偏置、构型的状态和速率限制。
 * 初始化配平求解由 FDM 完成；持续飞控反馈由机型 JSBSim 模型实现。
 */
class AIRCOMBAT_API FFighterCommandProcessor
{
public:
	/**
	 * 每帧调用：把操纵意图翻译成 JSBSim 指令。
	 *
	 * @param DeltaTime  帧时间
	 * @param Command    飞行员操纵意图（逻辑量）
	 * @param FDM        JSBSim 组件
	 */
	void Update(float DeltaTime, const FFighterControlCommand& Command, UJSBSimMovementComponent* FDM);

	/** 请求收放起落架（一次性，在下一次 Update 里生效） */
	void RequestGearToggle()
	{
		bGearToggleRequested = true;
	}

	/** 请求将三轴手动配平命令清零（不重新执行 FDM 初始配平） */
	void RequestResetTrim()
	{
		bResetTrimRequested = true;
	}

	// ---- 可调参数（由 AFighter 的 UPROPERTY 传进来） ----

	/** 油门变化率（每秒） */
	float ThrottleRate = 0.5f;
	/** 配平变化率（每秒） */
	float TrimRate = 0.25f;
	/** 襟翼 / 减速板 变化率（每秒） */
	float FlapRate = 0.5f;
	float SpeedBrakeRate = 0.5f;

	// ---- 当前状态（HUD / 调试读） ----
	float GetThrottle() const { return Throttle; }
	bool  IsGearDown() const { return bGearDown; }

private:
	/** 是否已经从 FDM 同步过初值（配平结果） */
	bool bSynced = false;

	float Throttle = 0.f;
	float PitchTrim = 0.f;
	float YawTrim = 0.f;
	float RollTrim = 0.f;
	bool  bGearDown = true;
	float Flap = 0.f;
	float SpeedBrake = 0.f;

	/** 一次性开关请求（在 Update 里消费后清零） */
	bool bGearToggleRequested = false;
	bool bResetTrimRequested = false;
};
