// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputActionValue.h"
#include "FighterInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 飞行输入配置 —— **只是一组显式的资产引用**。
 *
 * 这个 Data Asset 里**只有**"哪个动作对应哪个功能"，
 * **不存**任何：
 *  - 默认物理键位
 *  - 正负方向 / Negate 修饰器
 *  - 值类型
 *  - 触发器
 *  - 合并（Accumulation）方式
 *
 * 上面这些**全部**属于 IA / IMC 资产，在编辑器里改。
 * C++ 通过 `TObjectPtr<UInputAction>` 直接持有动作对象，
 * 运行时**不做任何按名字的查找**。
 *
 * 资产本体：`DA_FighterInputConfig`（/Game/Blueprints/Input）。
 */
UCLASS(BlueprintType)
class AIRCOMBAT_API UFighterInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------
	// 映射上下文
	// ------------------------------------------------------------------

	/** 飞行键盘 IMC。运行时由 PlayerController 挂到 LocalPlayer 上。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> FlightKeyboardContext;

	// ------------------------------------------------------------------
	// 视角（鼠标，只转视角不参与机动）
	// ------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraYawAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraPitchAction;

	// ------------------------------------------------------------------
	// 操纵轴（Axis1D，正负方向在 IMC 里用 Negate 修饰器表达）
	// ------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Flight")
	TObjectPtr<UInputAction> PitchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Flight")
	TObjectPtr<UInputAction> RollAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Flight")
	TObjectPtr<UInputAction> YawAction;

	// ------------------------------------------------------------------
	// 配平
	// ------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Trim")
	TObjectPtr<UInputAction> TrimPitchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Trim")
	TObjectPtr<UInputAction> TrimYawAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Trim")
	TObjectPtr<UInputAction> TrimRollAction;

	// ------------------------------------------------------------------
	// 构型 / 油门
	// ------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Configuration")
	TObjectPtr<UInputAction> FlapAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Configuration")
	TObjectPtr<UInputAction> ThrottleUpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Configuration")
	TObjectPtr<UInputAction> ThrottleDownAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Configuration")
	TObjectPtr<UInputAction> SpeedBrakeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Configuration")
	TObjectPtr<UInputAction> BrakeAction;

	// ------------------------------------------------------------------
	// 一次性开关
	// ------------------------------------------------------------------

	/** 起落架收放（切换） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Switch")
	TObjectPtr<UInputAction> GearAction;

	/** 切换追尾 / 座舱视角 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Switch")
	TObjectPtr<UInputAction> CameraAction;

	/** 复位配平 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Switch")
	TObjectPtr<UInputAction> ResetTrimAction;

	// ------------------------------------------------------------------
	// 校验
	// ------------------------------------------------------------------

	/**
	 * 收集所有配置问题（缺资产、值类型不对、IMC 里没有对应键位）。
	 *
	 * 每一项都指明**具体是哪个属性 / 哪个动作**，方便直接在编辑器里改。
	 */
	void CollectProblems(TArray<FString>& OutProblems) const;
};
