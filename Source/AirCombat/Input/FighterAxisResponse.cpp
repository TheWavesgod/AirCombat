// Fill out your copyright notice in the Description page of Project Settings.

#include "FighterAxisResponse.h"

float FFighterAxisResponse::Shape(float Normalized) const
{
	const float V = FMath::Clamp(Normalized, -1.f, 1.f);
	const float Abs = FMath::Abs(V);
	if (Abs <= DeadZone)
	{
		return 0.f;
	}

	// 死区外的区间重新映射回 0..1，保证 DeadZone 处连续
	const float Scaled = (Abs - DeadZone) / FMath::Max(1.f - DeadZone, KINDA_SMALL_NUMBER);
	const float Shaped = FMath::Pow(Scaled, Exponent);
	return FMath::Sign(V) * Shaped;
}

float FFighterAxisResponse::Apply(float Raw) const
{
	return Shape(Raw * Sensitivity);
}
