// Fill out your copyright notice in the Description page of Project Settings.

#include "AirCombatPlayerController.h"
#include "Camera/FighterCameraControllerComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "FighterInputConfig.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "Input/Debug/FighterInputDebugWidget.h"
#include "Debug/Flight/FighterFlightDebugWidget.h"
#include "InputMappingContext.h"
#include "Pawns/Fighter/Fighter.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAirCombatInput, Log, All);

// ----------------------------------------------------------------------
// 控制台命令：AirCombat.Input.Debug
//
// 整个进程只注册一次（不是每个 PlayerController 注册一次）——
// 否则多窗口 PIE 时第二个 PC 注册同名命令会刷警告。
// 命令通过 World 找到第一个 PC，再派发过去。
// ----------------------------------------------------------------------

static void HandleInputDebugConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
	AAirCombatPlayerController* PC = World
		? Cast<AAirCombatPlayerController>(World->GetFirstPlayerController())
		: nullptr;

	if (!PC)
	{
		UE_LOG(LogAirCombatInput, Warning,
			TEXT("[Input Debug] 当前世界里没有 AirCombatPlayerController，面板没开。"));
		return;
	}

	bool bVisible = !PC->IsInputDebugVisible();

	if (Args.Num() > 0)
	{
		const FString& Arg = Args[0];

		if (Arg == TEXT("1") || Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			bVisible = true;
		}
		else if (Arg == TEXT("0") || Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			bVisible = false;
		}
	}

	PC->SetInputDebugVisible(bVisible);

	UE_LOG(LogAirCombatInput, Log,
		TEXT("[Input Debug] 调试面板 %s。"), bVisible ? TEXT("显示") : TEXT("隐藏"));
}

static FAutoConsoleCommandWithWorldAndArgs GInputDebugConsoleCommand(
	TEXT("AirCombat.Input.Debug"),
	TEXT("飞行输入调试面板开关。\n")
	TEXT("  AirCombat.Input.Debug      切换显示 / 隐藏\n")
	TEXT("  AirCombat.Input.Debug 1    显示\n")
	TEXT("  AirCombat.Input.Debug 0    隐藏"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleInputDebugConsoleCommand));

// ----------------------------------------------------------------------
// 控制台命令：AirCombat.Input.Dump
//
// 把当前这一帧的输入快照打到日志里。面板关着、或者不方便看屏幕时用。
// 和面板读的是**同一份快照**（BuildInputDebugSnapshot），
// 所以两边的数永远对得上。
// ----------------------------------------------------------------------

