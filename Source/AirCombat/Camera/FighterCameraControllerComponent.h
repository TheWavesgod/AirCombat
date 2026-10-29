#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Pawns/Fighter/FighterTypes.h"
#include "FighterCameraControllerComponent.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

/** Local viewing behaviour. SpringArm owns chase positioning, lag and collision. */
UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent))
class AIRCOMBAT_API UFighterCameraControllerComponent : public UActorComponent
{

	GENERATED_BODY()

public:
	UFighterCameraControllerComponent();
	void Initialize(USpringArmComponent* InChaseArm, UCameraComponent* InChaseCamera, UCameraComponent* InCockpitCamera, USceneComponent* InViewFrame = nullptr);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Pixel displacement, consumed once at the next camera tick. No DeltaTime scaling. */
	void AddLookInput(float MouseDeltaX, float MouseDeltaY);
	void ClearLookInput();

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ToggleCamera();
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetCameraMode(EFighterCameraMode NewMode);
	UFUNCTION(BlueprintPure, Category = "Camera")
	EFighterCameraMode GetCameraMode() const { return CameraMode; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	EFighterCameraMode CameraMode = EFighterCameraMode::Chase;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Look", meta = (ClampMin = "0.0"))
	float CameraYawSensitivity = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Look", meta = (ClampMin = "0.0"))
	float CameraPitchSensitivity = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Look")
	bool bInvertCameraYaw = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Look")
	bool bInvertCameraPitch = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Chase")
	float ChasePitchMin = -55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Chase")
	float ChasePitchMax = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cockpit", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float CockpitPitchLimit = 85.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cockpit", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CockpitYawLimit = 170.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Chase")
	bool bChaseAutoRecenter = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Chase", meta = (ClampMin = "0.0"))
	float ChaseRecenterSpeed = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Chase", meta = (ClampMin = "0.0"))
	float ChaseRecenterDelay = 0.5f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> ChaseArm;
	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> ChaseCamera;
	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> CockpitCamera;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ViewFrame;
	FVector2D PendingLook = FVector2D::ZeroVector;
	FVector2D ChaseLook = FVector2D::ZeroVector;
	FVector2D CockpitLook = FVector2D::ZeroVector;
	float MouseIdleTime = 0.f;
	void ApplyViewRotations();
};
