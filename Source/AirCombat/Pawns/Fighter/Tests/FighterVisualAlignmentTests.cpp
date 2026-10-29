#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Pawns/Fighter/Fighter.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterFixedVisualFrameTest, "AirCombat.Visual.FixedFrameAndCameraAnchors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterFixedVisualFrameTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UClass* GeoClass = LoadClass<AActor>(nullptr, TEXT("/Script/GeoReferencing.GeoReferencingSystem"));
    if (!TestNotNull(TEXT("The FDM's georeferencing dependency is loaded"), GeoClass))
    {
        World->DestroyWorld(false);
        return false;
    }
    World->SpawnActor<AActor>(GeoClass);
    AFighter* Fighter = World->SpawnActor<AFighter>();
    auto* Display = FindObject<USceneComponent>(Fighter, TEXT("PresentationRoot"));
    auto* CG = FindObject<USceneComponent>(Fighter, TEXT("AircraftCG"));
    auto* Mesh = FindObject<USkeletalMeshComponent>(Fighter, TEXT("Airframe"));
    auto* CockpitAnchor = FindObject<USceneComponent>(Fighter, TEXT("CockpitAnchor"));
    auto* Focus = FindObject<USceneComponent>(Fighter, TEXT("ChaseFocus"));
    auto* Arm = FindObject<USpringArmComponent>(Fighter, TEXT("ChaseArm"));
    auto* Cockpit = FindObject<UCameraComponent>(Fighter, TEXT("CockpitCamera"));
    if (!Display || !CG || !Mesh || !CockpitAnchor || !Focus || !Arm || !Cockpit)
    {
        AddError(TEXT("The fixed visual hierarchy is incomplete"));
        World->DestroyWorld(false);
        return false;
    }
    TestNull(TEXT("There is no second mesh calibration component"), FindObject<USceneComponent>(Fighter, TEXT("VisualRoot")));
    TestTrue(TEXT("Imported mesh space equals the shared display frame"), Mesh->GetAttachParent() == Display
        && Mesh->GetRelativeTransform().Equals(FTransform::Identity));
    TestTrue(TEXT("Both camera anchors share mesh space"), CockpitAnchor->GetAttachParent() == Display && Focus->GetAttachParent() == Display);
    Fighter->GetFlightDynamics()->StructuralFrameOrigin = FVector(-550., 0., 190.);
    CockpitAnchor->SetRelativeLocation(FVector(320., 0., 260.));
    Focus->SetRelativeLocation(FVector(-200., 0., 190.));
    Fighter->SetActorLocationAndRotation(FVector(123., 456., 789.), FRotator(25., 70., 40.));
    const FTransform MeshBefore = Mesh->GetComponentTransform();
    const FVector EyeBefore = Cockpit->GetComponentLocation();
    const FVector FocusBefore = Focus->GetComponentLocation();
    auto* FDM = Fighter->GetFlightDynamics();
    const FVector StructuralOriginBefore = FDM->StructuralFrameOrigin;
    FDM->CGLocalPosition = FVector(487., 0., -9.);
    Fighter->Tick(.016f);
    FDM->CGLocalPosition = FVector(450., 20., -15.);
    Fighter->Tick(.016f);
    TestTrue(TEXT("Fuel/load CG changes cannot move visual geometry within the aircraft"), Mesh->GetComponentTransform().Equals(MeshBefore));
    TestTrue(TEXT("Pilot eye position stays rigidly attached to the cockpit"), Cockpit->GetComponentLocation().Equals(EyeBefore));
    TestTrue(TEXT("Chase focus is independent of CG drift"), Focus->GetComponentLocation().Equals(FocusBefore));
    TestTrue(TEXT("CG marker still reports the current physical CG"), CG->GetRelativeLocation().Equals(FDM->CGLocalPosition));
    TestTrue(TEXT("CG drift cannot change the authored structural datum"), FDM->StructuralFrameOrigin.Equals(StructuralOriginBefore));
    TestTrue(TEXT("SpringArm uses the authored visual focus"), Arm->GetAttachParent() == Focus);
    World->DestroyWorld(false);
    return true;
}

#endif