static void HandleInputDumpConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
	AAirCombatPlayerController* PC = World
		? Cast<AAirCombatPlayerController>(World->GetFirstPlayerController())
		: nullptr;

	if (!PC)
	{
		UE_LOG(LogAirCombatInput, Warning,
			TEXT("[Input Dump] 当前世界里没有 AirCombatPlayerController。"));
		return;
	}

	FFighterInputDebugSnapshot Snapshot;
	PC->BuildInputDebugSnapshot(Snapshot);

	FString Axes;
	for (const FFighterInputDebugAxis& Axis : Snapshot.Axes)
	{
		Axes += FString::Printf(TEXT("%s raw %+.2f stick %+.2f resp %+.2f cmd %+.2f | "),
			*Axis.Name,
			FighterInputDebugNormalizeZero(Axis.Raw),
			FighterInputDebugNormalizeZero(Axis.Stick),
			FighterInputDebugNormalizeZero(Axis.Response),
			FighterInputDebugNormalizeZero(Axis.Command));
	}

	UE_LOG(LogAirCombatInput, Log,
		TEXT("[Input Dump] %s"), *Axes);
	UE_LOG(LogAirCombatInput, Log,
		TEXT("[Input Dump] trim %+.2f/%+.2f/%+.2f  flap %+.2f  brake %+.2f  spdbrk %+.2f  thr %+.2f/%+.2f abs %+.2f  mouse %+.1f/%+.1f"),
		FighterInputDebugNormalizeZero(Snapshot.TrimPitch),
		FighterInputDebugNormalizeZero(Snapshot.TrimYaw),
		FighterInputDebugNormalizeZero(Snapshot.TrimRoll),
		FighterInputDebugNormalizeZero(Snapshot.Flap),
		FighterInputDebugNormalizeZero(Snapshot.Brake),
		FighterInputDebugNormalizeZero(Snapshot.SpeedBrake),
		FighterInputDebugNormalizeZero(Snapshot.ThrottleUp),
		FighterInputDebugNormalizeZero(Snapshot.ThrottleDown),
		FighterInputDebugNormalizeZero(Snapshot.ThrottleAbsolute),
		Snapshot.MouseDeltaX, Snapshot.MouseDeltaY);
	UE_LOG(LogAirCombatInput, Log,
		TEXT("[Input Dump] imc %s %s  rate %.2f/%.2f  return %.2f/%.2f  invertPitch %d  gear %d  alt %.0f"),
		*Snapshot.IMCName, Snapshot.bIMCActive ? TEXT("ACTIVE") : TEXT("INACTIVE"),
		Snapshot.MoveRateProperty, Snapshot.MoveRateEffective,
		Snapshot.ReturnRateProperty, Snapshot.ReturnRateEffective,
		Snapshot.bInvertPitch ? 1 : 0,
		Snapshot.bGearDown ? 1 : 0,
		Snapshot.AltitudeFt);
}

static FAutoConsoleCommandWithWorldAndArgs GInputDumpConsoleCommand(
	TEXT("AirCombat.Input.Dump"),
	TEXT("把当前输入快照打到日志里（面板关着的时候用）。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleInputDumpConsoleCommand));

// ----------------------------------------------------------------------
// 构造
// ----------------------------------------------------------------------

AAirCombatPlayerController::AAirCombatPlayerController()
{
	// APlayerController 默认就会 tick。
	// PlayerTick → TickPlayerInput → ProcessInputStack 里会刷新
	// Enhanced Input 的 value binding，所以我们每帧读到的都是**本帧**的值。

	// 调试面板默认用 C++ 类（UMG，widget tree 由 C++ 自建）。
	// 想换皮就做一个它的蓝图子类指定过来。
	InputDebugWidgetClass = UFighterInputDebugWidget::StaticClass();
    FlightDebugWidgetClass = UFighterFlightDebugWidget::StaticClass();
}

// ----------------------------------------------------------------------
// Enhanced Input 绑定
// ----------------------------------------------------------------------

void AAirCombatPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InitializeActionBindings();
}

void AAirCombatPlayerController::InitializeActionBindings()
{
	if (bBindingsInitialized)
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] InputComponent 不是 UEnhancedInputComponent，Enhanced Input 无法工作。"),
			*GetName());
		return;
	}

	if (!InputConfig)
	{
		// BeginPlay 里还会再报一次；这里先挡住，避免后面空指针。
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] 没有指定 InputConfig，所有飞行输入都不会生效。"),
			*GetName());
		return;
	}

	// ------------------------------------------------------------------
	// 连续量：建 value binding
	//
	// GetBoundActionValue 只能读到"建过 value binding"的动作，
	// 所以每个要每帧轮询的动作都要 BindActionValue 一次。
	//
	// ⚠ BindActionValue 返回的是内部数组元素的引用，之后再 Add 别的动作
	//   可能让这个引用失效 → 所以这里只调用、绝不保存返回值。
	// ------------------------------------------------------------------
	const UInputAction* const ValueActions[] =
	{
		InputConfig->CameraYawAction.Get(),
		InputConfig->CameraPitchAction.Get(),

		InputConfig->PitchAction.Get(),
		InputConfig->RollAction.Get(),
		InputConfig->YawAction.Get(),

		InputConfig->TrimPitchAction.Get(),
		InputConfig->TrimYawAction.Get(),
		InputConfig->TrimRollAction.Get(),

		InputConfig->FlapAction.Get(),

		InputConfig->ThrottleUpAction.Get(),
		InputConfig->ThrottleDownAction.Get(),
		InputConfig->SpeedBrakeAction.Get(),
		InputConfig->BrakeAction.Get(),
	};

	for (const UInputAction* Action : ValueActions)
	{
		if (Action)
		{
			EnhancedInput->BindActionValue(Action);
		}
	}

	// ------------------------------------------------------------------
	// 一次性开关：事件绑定
	//
	// Started = 按下的瞬间触发一次。按住不会重复触发（那是 Completed 的事），
	// 所以不会出现"按住起落架键每帧都收放一次"。
	//
	// 这三个开关由 C++ 独占，**蓝图里不要再绑一遍**，否则会双触发。
	// ------------------------------------------------------------------
	if (InputConfig->GearAction)
	{
		EnhancedInput->BindAction(InputConfig->GearAction, ETriggerEvent::Started,
			this, &AAirCombatPlayerController::OnGearToggled);
	}

	if (InputConfig->CameraAction)
	{
		EnhancedInput->BindAction(InputConfig->CameraAction, ETriggerEvent::Started,
			this, &AAirCombatPlayerController::OnCameraToggled);
	}

	if (InputConfig->ResetTrimAction)
	{
		EnhancedInput->BindAction(InputConfig->ResetTrimAction, ETriggerEvent::Started,
			this, &AAirCombatPlayerController::OnResetTrim);
	}

	bBindingsInitialized = true;
}

