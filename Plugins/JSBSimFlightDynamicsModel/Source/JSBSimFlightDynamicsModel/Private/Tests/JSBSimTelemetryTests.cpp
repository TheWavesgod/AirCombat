#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "JSBSimMovementComponent.h"
THIRD_PARTY_INCLUDES_START
#include "FGFDMExec.h"
#include "models/FGLGear.h"
#include "models/FGGroundReactions.h"
#include "models/FGInertial.h"
#include "models/FGFCS.h"
#include "initialization/FGInitialCondition.h"
#include "input_output/FGGroundCallback.h"
THIRD_PARTY_INCLUDES_END

struct FJSBSimTelemetryTestAccess
{
    static JSBSim::FGFDMExec* Exec(UJSBSimMovementComponent& FDM) { return FDM.Exec; }
    static void Sample(UJSBSimMovementComponent& FDM) { FDM.CopyGearPropertiesFromJSBSim(); FDM.GetEnginesStates(); }
    static void Ready(UJSBSimMovementComponent& FDM)
    {
        FDM.Trimmed = true;
        FDM.UpdateLocalTransforms();
        Sample(FDM);
    }
    static void Map(UJSBSimMovementComponent& FDM) { FDM.UpdateLocalTransforms(); }
    static void Invalidate(UJSBSimMovementComponent& FDM) { FDM.DeInitializeJSBSim(); }
    static bool CacheEmpty(const UJSBSimMovementComponent& FDM) { return FDM.ReadPropertyCache.IsEmpty(); }
};

