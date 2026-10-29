// Fill out your copyright notice in the Description page of Project Settings.

#include "Fighter.h"

#include "Camera/CameraComponent.h"
#include "Camera/FighterCameraControllerComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "Presentation/FighterPresentationComponent.h"

AFighter::AFighter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// ------------------------------------------------------------------
	// 根节点
	//
	// 插件在 BeginPlay 里执行 Parent->GetRootComponent()->SetMobility(Movable)，
	// 并且每帧用 SetActorLocationAndRotation 写根节点的世界变换。
	// 所以根节点必须是纯 SceneComponent（不能是网格）。
	// ------------------------------------------------------------------
	AircraftRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AircraftRoot"));
	AircraftRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(AircraftRoot);

	// ------------------------------------------------------------------
	// 动力学：JSBSim 是唯一的物理权威
	//
	// 注意：UJSBSimMovementComponent 继承的是 UActorComponent，不是 USceneComponent，
	// 所以它没有父子关系、也没有自己的 Transform —— 它通过
	// Owner->SetActorLocationAndRotation() 直接写 Actor 的世界变换。
	// ------------------------------------------------------------------
	FlightDynamics = CreateDefaultSubobject<UJSBSimMovementComponent>(TEXT("FlightDynamics"));
	FlightDynamics->AircraftModel = TEXT("f16");        // Resources/JSBSim/aircraft/f16
	FlightDynamics->bStartWithGearDown = true;
	FlightDynamics->bStartWithEngineRunning = true;
	// 插件自己的调试文本用 AddOnScreenDebugMessage(1, ...) 画在**屏幕左上角**，
	// 和输入调试面板（PanelOffset 16,16）完全重叠 —— 两块字会叠在一起，谁也看不清。
	// 插件的默认值本来就是 true，这里显式关掉。
	// 需要看 JSBSim 的原始调试文本（指令 / 发动机 / 油箱 / 落架力）时，
	// 在 BP 的 FlightDynamics 组件上勾回 Model → DrawDebug 即可。
	FlightDynamics->DrawDebug = false;

	// Pawn 的 Tick 排在 JSBSim 组件之后 → 读到的状态是本帧最新的
	AddTickPrerequisiteComponent(FlightDynamics);

	// ------------------------------------------------------------------
	// 动态重心标记与共享显示姿态；结构原点标定仅保存在 FlightDynamics 中。
	// ------------------------------------------------------------------
	AircraftCG = CreateDefaultSubobject<USceneComponent>(TEXT("AircraftCG"));
	AircraftCG->SetupAttachment(AircraftRoot);
	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	PresentationRoot->SetupAttachment(AircraftRoot);
	Presentation = CreateDefaultSubobject<UFighterPresentationComponent>(TEXT("Presentation"));

	// ------------------------------------------------------------------
	// 机体：骨架网格（模型 + 骨骼 + Animation BP）
	//
	// 操纵面不是独立的网格组件，而是**骨骼**：
	// AnimInstance 自行采集 JSBSim 实际状态并生成动画快照，
	// AnimBP 决定这些角度驱动哪根骨、绕哪个轴。
	// ------------------------------------------------------------------
	Airframe = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Airframe"));
	Airframe->SetupAttachment(PresentationRoot);
	Airframe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Airframe->SetGenerateOverlapEvents(false);
	Airframe->SetCastShadow(true);

	// Standard UE camera rig. Behaviour is owned by CameraController.
	CockpitAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CockpitAnchor"));
	CockpitAnchor->SetupAttachment(PresentationRoot);
	ChaseFocus = CreateDefaultSubobject<USceneComponent>(TEXT("ChaseFocus"));
	ChaseFocus->SetupAttachment(PresentationRoot);
	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(ChaseFocus);
	ChaseArm->SetUsingAbsoluteRotation(true);
	ChaseArm->TargetArmLength = 2500.f;
	ChaseArm->SocketOffset = FVector(0.f, 0.f, 250.f);
	ChaseArm->bEnableCameraLag = false;
	ChaseArm->CameraLagSpeed = 8.f;
	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;
	ChaseCamera->bLockToHmd = false;
	CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
	CockpitCamera->SetupAttachment(CockpitAnchor);
	CockpitCamera->bUsePawnControlRotation = false;
	CockpitCamera->bLockToHmd = false;
	CockpitCamera->SetAutoActivate(false);
	CameraController = CreateDefaultSubobject<UFighterCameraControllerComponent>(TEXT("CameraController"));
}

void AFighter::AttachPresentationComponents()
{
	auto Attach = [](USceneComponent* Child, USceneComponent* Parent)
	{
		if (Child->GetAttachParent() != Parent)
			Child->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
	};
	Attach(PresentationRoot, AircraftRoot);
	Attach(Airframe, PresentationRoot);
	// Model axes, dimensions and origin belong to the imported asset. No second calibration transform.
	Airframe->SetRelativeTransform(FTransform::Identity);
	Attach(CockpitAnchor, PresentationRoot);
	Attach(CockpitCamera, CockpitAnchor);
	Attach(ChaseFocus, PresentationRoot);
	Attach(ChaseArm, ChaseFocus);
}

