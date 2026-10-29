// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FighterAxisResponse.generated.h"

/**
 * 单轴手感曲线。
 *
 * 处理顺序：灵敏度 → 限幅到 ±1 → 死区 → 指数曲线。
 *
 * 注意：鼠标只用来控制视角，不参与飞机机动，
 * 所以这套曲线只作用在"键盘虚拟杆"（已经是 −1..1 的模拟量）上。
 */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterAxisResponse
{
	GENERATED_BODY()

	/** 死区（0..1）。只对已经归一化到 ±1 的输入有意义。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DeadZone = 0.02f;

	/** 非线性指数。1 = 线性，越大中心越柔。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float Exponent = 1.5f;

	/** 灵敏度。对鼠标这种"像素增量"量级的输入靠它归一化。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Sensitivity = 1.0f;

	/** 完整处理：灵敏度 → 限幅 → 死区 → 曲线 */
	float Apply(float Raw) const;

	/** 只做死区 + 曲线（输入已经归一化到 ±1） */
	float Shape(float Normalized) const;
};