namespace
{
class FCountingGround : public JSBSim::FGDefaultGroundCallback
{
public:
    FCountingGround() : FGDefaultGroundCallback(20925646.32546, 20855486.5951) {}
    mutable int32 Queries = 0;
    double GetAGLevel(double Time, const JSBSim::FGLocation& Location, JSBSim::FGLocation& Contact,
        JSBSim::FGColumnVector3& Normal, JSBSim::FGColumnVector3& Velocity, JSBSim::FGColumnVector3& Angular) const override
    {
        ++Queries;
        return FGDefaultGroundCallback::GetAGLevel(Time, Location, Contact, Normal, Velocity, Angular);
    }
};

bool StartAt(UJSBSimMovementComponent& FDM, double Height, double Extension)
{
    auto* Exec = FJSBSimTelemetryTestAccess::Exec(FDM);
    Exec->GetFCS()->SetGearCmd(Extension);
    Exec->GetFCS()->SetGearPos(Extension);
    Exec->GetIC()->SetAltitudeAGLFtIC(Height);
    Exec->GetIC()->SetVcalibratedKtsIC(0.);
    return Exec->RunIC();
}

bool NoContactOutputs(const FGear& Gear)
{
    return !Gear.HasWeightOnWheel && Gear.CompressionMeters == 0. && Gear.CompressionVelocityMetersPerSec == 0.
        && Gear.StrutForceNewtons == 0. && Gear.Force.IsZero();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJSBSimGearSamplingTest, "AirCombat.FlightDynamics.Telemetry.ContactLifecycleAndReadPurity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FJSBSimGearSamplingTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FDM->AircraftModel = TEXT("j10ce");
    FDM->StructuralFrameOrigin = FVector(-20., 0., -40.);
    FDM->LoadAircraft(true);
    if (!TestEqual(TEXT("J10CE gear metadata loads"), FDM->Gears.Num(), 3)) return false;
    for (const auto& Gear : FDM->Gears) TestTrue(TEXT("Uninitialized model publishes no contact load"), NoContactOutputs(Gear));
    auto* Exec = FJSBSimTelemetryTestAccess::Exec(*FDM);
    auto* Ground = new FCountingGround();
    Exec->GetInertial()->SetGroundCallback(Ground);
    if (!TestTrue(TEXT("Ground initial conditions run"), StartAt(*FDM, 6., 1.))) return false;
    FJSBSimTelemetryTestAccess::Ready(*FDM);
    TestTrue(TEXT("Ground fixture has a loaded nose gear"), FDM->Gears[0].HasWeightOnWheel);
    TestTrue(TEXT("Signed compression force remains negative"), FDM->Gears[0].StrutForceNewtons < 0.);
    TestTrue(TEXT("Actor contact-force Z points up"), FDM->Gears[0].Force.Z > 0.);
    auto Gear = Exec->GetGroundReactions()->GetGearUnit(0);
    const FVector BodyForce(Gear->GetBodyXForce(), Gear->GetBodyYForce(), Gear->GetBodyZForce());
    const FVector Expected(BodyForce.X * 4.4482216152605, BodyForce.Y * 4.4482216152605, -BodyForce.Z * 4.4482216152605);
    TestTrue(TEXT("Reaction force uses newtons, not a length conversion"), FDM->Gears[0].Force.Equals(Expected, 1.e-7));
    const int32 Queries = Ground->Queries;
    const double Time = Exec->GetSimTime();
    const double Speed = Gear->GetWheelRollVel();
    for (int32 Index = 0; Index < 25; ++Index) FJSBSimTelemetryTestAccess::Sample(*FDM);
    TestEqual(TEXT("Repeated reads do not query ground or recompute contacts"), Ground->Queries, Queries);
    TestEqual(TEXT("Repeated reads do not advance solver time"), Exec->GetSimTime(), Time);
    TestEqual(TEXT("Repeated reads do not advance wheel spin-down"), Gear->GetWheelRollVel(), Speed);
    FDM->StructuralFrameOrigin += FVector(10000., 20000., -30000.);
    FJSBSimTelemetryTestAccess::Map(*FDM);
    FJSBSimTelemetryTestAccess::Sample(*FDM);
    TestTrue(TEXT("Force vectors do not acquire coordinate-origin translations"), FDM->Gears[0].Force.Equals(Expected, 1.e-7));

    // Seed a moving wheel, then remove the ground without resetting its spin state.
    Exec->GetGroundReactions()->in.UVW = JSBSim::FGColumnVector3(30., 0., 0.);
    Exec->GetGroundReactions()->in.TotalDeltaT = 1. / 120.;
    Gear->GetBodyForces();
    Ground->SetTerrainElevation(-1000.);
    Gear->GetBodyForces();
    const double AirborneSpin = Gear->GetWheelRollVel();
    const int32 AirborneQueries = Ground->Queries;
    TestTrue(TEXT("Liftoff fixture retains wheel spin"), AirborneSpin > 0.);
    for (int32 Index = 0; Index < 25; ++Index) FJSBSimTelemetryTestAccess::Sample(*FDM);
    TestEqual(TEXT("Telemetry does not repeatedly decay airborne wheel spin"), Gear->GetWheelRollVel(), AirborneSpin);
    TestEqual(TEXT("Airborne telemetry cannot re-run contact queries"), Ground->Queries, AirborneQueries);
    TestTrue(TEXT("Airborne wheel telemetry preserves real residual spin"), FDM->Gears[0].WheelRollLinearVelocityMetersPerSec > 0.);
    Ground->SetTerrainElevation(0.);
    if (!TestTrue(TEXT("Contact fixture can be restored"), StartAt(*FDM, 6., 1.))) return false;
    FJSBSimTelemetryTestAccess::Ready(*FDM);

    // Retraction can leave the native WOW and compression from the previous contact.
    Exec->GetGroundReactions()->in.FCSGearPos = .5;
    FJSBSimTelemetryTestAccess::Sample(*FDM);
    for (const auto& Out : FDM->Gears)
    {
        TestTrue(TEXT("Retraction clears stale contact outputs"), NoContactOutputs(Out));
        TestEqual(TEXT("Retracting wheel steering returns to zero"), Out.SteeringAngleDeg, 0.);
    }
    Exec->GetGroundReactions()->in.FCSGearPos = 0.;
    FJSBSimTelemetryTestAccess::Sample(*FDM);
    for (const auto& Out : FDM->Gears) TestTrue(TEXT("Fully retracted contact outputs stay zero"), NoContactOutputs(Out));
    if (!TestTrue(TEXT("Airborne initial conditions run"), StartAt(*FDM, 10000., 1.))) return false;
    FJSBSimTelemetryTestAccess::Ready(*FDM);
    for (const auto& Out : FDM->Gears) TestTrue(TEXT("Liftoff clears compression, force and WOW"), NoContactOutputs(Out));
    FJSBSimTelemetryTestAccess::Invalidate(*FDM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJSBSimColdAirTelemetryTest, "AirCombat.FlightDynamics.Telemetry.ColdAirStartAndModelReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FJSBSimColdAirTelemetryTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FDM->AircraftModel = TEXT("j10ce");
    FDM->LoadAircraft(true);
    if (!TestTrue(TEXT("Retracted air start runs IC"), StartAt(*FDM, 10000., 0.))) return false;
    FJSBSimTelemetryTestAccess::Ready(*FDM);
    for (const auto& Gear : FDM->Gears) TestTrue(TEXT("Cold-start garbage never reaches UE contact telemetry"), NoContactOutputs(Gear));
    FDM->Gears[0].IsFrontBogey = true;
    FDM->Tanks[0].ContentGallons = 123.;
    FDM->AircraftState.Crashed = true;
    FDM->AircraftState.CalibratedAirSpeedKts = 999.;
    FDM->AircraftState.ECEFLocation = FVector(1., 2., 3.);
    FDM->EngineStates[0].N1 = 999.;
    double Value = 0.;
    TestTrue(TEXT("Read cache has a live-model node"), FDM->ReadPropertyValue(TEXT("inertia/cg-x-in"), Value));
    const uint32 Generation = FDM->GetModelGeneration();
    FDM->LoadAircraft(false);
    TestFalse(TEXT("Reload invalidates runtime readiness"), FDM->IsSimulationReady());
    TestTrue(TEXT("Reload advances the model generation"), FDM->GetModelGeneration() > Generation);
    TestTrue(TEXT("Property cache cannot retain old-model nodes"), FJSBSimTelemetryTestAccess::CacheEmpty(*FDM));
    TestFalse(TEXT("Crash flag is not carried into the new model"), FDM->AircraftState.Crashed);
    TestEqual(TEXT("Old airspeed is removed immediately"), FDM->AircraftState.CalibratedAirSpeedKts, 0.);
    TestTrue(TEXT("Old navigation is removed immediately"), FDM->AircraftState.ECEFLocation.IsZero());
    TestEqual(TEXT("Old engine output is removed immediately"), FDM->EngineStates[0].N1, 0.);
    TestTrue(TEXT("Authored gear label survives a same-model reload"), FDM->Gears[0].IsFrontBogey);
    TestEqual(TEXT("Authored fuel level survives reset-to-defaults=false"), FDM->Tanks[0].ContentGallons, 123.);
    FDM->AircraftModel = TEXT("aircombat_missing_telemetry_model");
    AddExpectedError(TEXT("Error while loading Model"), EAutomationExpectedErrorFlags::Contains, 1);
    FDM->LoadAircraft(false);
    TestFalse(TEXT("Failed model cannot appear ready"), FDM->IsSimulationReady());
    TestTrue(TEXT("Failed load has no previous gears"), FDM->Gears.IsEmpty());
    TestTrue(TEXT("Failed load has no previous engine states"), FDM->EngineStates.IsEmpty());
    TestTrue(TEXT("Failed load has no previous reference points"), FDM->CGLocalPosition.IsZero() && FDM->EPLocalPosition.IsZero() && FDM->VRPLocalPosition.IsZero());
    FString Output = TEXT("stale");
    FDM->CommandConsole(TEXT("inertia/cg-x-in"), TEXT(""), Output);
    TestTrue(TEXT("Unavailable console queries clear output"), Output.IsEmpty());
    TArray<FString> Catalog = {TEXT("stale")};
    FDM->PropertyManagerNode(Catalog);
    TestTrue(TEXT("Unavailable catalogs clear previous entries"), Catalog.IsEmpty());
    FJSBSimTelemetryTestAccess::Invalidate(*FDM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJSBSimEngineTelemetryResetTest, "AirCombat.FlightDynamics.Telemetry.EngineTypeAndQueryReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FJSBSimEngineTelemetryResetTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FDM->AircraftModel = TEXT("c172p");
    FDM->LoadAircraft(true);
    if (!TestTrue(TEXT("Piston fixture initializes"), StartAt(*FDM, 10000., 1.))) return false;
    FJSBSimTelemetryTestAccess::Ready(*FDM);
    FJSBSimTelemetryTestAccess::Exec(*FDM)->GetGroundReactions()->in.FCSGearPos = 0.;
    FJSBSimTelemetryTestAccess::Sample(*FDM);
    for (const auto& Gear : FDM->Gears)
        TestTrue(TEXT("Fixed landing gear remains fully down when the global retract input is zero"),
            Gear.NormalizedPosition == 1. && Gear.IsDown && !Gear.IsUp);
    FDM->EngineStates[0].N1 = 88.;
    FDM->EngineStates[0].N2 = 99.;
    FDM->EngineStates[0].HasNozzlePosition = true;
    FDM->EngineStates[0].NozzlePosition = .8;
    FDM->EngineStates[0].Augmentation = true;
    FJSBSimTelemetryTestAccess::Sample(*FDM);
    const auto& State = FDM->EngineStates[0];
    TestEqual(TEXT("Actual piston type"), State.EngineType, EEngineType::Piston);
    TestTrue(TEXT("Non-turbines cannot retain turbine fields"), State.N1 == 0. && State.N2 == 0. && !State.HasNozzlePosition && State.NozzlePosition == 0. && !State.Augmentation);
    TArray<FString> Outputs = {TEXT("stale"), TEXT("stale"), TEXT("stale")};
    FDM->CommandConsoleBatch({TEXT("inertia/cg-x-in"), TEXT("does/not/exist"), TEXT("invalid path")}, {}, Outputs);
    TestEqual(TEXT("Batch query output count"), Outputs.Num(), 3);
    TestFalse(TEXT("Short or empty input lists default to reads"), Outputs[0].IsEmpty());
    TestTrue(TEXT("Missing and invalid paths clear recycled output slots"), Outputs[1].IsEmpty() && Outputs[2].IsEmpty());
    FJSBSimTelemetryTestAccess::Invalidate(*FDM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJSBSimAircraftStateResetTest, "AirCombat.FlightDynamics.Telemetry.FullAircraftStateReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FJSBSimAircraftStateResetTest::RunTest(const FString& Parameters)
{
    FAircraftState State;
    State.Crashed = true;
    State.ElevatorPosition = 12.;
    State.CalibratedAirSpeedKts = State.GroundSpeedKts = State.TotalVelocityKts = 456.;
    State.AltitudeASLFt = State.AltitudeAGLFt = State.AltitudeRateFtps = 123.;
    State.StallWarning = State.Latitude = State.Longitude = 1.;
    State.VelocityNEDfps = State.ECEFLocation = State.EulerRates = State.UEForwardHorizontal = FVector(1., 2., 3.);
    State.LocalEulerAngles = FRotator(10., 20., 30.);
    State.Reset();
    const FAircraftState Default;
    TestTrue(TEXT("Reset clears all reflected state, not only articulations"), FAircraftState::StaticStruct()->CompareScriptStruct(&State, &Default, 0));
    return true;
}
#endif
