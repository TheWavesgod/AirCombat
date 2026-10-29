#pragma once
#include "Animation/FighterAnimationState.h"
#include "Animation/FighterAnimationBindings.h"

struct FGear;
class UJSBSimMovementComponent;

/** Game-thread adapter; the resulting state contains only values. */
class AIRCOMBAT_API FFighterAnimationStateBuilder
{
public:
    static FFighterSurfaceAnimationState Surface(const FFighterSurfaceAnimationBinding& Binding, bool bFound, double Value);
    
    static FFighterGearAnimationState Gear(const FFighterGearAnimationBinding& Binding, const FGear* Source,
        float DeltaSeconds, float PreviousRotationDeg);
    
    static FFighterAnimationState Build(UJSBSimMovementComponent& FDM, const UFighterAnimationBindings* Bindings,
        float DeltaSeconds, const FFighterAnimationState& Previous);
};
