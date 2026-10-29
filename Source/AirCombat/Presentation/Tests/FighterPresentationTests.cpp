#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "JSBSimSimulationState.h"
#include "JSBSimMovementComponent.h"
#include "Pawns/Fighter/Fighter.h"
#include "Presentation/FighterPresentationComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterClockCadenceTest, "AirCombat.Presentation.FixedClockCadence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterClockCadenceTest::RunTest(const FString& Parameters)
{
    for (const int32 FPS : {30, 60, 90, 120, 144, 240})
    {
        FJSBSimFixedStepClock Clock;
        int32 Steps = 0;
        for (int32 Frame = 0; Frame < FPS; ++Frame)
        {
            const auto Result = Clock.Advance(1. / FPS);
            Steps += Result.Steps;
            TestTrue(TEXT("Interpolation alpha stays in [0,1]"), Clock.GetAlpha() >= 0. && Clock.GetAlpha() <= 1.);
            TestEqual(TEXT("Ordinary frames drop no time"), Result.DroppedSeconds, 0.);
        }
        TestEqual(FString::Printf(TEXT("One second at %d FPS executes 120 solver steps"), FPS), Steps, 120);
        TestTrue(TEXT("No accumulated clock drift"), Clock.GetRemainderSeconds() < 1.e-10);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterClockHitchTest, "AirCombat.Presentation.HitchInvalidTimeAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterClockHitchTest::RunTest(const FString& Parameters)
{
    FJSBSimFixedStepClock Clock;
    for (double Value : {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        TestEqual(TEXT("Invalid frame time cannot advance or poison the clock"), Clock.Advance(Value).Steps, 0);
    const auto Hitch = Clock.Advance(2.);
    TestEqual(TEXT("A long hitch has bounded solver work"), Hitch.Steps, FJSBSimFixedStepClock::MaxStepsPerFrame);
    TestTrue(TEXT("Discarded hitch time is explicitly accounted for"), FMath::IsNearlyEqual(Hitch.AcceptedSeconds + Hitch.DroppedSeconds, 2., 1.e-12));
    TestTrue(TEXT("No unbounded whole-step debt survives a hitch"), Clock.GetRemainderSeconds() < FJSBSimFixedStepClock::StepSeconds);
    Clock.Advance(FJSBSimFixedStepClock::StepSeconds * .5);
    TestTrue(TEXT("Partial solver step is retained for rendering"), FMath::IsNearlyEqual(Clock.GetAlpha(), .5, 1.e-10));
    Clock.Reset();
    TestEqual(TEXT("Model resets remove old interpolation phase"), Clock.GetAlpha(), 0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterPoseHistoryTest, "AirCombat.Presentation.LastSubstepAndQuaternionInterpolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterPoseHistoryTest::RunTest(const FString& Parameters)
{
    FJSBSimPoseHistory History;
    FJSBSimPoseSample Sample;
    Sample.ECEFLocationMeters = FVector(6378137., 0., 0.);
    History.Reset(Sample);
    Sample.ECEFLocationMeters.X += 1.;
    Sample.EngineRotation = FRotator(0., 179., 0.).Quaternion();
    Sample.CGLocalCentimeters = FVector(480., 0., 0.);
    History.Push(Sample);
    Sample.ECEFLocationMeters.X += 1.;
    Sample.EngineRotation = FRotator(0., -179., 0.).Quaternion();
    Sample.CGLocalCentimeters.X = 482.;
    History.Push(Sample);
    const auto Mid = History.Interpolate(.5);
    TestTrue(TEXT("History retains the final two solver steps, not two display frames"), FMath::IsNearlyEqual(Mid.ECEFLocationMeters.X, 6378138.5));
    TestTrue(TEXT("Quaternion interpolation takes the short path through 180 degrees"), FMath::IsNearlyEqual(FMath::Abs(Mid.EngineRotation.Rotator().Yaw), 180., .001));
    TestEqual(TEXT("Moving CG is interpolated with the same pose sample"), Mid.CGLocalCentimeters.X, 481.);
    History.Reset(Sample);
    TestTrue(TEXT("Initialization seeds both endpoints without a visual jump"), History.Interpolate(.1).ECEFLocationMeters.Equals(Sample.ECEFLocationMeters));
    History.Clear();
    TestFalse(TEXT("Reload invalidates the previous aircraft pose"), History.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterRenderCadenceTest, "AirCombat.Presentation.RenderMotionAcrossCadences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterRenderCadenceTest::RunTest(const FString& Parameters)
{
    for (const int32 FPS : {30, 60, 90, 120, 144, 240, 0})
    {
        FJSBSimFixedStepClock Clock;
        FJSBSimPoseHistory History;
        FJSBSimPoseSample Sample;
        Sample.ECEFLocationMeters.X = 6378137.;
        History.Reset(Sample);
        double DisplayTime = 0.;
        int32 Steps = 0;
        constexpr double Speed = 235.;
        const double Varying[] = {1./60., 1./30., 1./144., 1./90., 1./240.};
        for (int32 Frame = 0; Frame < 300; ++Frame)
        {
            const double Dt = FPS ? 1. / FPS : Varying[Frame % UE_ARRAY_COUNT(Varying)];
            DisplayTime += Dt;
            const auto Advance = Clock.Advance(Dt);
            for (int32 Step = 0; Step < Advance.Steps; ++Step)
            {
                ++Steps;
                const double Time = Steps * FJSBSimFixedStepClock::StepSeconds;
                Sample.ECEFLocationMeters.X = 6378137. + Speed * Time;
                Sample.EngineRotation = FRotator(0., 9. * Time, 0.).Quaternion();
                History.Push(Sample);
            }
            const auto Pose = History.Interpolate(Clock.GetAlpha());
            const double TargetTime = FMath::Max(0., DisplayTime - FJSBSimFixedStepClock::StepSeconds);
            if (!TestTrue(TEXT("Constant velocity renders continuously at fixed and varying frame rates"),
                FMath::Abs(Pose.ECEFLocationMeters.X - (6378137. + Speed * TargetTime)) < 1.e-7)) return false;
            if (!TestTrue(TEXT("Heading uses the same interpolated time as position"),
                FMath::Abs(FMath::FindDeltaAngleDegrees(Pose.EngineRotation.Rotator().Yaw, TargetTime * 9.)) < .001)) return false;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterPresentationResetTest, "AirCombat.Presentation.UnavailableSourcePreservesStructuralMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterPresentationResetTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UClass* GeoClass = LoadClass<AActor>(nullptr, TEXT("/Script/GeoReferencing.GeoReferencingSystem"));
    if (!GeoClass) { World->DestroyWorld(false); AddError(TEXT("Missing georeferencing class")); return false; }
    World->SpawnActor<AActor>(GeoClass);
    AFighter* Fighter = World->SpawnActor<AFighter>();
    auto* Root = FindObject<USceneComponent>(Fighter, TEXT("PresentationRoot"));
    auto* Presentation = FindObject<UFighterPresentationComponent>(Fighter, TEXT("Presentation"));
    Presentation->Initialize(Fighter->GetFlightDynamics(), Root);
    Fighter->GetFlightDynamics()->StructuralFrameOrigin = FVector(-550.521082, 0., 182.883548);
    const FVector StructuralOrigin = Fighter->GetFlightDynamics()->StructuralFrameOrigin;
    const FTransform PhysicalActor = Fighter->GetActorTransform();
    Root->SetRelativeLocation(FVector(100., 20., 30.));
    Presentation->TickComponent(.016f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Preview or invalidated simulation clears stale presentation deltas"), Root->GetRelativeTransform().Equals(FTransform::Identity));
    TestTrue(TEXT("Clearing presentation never overwrites the structural mapping"), Fighter->GetFlightDynamics()->StructuralFrameOrigin.Equals(StructuralOrigin));
    TestTrue(TEXT("Presentation never moves the authoritative actor"), Fighter->GetActorTransform().Equals(PhysicalActor));
    World->DestroyWorld(false);
    return true;
}
#endif