// ----------------------------------------------------------------------
// 生命周期
// ----------------------------------------------------------------------

void AAirCombatPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ValidateInputConfiguration();

	// 配置可能是运行时才填上的（SetupInputComponent 时还没有），
	// 所以这里再兜一次。
	InitializeActionBindings();

	// 登记到用户设置 → 以后做重绑定 UI 时可以直接枚举这些映射。
	// 注意这和 AddMappingContext 是两件事：
	//   RegisterInputMappingContext = 登记（供 UI 枚举 / 保存）
	//   AddMappingContext            = 激活（真正吃键）
	RegisterMappingContextWithUserSettings();

	// Possess 可能发生在 BeginPlay 之前，这里补一次激活。
	if (GetPawn())
	{
		RegisterFlightMapping();
	}

	// 调试面板：属性里勾了"显示"就直接建起来。
	if (bShowInputDebug)
	{
		CreateInputDebugWidget();
	}
    if (bShowFlightDebug) CreateFlightDebugWidget();
}

void AAirCombatPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 兜底：PIE 结束等情况下可能没走过 OnUnPossess。
	UnregisterFlightMapping();

	// 调试面板要从视口摘掉，否则会残留到下一局。
	DestroyInputDebugWidget();
    DestroyFlightDebugWidget();

	Super::EndPlay(EndPlayReason);
}

void AAirCombatPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 换飞机：旧飞机的输入状态一律不带过去。
	ResetInputState();

	// 激活飞行 IMC。
	RegisterFlightMapping();
    if (FlightDebugWidget) FlightDebugWidget->RefreshNow();
}

void AAirCombatPlayerController::OnUnPossess()
{
	Super::OnUnPossess();

	// 飞行输入失效：停掉 IMC、清掉还按着的键、清空状态。
	UnregisterFlightMapping();
	ResetInputState();
    if (FlightDebugWidget) FlightDebugWidget->RefreshNow();
}

// ----------------------------------------------------------------------
// 每帧
// ----------------------------------------------------------------------

