#include "Camera/FighterCameraControllerComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

UFighterCameraControllerComponent::UFighterCameraControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UFighterCameraControllerComponent::Initialize(USpringArmComponent* InChaseArm,
	UCameraComponent* InChaseCamera, UCameraComponent* InCockpitCamera, USceneComponent* InViewFrame)
{
	ViewFrame = InViewFrame;
	ChaseArm = InChaseArm;
	ChaseCamera = InChaseCamera;
	CockpitCamera = InCockpitCamera;
	AddTickPrerequisiteActor(GetOwner());
	if (ChaseArm)
	{
		// Explicit world rotation preserves a level horizon through aircraft rolls.
		ChaseArm->SetUsingAbsoluteRotation(true);
		ChaseArm->AddTickPrerequisiteComponent(this);
		if (ChaseCamera)
		{
			ChaseCamera->AttachToComponent(ChaseArm, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				USpringArmComponent::SocketName);
		}
	}
	SetCameraMode(CameraMode);
}

void UFighterCameraControllerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		if (ChaseArm)
		{
			ChaseArm->SetComponentTickEnabled(false);
		}
	}
}

void UFighterCameraControllerComponent::AddLookInput(float MouseDeltaX, float MouseDeltaY)
{
	if (FMath::IsFinite(MouseDeltaX) && FMath::IsFinite(MouseDeltaY))
	{
		PendingLook += FVector2D(MouseDeltaX, MouseDeltaY);
	}
}

void UFighterCameraControllerComponent::ClearLookInput()
{
	PendingLook = FVector2D::ZeroVector;
}

void UFighterCameraControllerComponent::SetCameraMode(EFighterCameraMode NewMode)
{
	CameraMode = NewMode;
	ClearLookInput();
	MouseIdleTime = 0.f;
	const bool bCockpit = CameraMode == EFighterCameraMode::Cockpit;
	if (ChaseCamera)
	{
		ChaseCamera->SetActive(!bCockpit);
	}
	if (CockpitCamera)
	{
		CockpitCamera->SetActive(bCockpit);
	}
	ApplyViewRotations();
}

void UFighterCameraControllerComponent::ToggleCamera()
{
	SetCameraMode(CameraMode == EFighterCameraMode::Chase
		? EFighterCameraMode::Cockpit : EFighterCameraMode::Chase);
}

void UFighterCameraControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const FVector2D Look = PendingLook;
	ClearLookInput();
	MouseIdleTime = Look.IsNearlyZero() ? MouseIdleTime + DeltaTime : 0.f;
	const FVector2D Delta(
		Look.X * CameraYawSensitivity * (bInvertCameraYaw ? -1.f : 1.f),
		Look.Y * CameraPitchSensitivity * (bInvertCameraPitch ? -1.f : 1.f));

	if (CameraMode == EFighterCameraMode::Cockpit)
	{
		CockpitLook.X = FMath::Clamp(CockpitLook.X + Delta.X, -CockpitYawLimit, CockpitYawLimit);
		CockpitLook.Y = FMath::Clamp(CockpitLook.Y + Delta.Y, -CockpitPitchLimit, CockpitPitchLimit);
	}
	else
	{
		ChaseLook.X = FMath::UnwindDegrees(ChaseLook.X + Delta.X);
		ChaseLook.Y = FMath::Clamp(ChaseLook.Y + Delta.Y, ChasePitchMin, ChasePitchMax);
		if (bChaseAutoRecenter && MouseIdleTime > ChaseRecenterDelay && ChaseRecenterSpeed > 0.f)
		{
			ChaseLook.X = FMath::FInterpConstantTo(ChaseLook.X, 0.f, DeltaTime, ChaseRecenterSpeed);
		}
	}
	ApplyViewRotations();
}

void UFighterCameraControllerComponent::ApplyViewRotations()
{
	if (ChaseArm && GetOwner())
	{
		const float Heading = ViewFrame ? ViewFrame->GetComponentRotation().Yaw : GetOwner()->GetActorRotation().Yaw;
		ChaseArm->SetWorldRotation(FRotator(ChaseLook.Y, Heading + ChaseLook.X, 0.f));
	}
	if (CockpitCamera)
	{
		// CockpitAnchor preserves the authored pilot eye pose; look is applied relative to that pose.
		CockpitCamera->SetRelativeRotation(FRotator(CockpitLook.Y, CockpitLook.X, 0.f));
	}
}
