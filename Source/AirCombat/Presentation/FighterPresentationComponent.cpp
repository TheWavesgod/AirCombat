#include "Presentation/FighterPresentationComponent.h"
#include "JSBSimMovementComponent.h"
#include "Components/SceneComponent.h"

UFighterPresentationComponent::UFighterPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UFighterPresentationComponent::Initialize(UJSBSimMovementComponent* InFlightDynamics, USceneComponent* InPresentationRoot)
{
    FlightDynamics = InFlightDynamics;
    PresentationRoot = InPresentationRoot;
    AddTickPrerequisiteActor(GetOwner());
    if (FlightDynamics) AddTickPrerequisiteComponent(FlightDynamics);
}

void UFighterPresentationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() == NM_DedicatedServer) SetComponentTickEnabled(false);
}

void UFighterPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!IsValid(PresentationRoot)) return;
    FTransform Pose;
    if (IsValid(FlightDynamics) && FlightDynamics->GetInterpolatedActorTransform(Pose))
    {
        // The parent is the latest physical actor pose. This world setter produces only a transient display delta.
        PresentationRoot->SetWorldLocationAndRotation(Pose.GetLocation(), Pose.GetRotation());
    }
    else
    {
        // Preview, model reload and failed initialization never retain an old visual offset.
        PresentationRoot->SetRelativeTransform(FTransform::Identity);
    }
}