void AAirCombatPlayerController::PlayerTick(float DeltaTime)
{
	// Super 里会跑 TickPlayerInput → ProcessInputStack，
	// Enhanced Input 的 value binding 在那里刷新。所以 Super 之后读到的
	// 就是**本帧**的值。
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		// 专用服务器 / 非本地控制器：没有本地输入。
		return;
	}

	// 调试面板兜底：BeginPlay 时视口可能还没准备好，这里再试一次。
	if (bShowInputDebug && !InputDebugWidget)
	{
		CreateInputDebugWidget();
	}

    if (bShowFlightDebug) CreateFlightDebugWidget(); else DestroyFlightDebugWidget();

	// UserSettings 有可能比 BeginPlay 晚一点才建好（引擎按 LocalPlayer 生命周期创建），
	// 所以没登记成功就每帧再试一次，成功后就不试了。
	if (!bRegisteredWithUserSettings)
	{
		RegisterMappingContextWithUserSettings();
	}

	AFighter* Fighter = GetFighter();
	if (!Fighter || !InputConfig)
	{
		// 没有飞机或没有配置：不要留下任何旧输入。
		ResetInputState();
		return;
	}

	const UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	// ------------------------------------------------------------------
	// ① 键盘虚拟杆：数字量 → 速率受限的模拟量
	//
	// 轴动作在 IMC 里用 Cumulative 合并，所以正负键同时按 = 0（互相抵消）。
	// ------------------------------------------------------------------

	// ⚠ 这两个 UPROPERTY 以前**从来没有**传给虚拟杆 —— 改它们完全没效果，
	//   虚拟杆一直用构造时的默认 3.0 / 6.0。这里补上。
	Stick.SetRates(KeyAxisMoveRate, KeyAxisReturnRate);

	FFighterStickPosition Target;
	Target.Roll  = ReadAxis(EnhancedInput, InputConfig->RollAction);
	Target.Pitch = ReadAxis(EnhancedInput, InputConfig->PitchAction);
	Target.Yaw   = ReadAxis(EnhancedInput, InputConfig->YawAction);

	const FFighterStickPosition StickPos = Stick.Update(Target, DeltaTime);

	// ------------------------------------------------------------------
	// ② 组装操纵指令
	// ------------------------------------------------------------------
	FFighterControlCommand NewCommand;

	NewCommand.Roll  = FMath::Clamp(RollKey.Apply(StickPos.Roll),  -1.f, 1.f);
	NewCommand.Pitch = FMath::Clamp(PitchKey.Apply(StickPos.Pitch), -1.f, 1.f);
	NewCommand.Yaw   = FMath::Clamp(YawKey.Apply(StickPos.Yaw),   -1.f, 1.f);

	if (bInvertPitch)
	{
		NewCommand.Pitch = -NewCommand.Pitch;
	}

	// 配平 / 襟翼：直接用 ±1，不需要虚拟杆
	NewCommand.TrimPitch = ReadAxis(EnhancedInput, InputConfig->TrimPitchAction);
	NewCommand.TrimYaw   = ReadAxis(EnhancedInput, InputConfig->TrimYawAction);
	NewCommand.TrimRoll  = ReadAxis(EnhancedInput, InputConfig->TrimRollAction);

	NewCommand.Flap       = ReadAxis(EnhancedInput, InputConfig->FlapAction);
	NewCommand.SpeedBrake = ReadDigital(EnhancedInput, InputConfig->SpeedBrakeAction);
	NewCommand.Brake      = ReadDigital(EnhancedInput, InputConfig->BrakeAction);

	// 油门：按住"加油门"= +1，按住"收油门"= −1。
	// 绝对油门是**有状态**的，由飞控层按 ThrottleRate 缓慢改变并保持。
	NewCommand.ThrottleDelta = ReadDigital(EnhancedInput, InputConfig->ThrottleUpAction)
	                         - ReadDigital(EnhancedInput, InputConfig->ThrottleDownAction);

	// 本地观察独立于飞行命令；组件在物理更新后消费一次像素增量。
	if (UFighterCameraControllerComponent* Camera = Fighter->GetCameraController())
	{
		Camera->AddLookInput(ReadAxis(EnhancedInput, InputConfig->CameraYawAction),
			ReadAxis(EnhancedInput, InputConfig->CameraPitchAction));
	}

	// ------------------------------------------------------------------
	// ③ 交给飞机
	// ------------------------------------------------------------------
	Command = NewCommand;
	Fighter->ApplyPilotControls(Command);
}