void AFighter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	AttachPresentationComponents();
}

void AFighter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	AttachPresentationComponents();
	Presentation->Initialize(FlightDynamics, PresentationRoot);
	CameraController->Initialize(ChaseArm, ChaseCamera, CockpitCamera, PresentationRoot);
	CameraController->AddTickPrerequisiteComponent(Presentation);

	if (FlightDynamics)
	{
		FlightDynamics->OnBeforeSimulation.AddUObject(this, &AFighter::PrepareFlightControls);
		// ⚠ 必须在这里写，不能写在 BeginPlay 里：
		//   插件的地面配平（DoTrim）跑在它自己组件的 BeginPlay 里
		//   （LoadAircraft → PrepareJSBSim → DoTrim），
		//   而 Actor 的 BeginPlay 是在所有组件的 BeginPlay 之后才跑的。
		//   PostInitializeComponents 保证我们比组件的 BeginPlay 早。
		if (StartMode == EFighterStartMode::Ground)
		{
			FlightDynamics->StartOnGround = true;
			FlightDynamics->InitialCalibratedAirSpeedKts = 0.f;
			FlightDynamics->bStartWithGearDown = true;
		}
		else
		{
			FlightDynamics->StartOnGround = false;
			FlightDynamics->InitialCalibratedAirSpeedKts = AirStartSpeedKts;
		}
	}
}

void AFighter::ConfigureStartState(EFighterStartMode InStartMode, float InAirStartSpeedKts)
{
    if (!ensureMsgf(!IsActorInitialized(), TEXT("Initial conditions must be configured before actor initialization.")))
        return;
    StartMode = InStartMode;
    AirStartSpeedKts = FMath::Max(1.f, InAirStartSpeedKts);
}

void AFighter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// UE orders the Pawn after its controller; the independent FDM component needs its own dependency.
	FlightDynamics->AddTickPrerequisiteActor(NewController);
}

void AFighter::UnPossessed()
{
	if (AController* OldController = GetController())
	{
		FlightDynamics->RemoveTickPrerequisiteActor(OldController);
	}
	// 没人操纵了：把还没消费完的杆量清掉，
	// 否则飞机会保持着最后的操纵量继续飞。
	PendingCommand.Reset();
	CameraController->ClearLookInput();

	Super::UnPossessed();
}

void AFighter::BeginPlay()
{
	Super::BeginPlay();

	// 让骨架网格的 Tick 排在 Pawn 之后：
	// AnimInstance 在网格更新时采集本帧 FDM 状态；Pawn 不再逐字段写动画参数。
	if (Airframe)
	{
		Airframe->AddTickPrerequisiteActor(this);
		Airframe->AddTickPrerequisiteComponent(Presentation);
	}
}

// ----------------------------------------------------------------------
// 操纵接口
// ----------------------------------------------------------------------

void AFighter::ApplyPilotControls(const FFighterControlCommand& Command)
{
	PendingCommand = Command;
}

void AFighter::ToggleGear()
{
	CommandProcessor.RequestGearToggle();
}

void AFighter::ResetTrim()
{
	CommandProcessor.RequestResetTrim();
}

// ----------------------------------------------------------------------
// Tick
// ----------------------------------------------------------------------

void AFighter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The plugin accounts for CG and the structural datum in the actor pose; the mesh stays at identity.
	if (FlightDynamics && AircraftCG)
	{
		AircraftCG->SetRelativeLocation(FlightDynamics->CGLocalPosition);
	}

}

// ----------------------------------------------------------------------
// 状态查询
// ----------------------------------------------------------------------

float AFighter::GetAltitudeFt() const
{
	return FlightDynamics ? (float)FlightDynamics->AircraftState.AltitudeASLFt : 0.f;
}

bool AFighter::IsAfterburner() const
{
	return FlightDynamics
		&& FlightDynamics->EngineStates.Num() > 0
		&& FlightDynamics->EngineStates[0].Augmentation;
}

// ----------------------------------------------------------------------
// 仿真前指令提交
// ----------------------------------------------------------------------

void AFighter::PrepareFlightControls(float DeltaTime)
{
	// ------------------------------------------------------------------
	// 操纵指令 → JSBSim 指令
	//
	// 指令由 AAirCombatPlayerController 通过 ApplyPilotControls() 送来。
	// 鼠标只用来转视角（喂给相机），机动全部来自键盘虚拟杆。
	// ------------------------------------------------------------------
	if (FlightDynamics)
	{
		// 命令变化率（编辑器里可以实时改）
		CommandProcessor.ThrottleRate           = ThrottleRate;
		CommandProcessor.TrimRate               = TrimRate;
		CommandProcessor.FlapRate               = FlapRate;
		CommandProcessor.SpeedBrakeRate         = SpeedBrakeRate;

		CommandProcessor.Update(DeltaTime, PendingCommand, FlightDynamics);
	}
}

