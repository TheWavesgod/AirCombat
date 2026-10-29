// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FighterInputDebugTypes.generated.h"

/**
 * 单个操纵轴在**四级数据流**上的当前值。
 *
 * ```
 * IMC 键位 → IA 值 → 键盘虚拟杆 → 轴响应曲线 → 操纵指令
 *            RAW    STICK        RESP         CMD
 * ```
 *
 * 四列并排放，差值本身就是诊断信息：
 *  - `RESP − STICK` = 死区 + 指数曲线吃掉了多少量
 *  - `CMD  − RESP`  = 俯仰反转（`bInvertPitch`）有没有生效
 */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterInputDebugAxis
{
	GENERATED_BODY()

	/** 轴名（Roll / Pitch / Yaw） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	FString Name;

	/** ① RAW：Enhanced Input 合并后的原始值（键盘按下去就是 ±1） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Raw = 0.f;

	/** ② STICK：键盘虚拟杆当前杆位（速率受限，−1..1） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Stick = 0.f;

	/** ③ RESP：灵敏度 → 限幅 → 死区 → 指数曲线之后的结果（还没做俯仰反转） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Response = 0.f;

	/** ④ CMD：真正交给飞控层（`AFighter::ApplyPilotControls`）的操纵量 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Command = 0.f;
};

/**
 * 单轴手感曲线的当前参数（`FFighterAxisResponse` 的只读副本）。
 */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterInputDebugCurve
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float DeadZone = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Exponent = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Sensitivity = 1.f;
};

/**
 * 一帧的完整输入调试快照。
 *
 * 这是**输入层 → 显示层**的数据边界：里面只有已经解释过的数值，
 * 没有资产引用、没有物理键名、没有 Enhanced Input 的类型。
 */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterInputDebugSnapshot
{
	GENERATED_BODY()

	// ---- 四级数据流 ----

	/** Roll / Pitch / Yaw 三个操纵轴 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	TArray<FFighterInputDebugAxis> Axes;

	// ---- 配平 / 构型 / 油门 / 视角 ----

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float TrimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float TrimYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float TrimRoll = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Flap = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float SpeedBrake = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float Brake = 0.f;

	/** "加油门"键当前是否按下（0 / 1） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ThrottleUp = 0.f;

	/** "收油门"键当前是否按下（0 / 1） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ThrottleDown = 0.f;

	/** 油门增量意图（−1..1）。正 = 加油门。 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ThrottleDelta = 0.f;

	/** 绝对油门（由飞控层按速率保持的量） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ThrottleAbsolute = 0.f;

	/** 鼠标水平增量（像素/帧） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float MouseDeltaX = 0.f;

	/** 鼠标垂直增量（像素/帧） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float MouseDeltaY = 0.f;

	// ---- 配置 / 映射上下文 ----

	/** 飞行 IMC 的名字 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	FString IMCName;

	/** IMC 当前是否真的挂在 LocalPlayer 上 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	bool bIMCActive = false;

	/** 虚拟杆推出速率（属性值） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float MoveRateProperty = 0.f;

	/** 虚拟杆推出速率（虚拟杆实际在用的值） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float MoveRateEffective = 0.f;

	/** 虚拟杆回中速率（属性值） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ReturnRateProperty = 0.f;

	/** 虚拟杆回中速率（虚拟杆实际在用的值） */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float ReturnRateEffective = 0.f;

	/** 俯仰是否反转 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	bool bInvertPitch = false;

	/** 三条轴的手感曲线参数 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	FFighterInputDebugCurve RollCurve;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	FFighterInputDebugCurve PitchCurve;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	FFighterInputDebugCurve YawCurve;

	// ---- 飞机侧状态 ----

	/** 当前有没有操控着飞机 */
	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	bool bHasAircraft = false;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	bool bGearDown = false;

	UPROPERTY(BlueprintReadOnly, Category = "Input Debug")
	float AltitudeFt = 0.f;
};

/**
 * 把 `-0.0`（以及四舍五入后本来就是 0 的小值）归一化成 `+0.0`。
 *
 * `%+.2f` 会把 `-0.0` 打成 `"-0.00"` —— 看着像"有负值但又归零"，纯误导。
 * 面板和日志 dump 都走这个函数，两边显示保持一致。
 */
FORCEINLINE float FighterInputDebugNormalizeZero(float Value)
{
	return (FMath::Abs(Value) < 0.005f) ? 0.f : Value;
}

