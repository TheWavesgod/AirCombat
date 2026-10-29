#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterPresentationComponent.generated.h"

class UJSBSimMovementComponent;
class USceneComponent;

/** Applies one interpolated aircraft pose to the mesh and both camera rigs. Never writes solver state. */
UCLASS(ClassGroup = Aircraft)
class AIRCOMBAT_API UFighterPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterPresentationComponent();
    void Initialize(UJSBSimMovementComponent* InFlightDynamics, USceneComponent* InPresentationRoot);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient) TObjectPtr<UJSBSimMovementComponent> FlightDynamics;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> PresentationRoot;
};