// ----------------------------------------------------------------------
// IMC 生命周期
// ----------------------------------------------------------------------

void AAirCombatPlayerController::RegisterFlightMapping()
{
	if (!IsLocalController())
	{
		// 专用服务器上不注册本地输入。
		return;
	}

	const UInputMappingContext* IMC = InputConfig ? InputConfig->FlightKeyboardContext.Get() : nullptr;
	if (!IMC)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] 拿不到 UEnhancedInputLocalPlayerSubsystem，飞行输入无法激活。"),
			*GetName());
		return;
	}

	// 先摘后加 → 重复 Possess 也不会叠加注册。
	Subsystem->RemoveMappingContext(IMC);
	Subsystem->AddMappingContext(IMC, FlightMappingPriority);
}

void AAirCombatPlayerController::UnregisterFlightMapping()
{
	if (!IsLocalController())
	{
		return;
	}

	const UInputMappingContext* IMC = InputConfig ? InputConfig->FlightKeyboardContext.Get() : nullptr;
	if (!IMC)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RemoveMappingContext(IMC);
	}

	// 清掉"还按着"的状态：正在进行的动作会收到 Canceled，值回到 0。
	if (PlayerInput)
	{
		PlayerInput->FlushPressedKeys();
	}
}

void AAirCombatPlayerController::RegisterMappingContextWithUserSettings()
{
	if (bRegisteredWithUserSettings || !IsLocalController())
	{
		return;
	}

	const UInputMappingContext* IMC = InputConfig ? InputConfig->FlightKeyboardContext.Get() : nullptr;
	if (!IMC)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		return;
	}

	UEnhancedInputUserSettings* Settings = Subsystem->GetUserSettings();
	if (!Settings)
	{
		// UserSettings 还没建好（引擎按 LocalPlayer 生命周期创建），下帧再试。
		UE_LOG(LogAirCombatInput, Verbose,
			TEXT("[%s] Enhanced Input User Settings 还没就绪，稍后重试登记。"),
			*GetName());
		return;
	}

	// RegisterInputMappingContext 只做"登记"（供重绑定 UI 枚举 / 保存），
	// 真正激活输入的是 AddMappingContext —— 两者不能混用。
	Settings->RegisterInputMappingContext(IMC);

	bRegisteredWithUserSettings = true;

	UE_LOG(LogAirCombatInput, Log,
		TEXT("[%s] IMC '%s' 已登记到 Enhanced Input User Settings（可供玩家自定义键位）。"),
		*GetName(), *IMC->GetName());
}

void AAirCombatPlayerController::ResetInputState()
{
	Stick.Reset();
	Command.Reset();
	if (AFighter* Fighter = GetFighter())
	{
		Fighter->GetCameraController()->ClearLookInput();
	}
}

// ----------------------------------------------------------------------
// 校验
// ----------------------------------------------------------------------

void AAirCombatPlayerController::ValidateInputConfiguration() const
{
	if (!InputConfig)
	{
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] 没有指定 InputConfig。请在 BP_AirCombatPlayerController 里把 DA_FighterInputConfig 拖到 InputConfig。"),
			*GetName());
		return;
	}

	TArray<FString> Problems;
	InputConfig->CollectProblems(Problems);

	for (const FString& Problem : Problems)
	{
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] InputConfig '%s': %s"),
			*GetName(), *InputConfig->GetName(), *Problem);
	}
}

// ----------------------------------------------------------------------
// 工具
// ----------------------------------------------------------------------

UEnhancedInputLocalPlayerSubsystem* AAirCombatPlayerController::GetEnhancedInputSubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

AFighter* AAirCombatPlayerController::GetFighter() const
{
	// 不缓存指针：Pawn 随时可能被换掉。
	return Cast<AFighter>(GetPawn());
}

