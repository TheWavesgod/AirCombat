#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Debug/Flight/FighterFlightDebugSnapshot.h"
#include "Pawns/Fighter/Fighter.h"
#include "Animation/FighterAnimationBindings.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
const FFighterFlightDebugMetric& Metric(const TArray<FFighterFlightDebugMetric>& Rows, const FName Id)
{
    const auto* Found = Rows.FindByPredicate([&](const auto& M) { return M.Id == Id; });
    check(Found); return *Found;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightDebugAvailabilityTest, "AirCombat.FlightDebug.AvailabilityAndFormatting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFlightDebugAvailabilityTest::RunTest(const FString& Parameters)
{
    const auto S = FFighterFlightDebugSnapshotBuilder::Build(nullptr, EFighterFlightDebugPage::Overview);
    TestFalse(TEXT("No possessed fighter is explicitly unavailable"), S.bHasAircraft);
    for (const auto& M : S.Overview) TestEqual(TEXT("No fabricated zeros for missing data"), M.Format(), FString(TEXT("N/A")));
    FFighterFlightDebugMetric M; M.bAvailable = true; M.Value = -.00001; M.Decimals = 2; M.Unit = TEXT("g");
    TestEqual(TEXT("Small negative zero has a stable unit-labelled representation"), M.Format(), FString(TEXT("0.00 g")));
    M.bAvailable = false; TestEqual(TEXT("Missing measurement differs from zero"), M.Format(), FString(TEXT("N/A")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightDebugHistoryTest, "AirCombat.FlightDebug.PhysicsWindowAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFlightDebugHistoryTest::RunTest(const FString& Parameters)
{
    FFighterFlightDebugHistory H;
    for (int32 Index = 0; Index <= 14 * 120; ++Index)
    {
        FFighterFlightDebugHistory::FSample S;
        S.Time = Index / 120.; S.Alpha = 2.; S.Nz = Index == 901 ? 9. : 1.; S.Q = 0.; S.Valid = 7;
        H.Add(S);
    }
    const auto Window = H.Window(14.);
    TestTrue(TEXT("Storage is bounded to a ten-second fixed-step window"), H.Num() <= 1201 && Window.Num() >= 1200);
    TestTrue(TEXT("A single fixed-step G spike survives a slower UI refresh"), Window.ContainsByPredicate([](const auto& S) { return S.Nz == 9.; }));
    FFighterFlightDebugHistory::FSample S; S.Time = 14.;
    const int32 Count = H.Num(); H.Add(S); TestEqual(TEXT("Duplicate solver times do not manufacture samples"), H.Num(), Count);
    S.Time = 1.; S.Valid = 7; S.Alpha = std::numeric_limits<double>::quiet_NaN(); H.Add(S);
    TestEqual(TEXT("Rewinding simulation time clears the old history"), H.Num(), 1);
    TestFalse(TEXT("Nonfinite values never enter graph series"), (H.Window(1.)[0].Valid & 1) != 0);
    H.Reset(); TestEqual(TEXT("Hiding/switching sources can clear all samples"), H.Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightDebugSnapshotTest, "AirCombat.FlightDebug.UnitsActualStateAndModelBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFlightDebugSnapshotTest::RunTest(const FString& Parameters)
{
    auto* W = UWorld::CreateWorld(EWorldType::Game, false);
    UClass* Geo = LoadClass<AActor>(nullptr, TEXT("/Script/GeoReferencing.GeoReferencingSystem"));
    if (!Geo) { AddError(TEXT("Missing GeoReferencing")); W->DestroyWorld(false); return false; }
    W->SpawnActor<AActor>(Geo);
    auto* Fighter = W->SpawnActor<AFighter>(); auto* FDM = Fighter->GetFlightDynamics();
    FDM->AircraftModel = TEXT("j10ce"); FDM->LoadAircraft(true); FDM->Trimmed = true;
    FDM->AircraftState.CalibratedAirSpeedKts = 250.; FDM->AircraftState.TotalVelocityKts = 300.;
    FDM->AircraftState.AltitudeASLFt = 1000.; FDM->AircraftState.AltitudeAGLFt = 100.;
    FDM->AircraftState.AltitudeRateFtps = -10.; FDM->AircraftState.EulerRates = FVector(999.);
    FDM->Gears[0].CompressionMeters = .125; FDM->Gears[0].StrutForceNewtons = -12345.;
    auto* Bindings = NewObject<UFighterAnimationBindings>(); Bindings->AircraftModel = TEXT("j10ce");
    auto& Surface = Bindings->Surfaces.AddDefaulted_GetRef(); Surface.Id = TEXT("TestAlpha"); Surface.Property = TEXT("aero/alpha-rad"); Surface.DegreesPerUnit = 180.f / PI;
    auto& Missing = Bindings->Surfaces.AddDefaulted_GetRef(); Missing.Id = TEXT("Missing"); Missing.bOptional = true;
    FindFProperty<FObjectPropertyBase>(AFighter::StaticClass(), TEXT("AnimationBindings"))->SetObjectPropertyValue_InContainer(Fighter, Bindings);
    const auto S = FFighterFlightDebugSnapshotBuilder::Build(Fighter, EFighterFlightDebugPage::Response);
    TestTrue(TEXT("Live model readiness is reported"), S.bReady);
    TestEqual(TEXT("CAS is the solver calibrated airspeed"), Metric(S.Overview, TEXT("CAS")).Value, 250.);
    TestEqual(TEXT("TAS does not come from display-mesh differences"), Metric(S.Overview, TEXT("TAS")).Value, 300.);
    TestTrue(TEXT("Altitude explicitly converts feet to meters"), FMath::IsNearlyEqual(Metric(S.Overview, TEXT("ASL")).Value, 304.8));
    TestTrue(TEXT("Vertical speed keeps its sign and SI unit"), FMath::IsNearlyEqual(Metric(S.Overview, TEXT("VS")).Value, -3.048));
    TestTrue(TEXT("p is a body rate, not the Euler-rate vector"), FMath::Abs(Metric(S.Details, TEXT("p")).Value) < 1.);
    TestTrue(TEXT("Matching surface properties are discovered dynamically"), Metric(S.Details, TEXT("Surface.TestAlpha")).bAvailable);
    TestFalse(TEXT("Unsupported channels remain N/A"), Metric(S.Details, TEXT("Surface.Missing")).bAvailable);
    const auto Gear = FFighterFlightDebugSnapshotBuilder::Build(Fighter, EFighterFlightDebugPage::Gear);
    const FString Name = FDM->Gears[0].Name;
    TestEqual(TEXT("Suspension converts meters to centimeters"), Metric(Gear.Details, FName(*(Name + TEXT("Compression")))).Value, 12.5);
    TestEqual(TEXT("Signed force converts newtons to kilonewtons"), Metric(Gear.Details, FName(*(Name + TEXT("Strut")))).Value, -12.345);
    FDM->Trimmed = false;
    const auto Invalid = FFighterFlightDebugSnapshotBuilder::Build(Fighter, EFighterFlightDebugPage::Gear);
    TestFalse(TEXT("Invalidated model cannot publish previous live airspeed"), Metric(Invalid.Overview, TEXT("CAS")).bAvailable);
    W->DestroyWorld(false);
    return true;
}
#endif
