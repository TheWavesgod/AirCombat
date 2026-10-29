#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Camera/FighterCameraControllerComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"

namespace FighterCameraTests
{
struct FRig
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner = World->SpawnActor<AActor>();
	USpringArmComponent* Arm = NewObject<USpringArmComponent>(Owner);
	UCameraComponent* Chase = NewObject<UCameraComponent>(Owner);
	UCameraComponent* Cockpit = NewObject<UCameraComponent>(Owner);
	UFighterCameraControllerComponent* Controller = NewObject<UFighterCameraControllerComponent>(Owner);

	FRig()
	{
		auto* Root = NewObject<USceneComponent>(Owner);
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Arm->SetupAttachment(Root);
		Arm->RegisterComponent();
		Chase->SetupAttachment(Arm, USpringArmComponent::SocketName);
		Chase->RegisterComponent();
		Cockpit->SetupAttachment(Root);
		Cockpit->RegisterComponent();
		Controller->RegisterComponent();
		Controller->Initialize(Arm, Chase, Cockpit);
		Controller->bChaseAutoRecenter = false;
	}
	~FRig() { World->DestroyWorld(false); }
	void Tick(float Dt = 1.f / 60.f)
	{
		Controller->TickComponent(Dt, LEVELTICK_All, nullptr);
		Arm->TickComponent(Dt, LEVELTICK_All, nullptr);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterCameraInputTest, "AirCombat.Camera.LookConsumptionAndModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterCameraInputTest::RunTest(const FString& Parameters)
{
	FighterCameraTests::FRig Rig;
	Rig.Controller->AddLookInput(40.f, 20.f);
	Rig.Tick();
	TestTrue(TEXT("Pixel input is applied without DeltaTime scaling"), Rig.Arm->GetTargetRotation().Equals(FRotator(5.f, 10.f, 0.f)));
	Rig.Tick();
	TestTrue(TEXT("A mouse delta is consumed once"), Rig.Arm->GetTargetRotation().Equals(FRotator(5.f, 10.f, 0.f)));
	Rig.Controller->ToggleCamera();
	TestTrue(TEXT("Mode switches to cockpit"), Rig.Controller->GetCameraMode() == EFighterCameraMode::Cockpit);
	TestTrue(TEXT("Cockpit is the only active camera"), Rig.Cockpit->IsActive() && !Rig.Chase->IsActive());
	Rig.Controller->AddLookInput(10000.f, 10000.f);
	Rig.Tick();
	TestTrue(TEXT("Cockpit head limits are enforced"), Rig.Cockpit->GetRelativeRotation().Equals(FRotator(85.f, 170.f, 0.f)));
	Rig.Controller->ToggleCamera();
	TestTrue(TEXT("Chase look is independent of cockpit look"), Rig.Arm->GetTargetRotation().Equals(FRotator(5.f, 10.f, 0.f)));
	TestTrue(TEXT("Chase is the only active camera"), Rig.Chase->IsActive() && !Rig.Cockpit->IsActive());
	Rig.Controller->AddLookInput(100.f, 100.f);
	Rig.Controller->ClearLookInput();
	Rig.Tick();
	TestTrue(TEXT("Clearing input prevents stale rotation after unpossess"), Rig.Arm->GetTargetRotation().Equals(FRotator(5.f, 10.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterCameraAttitudeTest, "AirCombat.Camera.AircraftAttitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterCameraAttitudeTest::RunTest(const FString& Parameters)
{
	FighterCameraTests::FRig Rig;
	const FRotator Attitude(40.f, 60.f, 80.f);
	Rig.Owner->SetActorRotation(Attitude);
	Rig.Tick();
	TestTrue(TEXT("Chase follows heading and keeps a level horizon"), Rig.Arm->GetTargetRotation().Equals(FRotator(0.f, 60.f, 0.f)));
	Rig.Controller->SetCameraMode(EFighterCameraMode::Cockpit);
	Rig.Controller->AddLookInput(40.f, 20.f);
	Rig.Tick();
	const FQuat Expected = Attitude.Quaternion() * FRotator(5.f, 10.f, 0.f).Quaternion();
	TestTrue(TEXT("Cockpit look is composed in aircraft space, including roll"), Rig.Cockpit->GetComponentQuat().Equals(Expected, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterCameraRecenterTest, "AirCombat.Camera.RecenterAndLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterCameraRecenterTest::RunTest(const FString& Parameters)
{
	FighterCameraTests::FRig Rig;
	Rig.Controller->bInvertCameraYaw = true;
	Rig.Controller->AddLookInput(40.f, 10000.f);
	Rig.Tick();
	TestTrue(TEXT("Inversion and chase pitch limit"), Rig.Arm->GetTargetRotation().Equals(FRotator(45.f, -10.f, 0.f)));
	Rig.Controller->bChaseAutoRecenter = true;
	Rig.Tick(0.1f);
	TestEqual(TEXT("No recenter before idle delay"), Rig.Arm->GetTargetRotation().Yaw, -10.0);
	Rig.Tick(0.6f);
	TestTrue(TEXT("Recenter preserves pitch"), Rig.Arm->GetTargetRotation().Equals(FRotator(45.f, 0.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterCameraCollisionTest, "AirCombat.Camera.SpringArmCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterCameraCollisionTest::RunTest(const FString& Parameters)
{
	FighterCameraTests::FRig Rig;
	Rig.Arm->TargetArmLength = 1000.f;
	Rig.Arm->bEnableCameraLag = false;
	Rig.Tick();
	TestTrue(TEXT("Unobstructed chase distance"), Rig.Chase->GetComponentLocation().Equals(FVector(-1000.f, 0.f, 0.f), 0.1f));
	AActor* Wall = Rig.World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(100.f, 1000.f, 1000.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(-500.f, 0.f, 0.f));
	Rig.Tick();
	TestTrue(TEXT("SpringArm retracts before the wall"), Rig.Chase->GetComponentLocation().X > -400.f);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rig.Tick();
	TestTrue(TEXT("SpringArm restores distance after obstruction clears"), Rig.Chase->GetComponentLocation().Equals(FVector(-1000.f, 0.f, 0.f), 0.1f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterCameraPresentationHeadingTest, "AirCombat.Camera.PresentationHeading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterCameraPresentationHeadingTest::RunTest(const FString& Parameters)
{
    FighterCameraTests::FRig Rig;
    auto* ViewFrame = NewObject<USceneComponent>(Rig.Owner);
    ViewFrame->SetupAttachment(Rig.Owner->GetRootComponent());
    ViewFrame->RegisterComponent();
    Rig.Owner->SetActorRotation(FRotator(0., 90., 0.));
    ViewFrame->SetWorldRotation(FRotator(0., 30., 0.));
    Rig.Controller->Initialize(Rig.Arm, Rig.Chase, Rig.Cockpit, ViewFrame);
    Rig.Tick();
    TestTrue(TEXT("Chase heading uses the interpolated visual frame instead of the newer physical heading"),
        Rig.Arm->GetTargetRotation().Equals(FRotator(0., 30., 0.)));
    ViewFrame->SetWorldRotation(FRotator(0., 35., 0.));
    Rig.Controller->AddLookInput(40.f, 0.f);
    Rig.Tick();
    TestTrue(TEXT("Current mouse look is added to the same display heading"),
        Rig.Arm->GetTargetRotation().Equals(FRotator(0., 45., 0.)));
    return true;
}
#endif
