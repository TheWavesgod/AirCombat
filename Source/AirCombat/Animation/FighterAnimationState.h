#pragma once

#include "CoreMinimal.h"
#include "FighterAnimationState.generated.h"

/** Values in this snapshot own their storage. No simulation pointers cross into the animation graph. */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterSurfaceAnimationState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Animation") FName Id;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bAvailable = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float AngleDeg = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bHasAngle = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float Position01 = 0.f;
};

USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterGearAnimationState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Animation") FName Id;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bAvailable = false;
    /** Actual extension: 0 retracted, 1 extended. Not the pilot's gear command. */
    UPROPERTY(BlueprintReadOnly, Category="Animation") float Extension01 = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bWeightOnWheel = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float SteeringAngleDeg = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float WheelSpeedCmPerSec = 0.f;
    /** Visual rolling angle, integrated from speed only when a wheel radius is configured. */
    UPROPERTY(BlueprintReadOnly, Category="Animation") float WheelRotationDeg = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bHasWheelRadius = false;
    /** Actual JSBSim strut compression, independent of retraction animation. */
    UPROPERTY(BlueprintReadOnly, Category="Animation") float CompressionCm = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float CompressionSpeedCmPerSec = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float StrutForceNewtons = 0.f;
    /** Requires the artist's full-compression travel. An unset range is not silently guessed. */
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bHasSuspensionRange = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float SuspensionAlpha = 0.f;
};

USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterEngineAnimationState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Animation") int32 EngineIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bAvailable = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bRunning = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bAfterburner = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float ThrottleCommand01 = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float N1Percent = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float N2Percent = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float ThrustNewtons = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bNozzleAvailable = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float NozzlePosition01 = 0.f;
};

USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterAnimationState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") bool bBindingsMatchModel = false;
    UPROPERTY(BlueprintReadOnly, Category="Animation") FName Model;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float AirspeedKts = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") float GroundSpeedKts = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Animation") TArray<FFighterSurfaceAnimationState> Surfaces;
    UPROPERTY(BlueprintReadOnly, Category="Animation") TArray<FFighterGearAnimationState> Gears;
    UPROPERTY(BlueprintReadOnly, Category="Animation") TArray<FFighterEngineAnimationState> Engines;
};
