// Fill out your copyright notice in the Description page of Project Settings.

#include "KeyboardVirtualStick.h"

FFighterStickPosition FKeyboardVirtualStick::Update(const FFighterStickPosition& Target, float DeltaTime)
{
	Position.Roll  = Step(Position.Roll,  Target.Roll,  DeltaTime, MoveRate, ReturnRate);
	Position.Pitch = Step(Position.Pitch, Target.Pitch, DeltaTime, MoveRate, ReturnRate);
	Position.Yaw   = Step(Position.Yaw,   Target.Yaw,   DeltaTime, MoveRate, ReturnRate);
	return Position;
}

float FKeyboardVirtualStick::Step(float Current, float Target, float DeltaTime, float MoveRate, float ReturnRate)
{
	Target = FMath::Clamp(Target, -1.f, 1.f);

	// 松键时用回中速率，按住时用推出速率
	const float Rate = FMath::IsNearlyZero(Target) ? ReturnRate : MoveRate;

	if (Rate <= 0.f)
	{
		return Target;
	}

	// 恒速逼近 → 线性推杆手感（比 FInterpTo 的指数逼近更"线性"），且不会过冲
	return FMath::FInterpConstantTo(Current, Target, DeltaTime, Rate);
}