float AAirCombatPlayerController::ReadAxis(const UEnhancedInputComponent* EnhancedInput, const UInputAction* Action)
{
	if (!EnhancedInput || !Action)
	{
		return 0.f;
	}

	// 没有 action instance 时值会被重置为 0，所以键盘松开会自动回零。
	return EnhancedInput->GetBoundActionValue(Action).Get<float>();
}

float AAirCombatPlayerController::ReadDigital(const UEnhancedInputComponent* EnhancedInput, const UInputAction* Action)
{
	return ReadAxis(EnhancedInput, Action) > 0.5f ? 1.f : 0.f;
}

// ----------------------------------------------------------------------
// 一次性开关
// ----------------------------------------------------------------------

void AAirCombatPlayerController::OnGearToggled(const FInputActionValue& Value)
{
	if (AFighter* Fighter = GetFighter())
	{
		Fighter->ToggleGear();
	}
}

void AAirCombatPlayerController::OnCameraToggled(const FInputActionValue& Value)
{
	if (AFighter* Fighter = GetFighter())
	{
		Fighter->GetCameraController()->ToggleCamera();
	}
}

void AAirCombatPlayerController::OnResetTrim(const FInputActionValue& Value)
{
	if (AFighter* Fighter = GetFighter())
	{
		Fighter->ResetTrim();
	}
}

// ----------------------------------------------------------------------
// 调试面板
// ----------------------------------------------------------------------

void AAirCombatPlayerController::SetInputDebugVisible(bool bVisible)
{
	if (!IsLocalController())
	{
		// 专用服务器 / 非本地控制器没有本地输入，没什么可看。
		return;
	}

	bShowInputDebug = bVisible;

	if (bVisible)
	{
		CreateInputDebugWidget();
	}
	else
	{
		DestroyInputDebugWidget();
	}
}

void AAirCombatPlayerController::CreateInputDebugWidget()
{
	if (InputDebugWidget || !InputDebugWidgetClass)
	{
		return;
	}

	if (!GetWorld() || !GetWorld()->GetGameViewport())
	{
		// 视口还没准备好 —— PlayerTick 里会再试一次。
		return;
	}

	UFighterInputDebugWidget* Widget = CreateWidget<UFighterInputDebugWidget>(this, InputDebugWidgetClass);
	if (!Widget)
	{
		UE_LOG(LogAirCombatInput, Error,
			TEXT("[%s] 建调试面板失败（类 '%s'）。"), *GetName(), *GetNameSafe(InputDebugWidgetClass));
		return;
	}

	Widget->SourceController = this;
	Widget->AddToViewport(100);

	// 面板加进视口后，把键盘焦点明确交还给游戏视口。
	//
	// 为什么必须这么做：游戏视口（UGameViewportClient::GetGameViewportWidget()）才是
	// 真正接收键盘事件的地方 —— 键盘输入走 Slate 的"当前焦点控件"，
	// 焦点不在视口上，键盘操纵和控制台(`~`)就全废了。
	// 面板一出现就把焦点弄丢的话，玩家开着面板就没法开飞机了。
	//
	// 面板自己已经是 HitTestInvisible + 不可聚焦，所以这里只是"保险"：
	// 焦点本来就在视口上时，这次调用是空操作。
	RestoreGameViewportKeyboardFocus();

	InputDebugWidget = Widget;
}

