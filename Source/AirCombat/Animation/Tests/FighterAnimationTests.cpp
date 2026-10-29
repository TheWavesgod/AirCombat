#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Animation/FighterAnimInstance.h"
#include "Animation/FighterAnimationStateBuilder.h"
#include "JSBSimMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterSurfaceSnapshotTest, "AirCombat.Animation.SurfaceAvailabilityAndUnits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterSurfaceSnapshotTest::RunTest(const FString& Parameters)
{
    FFighterSurfaceAnimationBinding Binding;
    Binding.Id = TEXT("StabilatorLeft"); Binding.DegreesPerUnit = 180.f / PI; Binding.MinimumDeg = -30.f; Binding.MaximumDeg = 30.f;
    auto State = FFighterAnimationStateBuilder::Surface(Binding, true, PI / 12.);
    TestTrue(TEXT("Radians become degrees"), FMath::IsNearlyEqual(State.AngleDeg, 15.f));
    TestTrue(TEXT("Signed interval becomes 0..1"), FMath::IsNearlyEqual(State.Position01, .75f));
    State = FFighterAnimationStateBuilder::Surface(Binding, true, 0.);
    TestTrue(TEXT("Zero is an available stationary surface"), State.bAvailable);
    State = FFighterAnimationStateBuilder::Surface(Binding, false, 0.);
    TestFalse(TEXT("Missing property differs from zero"), State.bAvailable);
    State = FFighterAnimationStateBuilder::Surface(Binding, true, std::numeric_limits<double>::quiet_NaN());
    TestFalse(TEXT("NaN never reaches the graph"), State.bAvailable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterSuspensionSnapshotTest, "AirCombat.Animation.ActualGearAndSuspension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterSuspensionSnapshotTest::RunTest(const FString& Parameters)
{
    FGear Source; Source.IsBogey = true; Source.NormalizedPosition = .35; Source.HasWeightOnWheel = true;
    Source.CompressionMeters = .05; Source.CompressionVelocityMetersPerSec = -.4;
    Source.StrutForceNewtons = 1234.; Source.SteeringAngleDeg = -12.; Source.WheelRollLinearVelocityMetersPerSec = .2;
    FFighterGearAnimationBinding Binding; Binding.Id = TEXT("Nose"); Binding.MaximumCompressionCm = 20.f; Binding.WheelRadiusCm = 10.f;
    const auto State = FFighterAnimationStateBuilder::Gear(Binding, &Source, 1.f, 0.f);
    TestTrue(TEXT("Uses fractional actual gear extension"), FMath::IsNearlyEqual(State.Extension01, .35f));
    TestEqual(TEXT("Real compression in cm"), State.CompressionCm, 5.f);
    TestEqual(TEXT("Signed compression speed in cm/s"), State.CompressionSpeedCmPerSec, -40.f);
    TestEqual(TEXT("Suspension clip alpha"), State.SuspensionAlpha, .25f);
    TestEqual(TEXT("Actual steering"), State.SteeringAngleDeg, -12.f);
    TestEqual(TEXT("Actual strut force"), State.StrutForceNewtons, 1234.f);
    const auto Half = FFighterAnimationStateBuilder::Gear(Binding, &Source, .5f, 0.f);
    const auto SecondHalf = FFighterAnimationStateBuilder::Gear(Binding, &Source, .5f, Half.WheelRotationDeg);
    TestTrue(TEXT("Wheel integration is frame-rate independent at constant speed"), FMath::IsNearlyEqual(SecondHalf.WheelRotationDeg, State.WheelRotationDeg));
    Binding.MaximumCompressionCm = 0.f; Binding.WheelRadiusCm = 0.f;
    const auto Unconfigured = FFighterAnimationStateBuilder::Gear(Binding, &Source, 1.f, 0.f);
    TestFalse(TEXT("Artist's suspension travel is not guessed"), Unconfigured.bHasSuspensionRange);
    TestFalse(TEXT("Wheel radius is not guessed"), Unconfigured.bHasWheelRadius);
    TestEqual(TEXT("Raw suspension remains usable without clips"), Unconfigured.CompressionCm, 5.f);
    TestFalse(TEXT("Missing landing gear cannot be animated as real data"), FFighterAnimationStateBuilder::Gear(Binding, nullptr, 1.f, 0.f).bAvailable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterAnimationLifecycleTest, "AirCombat.Animation.PreviewAndInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterAnimationLifecycleTest::RunTest(const FString& Parameters)
{
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    auto* Anim = NewObject<UFighterAnimInstance>(Mesh);
    Anim->AnimationState.bValid = true;
    Anim->AnimationState.Engines.AddDefaulted();
    Anim->NativeInitializeAnimation();
    TestFalse(TEXT("Preview without a Pawn starts invalid"), Anim->AnimationState.bValid);
    Anim->AnimationState.Gears.AddDefaulted();
    Anim->NativeUpdateAnimation(.1f);
    TestEqual(TEXT("Unavailable owner clears stale gear"), Anim->AnimationState.Gears.Num(), 0);
    TestFalse(TEXT("Unknown channels remain explicitly unavailable"), Anim->GetSurfaceState(TEXT("Missing")).bAvailable);
    TestTrue(TEXT("Unbound suspension leaves the bone alone"), Anim->GetSuspensionTranslation(TEXT("Nose"), FVector::ZAxisVector).IsZero());
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    double Value = 1.;
    TestFalse(TEXT("Property reads before simulation initialization are safe"), FDM->ReadPropertyValue(TEXT("fcs/no-such-property"), Value));
    TestEqual(TEXT("Failed reads reset output"), Value, 0.);
    Anim->NativeUninitializeAnimation();
    TestFalse(TEXT("Uninitialization clears snapshot"), Anim->AnimationState.bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterAnimationBindingsTest, "AirCombat.Animation.F16BindingsAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterAnimationBindingsTest::RunTest(const FString& Parameters)
{
    auto* Bindings = LoadObject<UFighterAnimationBindings>(nullptr, TEXT("/Game/Data/Animation/DA_F16_AnimationBindings.DA_F16_AnimationBindings"));
    if (!TestNotNull(TEXT("Current aircraft have explicit model bindings"), Bindings)) return false;
    TArray<FString> Problems; Bindings->CollectProblems(Problems);
    for (const auto& Problem : Problems) AddError(Problem);
    TestEqual(TEXT("Three independently named landing gears"), Bindings->Gears.Num(), 3);
    for (FName Id : {FName(TEXT("StabilatorLeft")), FName(TEXT("StabilatorRight")), FName(TEXT("LeadingEdgeFlapLeft")), FName(TEXT("TrailingEdgeFlapLeft")), FName(TEXT("SpeedBrakeLeft"))})
        TestTrue(TEXT("Current F16 mechanical channels are bound"), Bindings->Surfaces.ContainsByPredicate([&](const auto& Item) { return Item.Id == Id; }));
    return Problems.IsEmpty();
}
#endif

