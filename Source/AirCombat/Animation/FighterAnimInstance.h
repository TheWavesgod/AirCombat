#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/FighterAnimationState.h"
#include "FighterAnimInstance.generated.h"

class AFighter;
class UJSBSimMovementComponent;
class UFighterAnimationBindings;

/** Shared snapshot consumer for any aircraft skeleton. Bone axes and sequences belong in AnimBP. */
UCLASS(Blueprintable)
class AIRCOMBAT_API UFighterAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativeUninitializeAnimation() override;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Fighter|Animation") FFighterAnimationState AnimationState;

    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    FFighterSurfaceAnimationState GetSurfaceState(FName Id) const;
    
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    FFighterGearAnimationState GetGearState(FName Id) const;
    
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    FFighterEngineAnimationState GetEngineState(int32 EngineIndex) const;
    
    /** RotationPerDegree is (Pitch,Yaw,Roll); set signs and axes in the skeleton's AnimBP. */
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    FRotator GetSurfaceRotation(FName Id, FRotator RotationPerDegree) const;
    
    /** Axis is in the bone space selected by the AnimBP's Modify Bone node. No extra spring simulation. */
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    FVector GetSuspensionTranslation(FName Id, FVector CompressionAxis) const;
    
    /** Absolute time for an authored extended-to-retracted clip; works at spawn and during reversal. */
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    float GetGearRetractionTime(FName Id, float SequenceLength) const;
    
    /** Absolute time for an uncompressed-to-fully-compressed clip; 0 until travel is configured. */
    UFUNCTION(BlueprintPure, Category="Fighter|Animation", meta=(BlueprintThreadSafe))
    float GetSuspensionAnimationTime(FName Id, float SequenceLength) const;
    
private:
    TWeakObjectPtr<AFighter> Fighter;
    TWeakObjectPtr<UJSBSimMovementComponent> LastFDM;
    TWeakObjectPtr<UFighterAnimationBindings> LastBindings;
    uint32 LastGeneration = 0;
    void ResetSnapshot();
};