void AAirCombatPlayerController::RestoreGameViewportKeyboardFocus()
{
	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	// 控制台开着的时候别动焦点：控制台自己管焦点（打开时把焦点设到游戏视口，
	// 关闭时再还回来），这时候抢焦点会打断用户继续敲命令。
	if (const UConsole* Console = ViewportClient->ViewportConsole)
	{
		if (Console->ConsoleState != NAME_None)
		{
			return;
		}
	}

	TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
	if (!ViewportWidget.IsValid())
	{
		return;
	}

	// 引擎自己就是这么做的（`UGameViewportClient::NotifyPlayerAdded`）：
	//     FSlateApplication::Get().SetUserFocusToGameViewport(PlayerIndex);
	// 用同一套 API，"焦点回到游戏视口"这件事就和游戏刚启动时一模一样。
	//
	// 为什么强调 user focus：UE5 里键盘事件是发给**用户焦点**所在控件的，
	// 老的 SetKeyboardFocus 只动"键盘焦点"，不一定管用。
	const int32 PlayerIndex = GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : 0;
	FSlateApplication::Get().SetUserFocusToGameViewport(PlayerIndex);

	// 兜底：万一 user focus 那条路没生效，再明确设一次键盘焦点。
	if (FSlateApplication::Get().GetUserFocusedWidget(PlayerIndex) != ViewportWidget)
	{
		FSlateApplication::Get().SetKeyboardFocus(ViewportWidget, EFocusCause::SetDirectly);
	}
}

void AAirCombatPlayerController::DestroyInputDebugWidget()
{
	if (!InputDebugWidget)
	{
		return;
	}

	InputDebugWidget->RemoveFromParent();
	InputDebugWidget = nullptr;
}

