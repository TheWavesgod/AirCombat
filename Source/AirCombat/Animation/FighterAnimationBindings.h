#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FighterAnimationBindings.generated.h"

/** Property value -> degrees. Examples: radians use 180/PI; normalized TEF uses 20 degrees. */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterSurfaceAnimationBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FString Property;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") float DegreesPerUnit = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") float OffsetDeg = 0.f;
    /** Normalized mechanisms (canopy/hook) have a position, not a fabricated physical angle. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") bool bNormalizedPositionOnly = false;
    /** Empty optional bindings explicitly describe unsupported channels such as canards on the F16 model. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") bool bOptional = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") float MinimumDeg = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") float MaximumDeg = 0.f;
};

USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterGearAnimationBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FName Id;
    /** JSBSim contact name, never a guessed array index or skeletal bone name. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FString ContactName;
    /** Set after the suspension animation is authored: uncompressed -> fully compressed travel. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding", meta=(ClampMin="0")) float MaximumCompressionCm = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding", meta=(ClampMin="0")) float WheelRadiusCm = 0.f;
};

/** Maps one simulation model to stable mechanical channels. Bone names/axes belong in its AnimBP. */
UCLASS(BlueprintType)
class AIRCOMBAT_API UFighterAnimationBindings : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FString AircraftModel;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") TArray<FFighterSurfaceAnimationBinding> Surfaces;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") TArray<FFighterGearAnimationBinding> Gears;
    void CollectProblems(TArray<FString>& Problems) const;
};
