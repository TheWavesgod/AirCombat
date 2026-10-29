#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AircraftCalibrationLibrary.generated.h"
class UAnimSequence;
class USkeletalMeshComponent;
UCLASS()
class AIRCRAFTCALIBRATIONTOOLS_API UAircraftCalibrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Editor|Aircraft Calibration")
    static bool BeginCalibrationAnimation(UAnimSequence* Sequence);
    /** Complete the native animation-data notification and adopt the authored sampling rate. */
    UFUNCTION(BlueprintCallable, Category="Editor|Aircraft Calibration")
    static bool FinalizeCalibrationAnimation(UAnimSequence* Sequence);
    /** Read-only LOD0 CPU skinning export in component (Mesh) coordinates. */
    UFUNCTION(BlueprintCallable, Category="Editor|Aircraft Calibration")
    static bool ExportCalibrationGeometry(USkeletalMeshComponent* Component, const FString& Filename);
};