void AAirCombatPlayerController::BuildInputDebugSnapshot(FFighterInputDebugSnapshot& OutSnapshot)
{
	OutSnapshot = FFighterInputDebugSnapshot();

	const UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	const UFighterInputConfig* Config = InputConfig.Get();

	// ---- 配置里的动作（配置为空时全是 nullptr，读出来就是 0） ----
	const UInputAction* RollAction       = Config ? Config->RollAction.Get()               : nullptr;
	const UInputAction* PitchAction      = Config ? Config->PitchAction.Get()              : nullptr;
	const UInputAction* YawAction        = Config ? Config->YawAction.Get()                : nullptr;
	const UInputAction* TrimPitchAction  = Config ? Config->TrimPitchAction.Get()          : nullptr;
	const UInputAction* TrimYawAction    = Config ? Config->TrimYawAction.Get()            : nullptr;
	const UInputAction* TrimRollAction   = Config ? Config->TrimRollAction.Get()           : nullptr;
	const UInputAction* FlapAction       = Config ? Config->FlapAction.Get()               : nullptr;
	const UInputAction* ThrottleUpAction = Config ? Config->ThrottleUpAction.Get()         : nullptr;
	const UInputAction* ThrottleDownAction = Config ? Config->ThrottleDownAction.Get()     : nullptr;
	const UInputAction* SpeedBrakeAction = Config ? Config->SpeedBrakeAction.Get()         : nullptr;
	const UInputAction* BrakeAction      = Config ? Config->BrakeAction.Get()              : nullptr;
	const UInputAction* CameraYawAction  = Config ? Config->CameraYawAction.Get()          : nullptr;
	const UInputAction* CameraPitchAction = Config ? Config->CameraPitchAction.Get()       : nullptr;

	// ------------------------------------------------------------------
	// 操纵轴四级数据流：RAW → STICK → RESP → CMD
	// ------------------------------------------------------------------
	const FFighterStickPosition& StickPos = Stick.GetPosition();
	const FFighterAxisResponse* const AxisCurves[3] = { &RollKey, &PitchKey, &YawKey };
	const UInputAction* const AxisActions[3] = { RollAction, PitchAction, YawAction };
	const float StickValues[3] = { StickPos.Roll, StickPos.Pitch, StickPos.Yaw };
	const float CommandValues[3] = { Command.Roll, Command.Pitch, Command.Yaw };
	const TCHAR* const AxisNames[3] = { TEXT("Roll"), TEXT("Pitch"), TEXT("Yaw") };

	for (int32 Index = 0; Index < 3; ++Index)
	{
		FFighterInputDebugAxis& Axis = OutSnapshot.Axes.AddDefaulted_GetRef();
		Axis.Name = AxisNames[Index];

		// ① IA 合并后的原始值（键盘按下去就是 ±1）
		Axis.Raw = ReadAxis(EnhancedInput, AxisActions[Index]);

		// ② 键盘虚拟杆当前的杆位
		Axis.Stick = StickValues[Index];

		// ③ 灵敏度 → 限幅 → 死区 → 指数曲线（还没做俯仰反转）
		Axis.Response = FMath::Clamp(AxisCurves[Index]->Apply(Axis.Stick), -1.f, 1.f);

		// ④ 真正交给飞控层的量（俯仰反转在这里面）
		Axis.Command = CommandValues[Index];
	}

	// ------------------------------------------------------------------
	// 配平 / 构型 / 油门 / 视角
	// ------------------------------------------------------------------
	OutSnapshot.TrimPitch = ReadAxis(EnhancedInput, TrimPitchAction);
	OutSnapshot.TrimYaw   = ReadAxis(EnhancedInput, TrimYawAction);
	OutSnapshot.TrimRoll  = ReadAxis(EnhancedInput, TrimRollAction);

	OutSnapshot.Flap       = ReadAxis(EnhancedInput, FlapAction);
	OutSnapshot.SpeedBrake = ReadDigital(EnhancedInput, SpeedBrakeAction);
	OutSnapshot.Brake      = ReadDigital(EnhancedInput, BrakeAction);

	OutSnapshot.ThrottleUp   = ReadDigital(EnhancedInput, ThrottleUpAction);
	OutSnapshot.ThrottleDown = ReadDigital(EnhancedInput, ThrottleDownAction);
	OutSnapshot.ThrottleDelta = OutSnapshot.ThrottleUp - OutSnapshot.ThrottleDown;

	OutSnapshot.MouseDeltaX = ReadAxis(EnhancedInput, CameraYawAction);
	OutSnapshot.MouseDeltaY = ReadAxis(EnhancedInput, CameraPitchAction);

	// ------------------------------------------------------------------
	// IMC / 速率 / 曲线
	// ------------------------------------------------------------------
	const UInputMappingContext* IMC = Config ? Config->FlightKeyboardContext.Get() : nullptr;
	OutSnapshot.IMCName = IMC ? FString(IMC->GetName()) : FString(TEXT("<none>"));

	OutSnapshot.bIMCActive = false;
	if (IMC)
	{
		if (const UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
		{
			OutSnapshot.bIMCActive = Subsystem->HasMappingContext(IMC);
		}
	}

	// prop = 属性里填的值；eff = 虚拟杆实际在用的值。
	// 两者不一致 = 属性没接上（这正是之前 KeyAxisMoveRate 的毛病）。
	OutSnapshot.MoveRateProperty   = KeyAxisMoveRate;
	OutSnapshot.MoveRateEffective  = Stick.GetMoveRate();
	OutSnapshot.ReturnRateProperty = KeyAxisReturnRate;
	OutSnapshot.ReturnRateEffective = Stick.GetReturnRate();

	OutSnapshot.bInvertPitch = bInvertPitch;

	const auto FillCurve = [](FFighterInputDebugCurve& Out, const FFighterAxisResponse& In)
	{
		Out.DeadZone   = In.DeadZone;
		Out.Exponent   = In.Exponent;
		Out.Sensitivity = In.Sensitivity;
	};

	FillCurve(OutSnapshot.RollCurve, RollKey);
	FillCurve(OutSnapshot.PitchCurve, PitchKey);
	FillCurve(OutSnapshot.YawCurve, YawKey);

	// ------------------------------------------------------------------
	// 飞机侧状态
	// ------------------------------------------------------------------
	if (const AFighter* Fighter = GetFighter())
	{
		OutSnapshot.bHasAircraft = true;
		OutSnapshot.bGearDown = Fighter->IsGearDown();
		OutSnapshot.AltitudeFt = Fighter->GetAltitudeFt();
		OutSnapshot.ThrottleAbsolute = Fighter->GetThrottle();
	}
}
