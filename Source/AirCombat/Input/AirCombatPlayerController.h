// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "FighterAxisResponse.h"
#include "FighterControlCommand.h"
#include "KeyboardVirtualStick.h"
#include "Input/Debug/FighterInputDebugTypes.h"
#include "Debug/Flight/FighterFlightDebugSnapshot.h"
#include "AirCombatPlayerController.generated.h"

class AFighter;
class UFighterInputConfig;
class UFighterInputDebugWidget;
class UFighterFlightDebugWidget;
class UInputAction;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;

/**
 * 飞行输入的**拥有者**。
 *
 * ## 分工
 *
 * ```
 * 键盘 / 鼠标
 *    ↓
 * IMC 资产（键位表、修饰器、触发器 —— 全在编辑器里）
 *    ↓
 * IA 资产（值类型、合并方式）
 *    ↓
 * 【本类】Enhanced Input 绑定 → 每帧读值 → 虚拟杆 → FFighterControlCommand
 *    ↓
 * AFighter::ApplyPilotControls()
 *    ↓
 * FFighterCommandProcessor → JSBSim
 * ```
 *
 * 物理键位**只**存在于 IMC 资产里，C++ 里一个键名都没有。
 * 动作对象通过 `UFighterInputConfig`（Data Asset）直接引用，
 * 运行时**不做任何按名字的查找**。
 *
 * ## 帧序
 *
 * `PlayerTick` 里先调 `Super::PlayerTick()` —— 引擎在这一步做
 * `TickPlayerInput → ProcessInputStack`，Enhanced Input 的 value binding
 * 就是在那里刷新的。所以 Super 之后读到的就是**本帧**的值，
 * 不需要任何"延迟一帧"的补偿。
 */
