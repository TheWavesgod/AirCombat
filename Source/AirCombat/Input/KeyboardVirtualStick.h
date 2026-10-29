// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/** 三个杆位（滚转 / 俯仰 / 方向舵），归一化到 −1..1 */
struct FFighterStickPosition
{
	float Roll = 0.f;
	float Pitch = 0.f;
	float Yaw = 0.f;
};

/**
 * 键盘虚拟杆。
 *
 * 键盘是**数字量**：按下去就是满杆，直接喂给飞机会"啪"地一下满舵。
 * 这里把数字量变成"慢慢推杆"：按住的时候以 MoveRate 逼近目标，
 * 松手后以 ReturnRate 回中。没有鼠标操纵杆也能飞出连续手感。
 *
 * 它只处理**逻辑数值**：
 *  - 不认识 EKeys::W / EKeys::S
 *  - 不认识 UInputAction / UInputMappingContext
 *  - 不认识 PlayerController / JSBSim
 *
 * 速率与原实现完全一致：
 *  - MoveRate  默认 3.0（杆位/秒）≈ 0.33 s 推满
 *  - ReturnRate 默认 6.0（杆位/秒），比推出快，手感更"跟手"
 *  - 逼近用 FInterpConstantTo（恒速），不会过冲
 */
class AIRCOMBAT_API FKeyboardVirtualStick
{
public:
	FKeyboardVirtualStick() = default;
	FKeyboardVirtualStick(float InMoveRate, float InReturnRate)
	{
		SetRates(InMoveRate, InReturnRate);
	}

	/** 设置推出 / 回中速率（杆位/秒） */
	void SetRates(float InMoveRate, float InReturnRate)
	{
		MoveRate = InMoveRate;
		ReturnRate = InReturnRate;
	}

	float GetMoveRate() const { return MoveRate; }
	float GetReturnRate() const { return ReturnRate; }

	/**
	 * 推进一帧。
	 *
	 * @param Target    目标杆位（键盘读出来的数字量，−1..1）
	 * @param DeltaTime 帧时间
	 * @return 推进后的杆位
	 */
	FFighterStickPosition Update(const FFighterStickPosition& Target, float DeltaTime);

	/** 清空杆位（失去控制权 / 换飞机时用） */
	void Reset()
	{
		Position = FFighterStickPosition();
	}

	const FFighterStickPosition& GetPosition() const { return Position; }

private:
	/** 推出速率（杆位/秒） */
	float MoveRate = 3.f;

	/** 松键回中速率（杆位/秒） */
	float ReturnRate = 6.f;

	FFighterStickPosition Position;

	/** 单轴推进：恒速逼近目标 */
	static float Step(float Current, float Target, float DeltaTime, float MoveRate, float ReturnRate);
};
