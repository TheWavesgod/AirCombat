// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "JSBSimMovementComponent.h"
#include "FighterTypes.h"
#include "FighterCommandProcessor.h"
#include "Input/FighterControlCommand.h"
#include "Fighter.generated.h"

class UFighterAnimationBindings;
class UFighterPresentationComponent;
class UCameraComponent;
class USpringArmComponent;
class UFighterCameraControllerComponent;
class USkeletalMeshComponent;

/**
 * 一架飞机。
 *
 * 设计要点：
 *  - 基类是 APawn（不是 ACharacter / AVehicle）：JSBSim 是唯一的动力学权威，
 *    Pawn 不带任何 MovementComponent，不会跟插件抢 Transform。
 *  - 所有组件都在 C++ 里 CreateDefaultSubobject，蓝图只改属性、不加组件，
 *    避免 BP 组件和 C++ 组件重名冲突。
 *  - Actor 原点与导入后的 Mesh 原点一致，轴向和尺寸由模型导入保证。
 *    StructuralFrameOrigin 是唯一的结构原点平移标定；动态重心仅作参考标记。
 */
UCLASS()
class AIRCOMBAT_API AFighter : public APawn
{
	GENERATED_BODY()

public:
	AFighter();

	virtual void Tick(float DeltaTime) override;

	/** 起始模式必须在这里写：插件的地面配平跑在它自己组件的 BeginPlay 里 */
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;

	/**
	 * 失去控制权。
	 *
	 * ⚠ UE 5.8 的 `APawn::UnPossessed()` 是**无参**的（带 `AController*` 的是
	 * `AController::UnPossessed`）。旧控制器通过 `GetController()` 拿。
	 *
	 * 这里只做一件事：把还没消费完的操纵指令清零，
	 * 免得飞机在没人操纵的时候还保持着最后的杆量。
	 */
	virtual void UnPossessed() override;

	// ------------------------------------------------------------------
	// 操纵接口（由 AAirCombatPlayerController 调用）
	// ------------------------------------------------------------------

	/**
	 * 接收飞行员操纵指令。
	 *
	 * @param Command 逻辑操纵量（杆量 / 配平 / 构型 / 油门意图）
	 *
	 * 这里只存储，在同帧 JSBSim 仿真前写入 FDM 指令。
	 */
	void ApplyPilotControls(const FFighterControlCommand& Command);

	/** Configure a deferred spawn before PostInitializeComponents; does not change asset defaults. */
	void ConfigureStartState(EFighterStartMode InStartMode, float InAirStartSpeedKts);

	UJSBSimMovementComponent* GetFlightDynamics() const { return FlightDynamics; }
	UFighterAnimationBindings* GetAnimationBindings() const { return AnimationBindings; }
    EFighterStartMode GetStartMode() const { return StartMode; }

	/** 收放起落架（一次性） */
	void ToggleGear();

	/** 本地观察接口，与飞行控制指令分离。 */
	UFighterCameraControllerComponent* GetCameraController() const { return CameraController; }

	/** 复位配平（一次性） */
	void ResetTrim();

	// ---- 只读状态（HUD / 调试用） ----
	UFUNCTION(BlueprintPure, Category = "Fighter")
	float GetThrottle() const { return CommandProcessor.GetThrottle(); }

	UFUNCTION(BlueprintPure, Category = "Fighter")
	float GetAltitudeFt() const;

	UFUNCTION(BlueprintPure, Category = "Fighter")
	bool  IsGearDown() const { return CommandProcessor.IsGearDown(); }

	UFUNCTION(BlueprintPure, Category = "Fighter")
	bool  IsAfterburner() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---------------- 组件 ----------------

	/** Pawn 根节点。JSBSim 每帧写它的世界变换。必须是纯 SceneComponent。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> AircraftRoot;

	/** 插件组件：动力学权威，独占一个 FGFDMExec */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UJSBSimMovementComponent> FlightDynamics;

	/** Dynamic physical CG marker in actor coordinates; never a parent of visual geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> AircraftCG;

    /** Transient interpolated actor/mesh pose. Never stores an authored calibration offset. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<USceneComponent> PresentationRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
    TObjectPtr<UFighterPresentationComponent> Presentation;

	/**
	 * 机体：骨架网格（模型 + 骨骼 + Animation BP）。
	 *
	 * 蓝图只配置资产；局部变换保持单位变换，轴向和尺寸在导入时处理。
	 * 操纵面不是独立的网格组件，而是**骨骼**，由 UFighterAnimInstance
	 * 把 JSBSim 算出的真实偏角喂给 AnimBP 去驱动。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> Airframe;

	/** Authored pilot eye pose in mesh coordinates. Independent of the physical EYEPOINT. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> CockpitAnchor;

	/** Authored chase focus in mesh coordinates; SpringArm supplies distance/lag/collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> ChaseFocus;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFighterCameraControllerComponent> CameraController;

	/** chase camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> ChaseCamera;

	/** cockpit camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> CockpitCamera;

	/** Simulation-model channels only; skeletal bone mappings remain in AnimBP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UFighterAnimationBindings> AnimationBindings;

	// ---------------- config ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter")
	EFighterStartMode StartMode = EFighterStartMode::Ground;

	/** 空中起始时的配平速度（节） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter", meta = (ClampMin = "0.0"))
	float AirStartSpeedKts = 250.f;

	// ---- 控制指令变化率 ----


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01"))
	float ThrottleRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01"))
	float TrimRate = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01"))
	float FlapRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "0.01"))
	float SpeedBrakeRate = 0.5f;

private:
	/** Shares the display pose and keeps imported mesh space identical to actor space. */
	void AttachPresentationComponents();

	FFighterCommandProcessor CommandProcessor;

	/**
	 * 最近一次收到的操纵指令。
	 *
	 * 由 `ApplyPilotControls()` 写入，在 `PrepareFlightControls()` 里消费。
	 */
	FFighterControlCommand PendingCommand;

	/** Called inside the FDM tick, immediately before commands are copied to JSBSim. */
	void PrepareFlightControls(float DeltaTime);

};