UCLASS()
class AIRCOMBAT_API AAirCombatPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAirCombatPlayerController();

	// ---- Enhanced Input 生命周期 ----

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void PlayerTick(float DeltaTime) override;

	// ------------------------------------------------------------------
	// 配置
	// ------------------------------------------------------------------

	/**
	 * 输入配置资产（`DA_FighterInputConfig`）。
	 *
	 * 在 **BP_AirCombatPlayerController** 里指定。
	 * 由 UPROPERTY 持有 → GC 安全。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UFighterInputConfig> InputConfig;

	/** 飞行 IMC 的优先级（同一个 LocalPlayer 上多个 IMC 时用） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 FlightMappingPriority = 0;

	// ------------------------------------------------------------------
	// 手感
	//
	// 分工：**鼠标只控制视角，不参与飞机机动。**
	// 键盘是数字量，所以下面加了"键盘虚拟杆"的速率限制，
	// 把按 A/D/W/S 变成"慢慢推杆"，手感接近真操纵杆。
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response")
	FFighterAxisResponse RollKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response")
	FFighterAxisResponse PitchKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response")
	FFighterAxisResponse YawKey;

	/** 键盘虚拟杆推出速率（杆位/秒），3.0 ≈ 0.33 s 推满 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response", meta = (ClampMin = "0.0"))
	float KeyAxisMoveRate = 3.f;

	/** 键盘虚拟杆松键回中速率（杆位/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response", meta = (ClampMin = "0.0"))
	float KeyAxisReturnRate = 6.f;

	/**
	 * 反转俯仰。
	 *
	 * 约定：正值 = 拉杆抬头，键盘 W = 抬头（正值）。
	 * 实机方向相反时勾这个开关即可，不用改 C++。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Response")
	bool bInvertPitch = false;

	// ------------------------------------------------------------------
	// 调试面板
	//
	// 把四级输入数据流（RAW → STICK → RESP → CMD）摊开显示在屏幕上。
	// 面板本体是 UMG（`UFighterInputDebugWidget`），C++ 自建 widget tree。
	// ------------------------------------------------------------------

	/**
	 * 调试面板的类。
	 *
	 * 留空就用 C++ 默认类（`UFighterInputDebugWidget`），照样能跑。
	 * 想换皮就做一个它的蓝图子类，只改类默认值里的字体 / 配色。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Debug")
	TSubclassOf<UFighterInputDebugWidget> InputDebugWidgetClass;

	/** 调试面板是否显示。也可以用控制台命令 `AirCombat.Input.Debug` 切。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Debug")
	bool bShowInputDebug = false;

	/** 显示 / 隐藏调试面板 */
	UFUNCTION(BlueprintCallable, Category = "Input|Debug")
	void SetInputDebugVisible(bool bVisible);

	/** 调试面板当前是否显示 */
	UFUNCTION(BlueprintPure, Category = "Input|Debug")
	bool IsInputDebugVisible() const { return bShowInputDebug; }

	/**
	 * 采集一帧的输入调试数据。
	 *
	 * 面板每帧调一次，把四级数据流 + 配置项 + 飞机状态填进快照。
	 */
	void BuildInputDebugSnapshot(FFighterInputDebugSnapshot& OutSnapshot);

    /** Independent passive flight monitor; never changes input-debug or JSBSim DrawDebug settings. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight|Debug")
    TSubclassOf<UFighterFlightDebugWidget> FlightDebugWidgetClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight|Debug") bool bShowFlightDebug = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight|Debug") EFighterFlightDebugPage FlightDebugPage = EFighterFlightDebugPage::Overview;
    UFUNCTION(BlueprintCallable, Category="Flight|Debug") void SetFlightDebugVisible(bool bVisible);
    UFUNCTION(BlueprintCallable, Category="Flight|Debug") void SetFlightDebugPage(EFighterFlightDebugPage Page);
    UFUNCTION(BlueprintPure, Category="Flight|Debug") bool IsFlightDebugVisible() const { return bShowFlightDebug; }
    void BuildFlightDebugSnapshot(FFighterFlightDebugSnapshot& OutSnapshot);

private:
	/** 键盘虚拟杆（滚转 / 俯仰 / 方向舵） */
	FKeyboardVirtualStick Stick;

	/** 本帧构造好的操纵指令 */
	FFighterControlCommand Command;

	/** 动作绑定是否已经建过（避免重复绑定） */
	bool bBindingsInitialized = false;

	/** IMC 是否已经登记到 Enhanced Input User Settings */
	bool bRegisteredWithUserSettings = false;

	/** 当前挂着的调试面板（没有时为空） */
	UPROPERTY(Transient)
	TObjectPtr<UFighterInputDebugWidget> InputDebugWidget;
    UPROPERTY(Transient) TObjectPtr<UFighterFlightDebugWidget> FlightDebugWidget;
    void CreateFlightDebugWidget();
    void DestroyFlightDebugWidget();

	// ---- 调试面板生命周期 ----

	/** 建调试面板并挂到视口 */
	void CreateInputDebugWidget();

	/** 从视口摘掉调试面板 */
	void DestroyInputDebugWidget();

	/** 把 Slate 键盘焦点交还给游戏视口（面板别把玩家的键盘输入截胡） */
	void RestoreGameViewportKeyboardFocus();

	// ---- IMC 生命周期 ----

	/** 把飞行 IMC 挂到 LocalPlayer 上（激活实际输入） */
	void RegisterFlightMapping();

	/** 把飞行 IMC 从 LocalPlayer 上摘掉（停用实际输入） */
	void UnregisterFlightMapping();

	/** 把飞行 IMC 登记到 Enhanced Input User Settings（供未来的重绑定 UI 枚举） */
	void RegisterMappingContextWithUserSettings();

	/** 清空所有本地输入状态（换飞机 / 失去控制权时调用） */
	void ResetInputState();

	/** 启动时校验配置，把问题一条条打出来 */
	void ValidateInputConfiguration() const;

	/** 建 value binding 和一次性开关的事件绑定。幂等。 */
	void InitializeActionBindings();

	// ---- 工具 ----

	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	/** 当前操控的飞机。**不缓存** —— Pawn 随时可能换掉。 */
	AFighter* GetFighter() const;

	/** 读一个连续量动作的值（必须是建过 value binding 的动作） */
	static float ReadAxis(const UEnhancedInputComponent* EnhancedInput, const UInputAction* Action);

	/** 读一个布尔量动作的值，转成 0 / 1 */
	static float ReadDigital(const UEnhancedInputComponent* EnhancedInput, const UInputAction* Action);

	// ---- 一次性开关的事件回调 ----

	UFUNCTION()
	void OnGearToggled(const FInputActionValue& Value);

	UFUNCTION()
	void OnCameraToggled(const FInputActionValue& Value);

	UFUNCTION()
	void OnResetTrim(const FInputActionValue& Value);
};
