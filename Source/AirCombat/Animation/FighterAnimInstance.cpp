#include "Animation/FighterAnimInstance.h"
#include "Animation/FighterAnimationStateBuilder.h"
#include "Pawns/Fighter/Fighter.h"
#include "JSBSimMovementComponent.h"

void UFighterAnimInstance::ResetSnapshot()
{
    AnimationState = FFighterAnimationState();
    LastFDM.Reset();
    LastBindings.Reset();
    LastGeneration = 0;
}

void UFighterAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    ResetSnapshot();
    Fighter = Cast<AFighter>(TryGetPawnOwner());
}

void UFighterAnimInstance::NativeUninitializeAnimation()
{
    ResetSnapshot();
    Fighter.Reset();
    Super::NativeUninitializeAnimation();
}

void UFighterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    
    check(IsInGameThread());
    AFighter* Owner = Cast<AFighter>(TryGetPawnOwner());
    if (Owner != Fighter.Get())
    {
        ResetSnapshot();
        Fighter = Owner;
    }
    UJSBSimMovementComponent* FDM = Owner ? Owner->GetFlightDynamics() : nullptr;
    UFighterAnimationBindings* Bindings = Owner ? Owner->GetAnimationBindings() : nullptr;
    if (!IsValid(FDM) || !FDM->IsSimulationReady())
    {
        ResetSnapshot();
        return;
    }
    if (LastFDM.Get() != FDM || LastBindings.Get() != Bindings || LastGeneration != FDM->GetModelGeneration())
    {
        ResetSnapshot();
        LastFDM = FDM;
        LastBindings = Bindings;
        LastGeneration = FDM->GetModelGeneration();
    }
    AnimationState = FFighterAnimationStateBuilder::Build(*FDM, Bindings, DeltaSeconds, AnimationState);
}

FFighterSurfaceAnimationState UFighterAnimInstance::GetSurfaceState(FName Id) const
{
    const auto* Found = AnimationState.Surfaces.FindByPredicate([&](const auto& Item) { return Item.Id == Id; });
    if (Found) return *Found;
    
    FFighterSurfaceAnimationState Empty; 
    Empty.Id = Id; 
    return Empty;
}

FFighterGearAnimationState UFighterAnimInstance::GetGearState(FName Id) const
{
    const auto* Found = AnimationState.Gears.FindByPredicate([&](const auto& Item) { return Item.Id == Id; });
    if (Found) return *Found;
    
    FFighterGearAnimationState Empty; 
    Empty.Id = Id; 
    return Empty;
}

FFighterEngineAnimationState UFighterAnimInstance::GetEngineState(int32 EngineIndex) const
{
    const auto* Found = AnimationState.Engines.FindByPredicate([&](const auto& Item) { return Item.EngineIndex == EngineIndex; });
    if (Found) return *Found;
    
    FFighterEngineAnimationState Empty; 
    Empty.EngineIndex = EngineIndex; 
    return Empty;
}

FRotator UFighterAnimInstance::GetSurfaceRotation(FName Id, FRotator RotationPerDegree) const
{
    const auto Surface = GetSurfaceState(Id);
    
    if (!Surface.bAvailable || !Surface.bHasAngle || RotationPerDegree.ContainsNaN()) return FRotator::ZeroRotator;
    
    return RotationPerDegree * Surface.AngleDeg;
}

FVector UFighterAnimInstance::GetSuspensionTranslation(FName Id, FVector CompressionAxis) const
{
    const auto Gear = GetGearState(Id);
    
    if (!Gear.bAvailable || CompressionAxis.ContainsNaN()) return FVector::ZeroVector;
    
    return CompressionAxis.GetSafeNormal() * Gear.CompressionCm;
}

float UFighterAnimInstance::GetGearRetractionTime(FName Id, float SequenceLength) const
{
    const auto Gear = GetGearState(Id);
    
    if (!Gear.bAvailable || !FMath::IsFinite(SequenceLength)) return 0.f;
    
    return (1.f - Gear.Extension01) * FMath::Max(0.f, SequenceLength);
}

float UFighterAnimInstance::GetSuspensionAnimationTime(FName Id, float SequenceLength) const
{
    const auto Gear = GetGearState(Id);
    
    if (!Gear.bAvailable || !Gear.bHasSuspensionRange || !FMath::IsFinite(SequenceLength)) return 0.f;
    
    return Gear.SuspensionAlpha * FMath::Max(0.f, SequenceLength);
}
