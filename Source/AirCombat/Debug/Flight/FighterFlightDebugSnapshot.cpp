#include "Debug/Flight/FighterFlightDebugSnapshot.h"
#include "Pawns/Fighter/Fighter.h"
#include "Animation/FighterAnimationBindings.h"
#include "Engine/World.h"

namespace
{
constexpr double LbToKg = .45359237, LbfToN = 4.4482216152605, RadToDeg = 180. / PI;
void Add(TArray<FFighterFlightDebugMetric>& Rows, const FString& Id, const FString& Label,
    const FString& Unit, double Value, bool Available, int32 Decimals = 1)
{
    auto& M = Rows.AddDefaulted_GetRef();
    M.Id = FName(*Id); M.Label = Label; M.Unit = Unit;
    M.bAvailable = Available && FMath::IsFinite(Value);
    M.Value = M.bAvailable ? Value : 0.; M.Decimals = Decimals;
}
void Text(TArray<FFighterFlightDebugMetric>& Rows, const FString& Id, const FString& Label, const FString& Value, bool Available = true)
{
    Add(Rows, Id, Label, TEXT(""), 0., Available);
    Rows.Last().bIsText = true; Rows.Last().Text = Value;
}
void Property(TArray<FFighterFlightDebugMetric>& Rows, UJSBSimMovementComponent* FDM,
    const FString& Id, const FString& Label, const FString& Path, const FString& Unit, double Scale = 1., int32 Decimals = 1)
{
    double Value = 0.;
    const bool Available = FDM && FDM->ReadPropertyValue(Path, Value);
    Add(Rows, Id, Label, Unit, Value * Scale, Available, Decimals);
}
}

FString FFighterFlightDebugMetric::Format() const
{
    if (!bAvailable) return TEXT("N/A");
    if (bIsText) return Text;
    const int32 Digits = FMath::Clamp(Decimals, 0, 4);
    const double RoundedZero = .5 * FMath::Pow(10., -Digits);
    return FString::Printf(TEXT("%.*f%s%s"), Digits, FMath::Abs(Value) < RoundedZero ? 0. : Value,
        Unit.IsEmpty() ? TEXT("") : TEXT(" "), *Unit);
}

const TCHAR* FFighterFlightDebugSnapshotBuilder::PageName(EFighterFlightDebugPage Page)
{
    switch (Page)
    {
    case EFighterFlightDebugPage::Response: return TEXT("RESPONSE");
    case EFighterFlightDebugPage::Power: return TEXT("POWER / FUEL");
    case EFighterFlightDebugPage::Gear: return TEXT("GEAR / CONTACTS");
    case EFighterFlightDebugPage::Aero: return TEXT("AERO / MASS (JSBSim body axes)");
    case EFighterFlightDebugPage::Simulation: return TEXT("SIMULATION");
    default: return TEXT("OVERVIEW / BODY RATES");
    }
}

FFighterFlightDebugSnapshot FFighterFlightDebugSnapshotBuilder::Build(AFighter* Fighter, EFighterFlightDebugPage Page)
{
    check(IsInGameThread());
    FFighterFlightDebugSnapshot S;
    UJSBSimMovementComponent* FDM = IsValid(Fighter) ? Fighter->GetFlightDynamics() : nullptr;
    S.bHasAircraft = IsValid(FDM);
    S.bReady = S.bHasAircraft && FDM->IsSimulationReady();
    if (S.bHasAircraft)
    {
        S.Model = FDM->AircraftModel; S.PawnName = Fighter->GetName();
        S.bCrashed = FDM->AircraftState.Crashed;
        S.ModelGeneration = static_cast<int32>(FDM->GetModelGeneration());
        const TCHAR* Start = Fighter->GetStartMode() == EFighterStartMode::Ground ? TEXT("GROUND") : TEXT("AIR");
        S.Status = FString::Printf(TEXT("%s | %s"), Start, S.bCrashed ? TEXT("CRASHED") : S.bReady ? TEXT("TRIM READY") : TEXT("NOT READY / TRIM REQUIRED"));
        S.bHasSimulationTime = FDM->ReadPropertyValue(TEXT("simulation/sim-time-sec"), S.SimulationSeconds);
    }
    auto& O = S.Overview; auto& D = S.Details;
    const FAircraftState State = S.bReady ? FDM->AircraftState : FAircraftState{};
    Add(O, TEXT("CAS"), TEXT("CAS"), TEXT("kt"), State.CalibratedAirSpeedKts, S.bReady);
    Add(O, TEXT("TAS"), TEXT("TAS"), TEXT("kt"), State.TotalVelocityKts, S.bReady);
    Add(O, TEXT("GS"), TEXT("Ground speed"), TEXT("kt"), State.GroundSpeedKts, S.bReady);
    Property(O, FDM, TEXT("Mach"), TEXT("Mach"), TEXT("velocities/mach"), TEXT(""), 1., 3);
    Add(O, TEXT("ASL"), TEXT("ASL"), TEXT("m"), State.AltitudeASLFt * FEET_TO_METER, S.bReady);
    Add(O, TEXT("AGL"), TEXT("AGL"), TEXT("m"), State.AltitudeAGLFt * FEET_TO_METER, S.bReady);
    Add(O, TEXT("VS"), TEXT("Vertical speed"), TEXT("m/s"), State.AltitudeRateFtps * FEET_TO_METER, S.bReady);
    Property(O, FDM, TEXT("Nz"), TEXT("CG normal load"), TEXT("accelerations/Nz"), TEXT("g"), 1., 2);
    Property(O, FDM, TEXT("Alpha"), TEXT("Alpha"), TEXT("aero/alpha-deg"), TEXT("deg"));
    Property(O, FDM, TEXT("Beta"), TEXT("Beta"), TEXT("aero/beta-deg"), TEXT("deg"));
    Add(O, TEXT("Pitch"), TEXT("Pitch"), TEXT("deg"), State.LocalEulerAngles.Pitch, S.bReady);
    Add(O, TEXT("Roll"), TEXT("Roll"), TEXT("deg"), State.LocalEulerAngles.Roll, S.bReady);
    Add(O, TEXT("HDG"), TEXT("True heading"), TEXT("deg"), FMath::Fmod(State.LocalEulerAngles.Yaw + 360., 360.), S.bReady);
    Property(O, FDM, TEXT("Fuel"), TEXT("Total fuel"), TEXT("propulsion/total-fuel-lbs"), TEXT("kg"), LbToKg);
    const bool HasEngine = S.bReady && !FDM->EngineStates.IsEmpty();
    Add(O, TEXT("Throttle"), TEXT("Throttle E0"), TEXT("%"), HasEngine && !FDM->EngineCommands.IsEmpty() ? FDM->EngineCommands[0].Throttle * 100. : 0., HasEngine);
    double Thrust = 0.; if (HasEngine) for (const auto& E : FDM->EngineStates) Thrust += E.Thrust;
    Add(O, TEXT("Thrust"), TEXT("Total thrust"), TEXT("kN"), Thrust * LbfToN / 1000., HasEngine);

    if (Page == EFighterFlightDebugPage::Overview || Page == EFighterFlightDebugPage::Response)
        for (const TCHAR* Axis : {TEXT("p"), TEXT("q"), TEXT("r")})
            Property(D, FDM, Axis, FString(Axis) + TEXT(" body rate"), FString::Printf(TEXT("velocities/%s-rad_sec"), Axis), TEXT("deg/s"), RadToDeg);
    if (!S.bHasAircraft) return S;
    switch (Page)
    {
    case EFighterFlightDebugPage::Response:
    {
        const auto& C = FDM->Commands;
        Add(D, TEXT("RollCmd"), TEXT("JSB roll cmd"), TEXT("norm"), C.Aileron, S.bReady, 3);
        Add(D, TEXT("PitchCmd"), TEXT("JSB pitch cmd"), TEXT("norm"), C.Elevator, S.bReady, 3);
        Add(D, TEXT("YawCmd"), TEXT("JSB yaw cmd"), TEXT("norm"), C.Rudder, S.bReady, 3);
        Add(D, TEXT("PitchTrim"), TEXT("Pitch trim"), TEXT("norm"), C.PitchTrim, S.bReady, 3);
        Add(D, TEXT("RollTrim"), TEXT("Roll trim"), TEXT("norm"), C.RollTrim, S.bReady, 3);
        Add(D, TEXT("YawTrim"), TEXT("Yaw trim"), TEXT("norm"), C.YawTrim, S.bReady, 3);
        const auto* Bindings = Fighter->GetAnimationBindings();
        const bool Match = Bindings && Bindings->AircraftModel.Equals(FDM->AircraftModel, ESearchCase::IgnoreCase);
        Text(D, TEXT("Bindings"), TEXT("Surface mapping"), Match ? TEXT("MODEL MATCH") : TEXT("MISSING / MISMATCH"));
        if (Match) for (const auto& B : Bindings->Surfaces)
        {
            double Value = 0.; const bool Found = FDM->ReadPropertyValue(B.Property, Value);
            const double Converted = Value * B.DegreesPerUnit + B.OffsetDeg;
            Add(D, TEXT("Surface.") + B.Id.ToString(), B.Id.ToString(), B.bNormalizedPositionOnly ? TEXT("%") : TEXT("deg"),
                B.bNormalizedPositionOnly ? FMath::Clamp(Converted, 0., 1.) * 100. : Converted, Found);
        }
        break;
    }
    case EFighterFlightDebugPage::Power:
        for (int32 Index = 0; Index < FDM->EngineStates.Num(); ++Index)
        {
            const auto& E = FDM->EngineStates[Index]; const FString Prefix = FString::Printf(TEXT("E%d "), Index);
            Text(D, Prefix + TEXT("Running"), Prefix + TEXT("running / AB"), FString::Printf(TEXT("%s / %s"), E.Running ? TEXT("RUN") : TEXT("OFF"),
                E.EngineType == EEngineType::Turbine ? (E.Augmentation ? TEXT("AB ON") : TEXT("AB OFF")) : TEXT("AB N/A")), S.bReady);
            Add(D, Prefix + TEXT("Throttle"), Prefix + TEXT("throttle"), TEXT("%"), FDM->EngineCommands.IsValidIndex(Index) ? FDM->EngineCommands[Index].Throttle * 100. : 0., S.bReady);
            Add(D, Prefix + TEXT("N1"), Prefix + TEXT("N1"), TEXT("%"), E.N1, S.bReady && E.EngineType == EEngineType::Turbine);
            Add(D, Prefix + TEXT("N2"), Prefix + TEXT("N2"), TEXT("%"), E.N2, S.bReady && E.EngineType == EEngineType::Turbine);
            Add(D, Prefix + TEXT("Thrust"), Prefix + TEXT("thrust"), TEXT("kN"), E.Thrust * LbfToN / 1000., S.bReady);
            Add(D, Prefix + TEXT("Nozzle"), Prefix + TEXT("nozzle"), TEXT("%"), E.NozzlePosition * 100., S.bReady && E.HasNozzlePosition);
            Property(D, FDM, Prefix + TEXT("FuelFlow"), Prefix + TEXT("fuel flow"), FString::Printf(TEXT("propulsion/engine[%d]/fuel-flow-rate-pps"), Index), TEXT("kg/s"), LbToKg, 3);
        }
        for (int32 Index = 0; Index < FDM->Tanks.Num(); ++Index)
        {
            const auto& T = FDM->Tanks[Index]; const FString Prefix = FString::Printf(TEXT("Tank %d"), Index);
            Add(D, Prefix + TEXT("Mass"), Prefix + TEXT(" fuel"), TEXT("kg"), T.ContentGallons * T.FuelDensityPoundsPerGallon * LbToKg, S.bReady);
            Add(D, Prefix + TEXT("Fill"), Prefix + TEXT(" fill"), TEXT("%"), T.FillPercentage, S.bReady);
        }
        break;
    case EFighterFlightDebugPage::Gear:
        for (const auto& G : FDM->Gears)
        {
            const FString N = G.Name;
            Text(D, N + TEXT("State"), N, FString::Printf(TEXT("%s | %s | WOW %s"), G.IsBogey ? TEXT("WHEEL") : TEXT("STRUCTURE"), G.IsDown ? TEXT("DOWN") : G.IsUp ? TEXT("UP") : TEXT("TRANSIT"), G.HasWeightOnWheel ? TEXT("YES") : TEXT("NO")), S.bReady);
            if (!G.IsBogey)
            {
                Add(D, N + TEXT("Contact"), TEXT("  Contact force magnitude"), TEXT("kN"), G.Force.Length() / 1000., S.bReady);
                continue;
            }
            Add(D, N + TEXT("Extension"), TEXT("  Extension"), TEXT("%"), G.NormalizedPosition * 100., S.bReady);
            Add(D, N + TEXT("Compression"), TEXT("  Compression"), TEXT("cm"), G.CompressionMeters * 100., S.bReady);
            Add(D, N + TEXT("CompressionRate"), TEXT("  Compression rate"), TEXT("cm/s"), G.CompressionVelocityMetersPerSec * 100., S.bReady);
            Add(D, N + TEXT("Strut"), TEXT("  Signed strut force"), TEXT("kN"), G.StrutForceNewtons / 1000., S.bReady);
            Add(D, N + TEXT("Contact"), TEXT("  Contact force magnitude"), TEXT("kN"), G.Force.Length() / 1000., S.bReady);
            Add(D, N + TEXT("Spin"), TEXT("  Wheel speed"), TEXT("m/s"), G.WheelRollLinearVelocityMetersPerSec, S.bReady);
            Add(D, N + TEXT("Steer"), TEXT("  Steering"), TEXT("deg"), G.SteeringAngleDeg, S.bReady);
        }
        break;
    case EFighterFlightDebugPage::Aero:
        Property(D, FDM, TEXT("Qbar"), TEXT("Dynamic pressure"), TEXT("aero/qbar-psf"), TEXT("kPa"), .04788025898);
        Property(D, FDM, TEXT("Weight"), TEXT("Current total mass"), TEXT("inertia/weight-lbs"), TEXT("kg"), LbToKg);
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const TCHAR Letter = TEXT('x') + Axis;
            Add(D, FString::Printf(TEXT("CG%c"), Letter), FString::Printf(TEXT("CG %c (Actor/Mesh)"), Letter), TEXT("cm"), FDM->CGLocalPosition[Axis], S.bReady);
            Property(D, FDM, FString::Printf(TEXT("Fa%c"), Letter), FString::Printf(TEXT("Aero F%c (body)"), Letter), FString::Printf(TEXT("forces/fb%c-aero-lbs"), Letter), TEXT("kN"), LbfToN / 1000.);
        }
        for (const TCHAR* Axis : {TEXT("l"), TEXT("m"), TEXT("n")})
            Property(D, FDM, FString(TEXT("Moment")) + Axis, FString::Printf(TEXT("Aero moment %s (body)"), Axis), FString::Printf(TEXT("moments/%s-aero-lbsft"), Axis), TEXT("kNm"), LbfToN * FEET_TO_METER / 1000.);
        for (const TCHAR* Axis : {TEXT("north"), TEXT("east"), TEXT("down")})
            Property(D, FDM, FString(TEXT("Wind")) + Axis, FString::Printf(TEXT("Wind %s (NED)"), Axis), FString::Printf(TEXT("atmosphere/total-wind-%s-fps"), Axis), TEXT("m/s"), FEET_TO_METER);
        break;
    case EFighterFlightDebugPage::Simulation:
    {
        Text(D, TEXT("Pawn"), TEXT("Pawn"), S.PawnName);
        Add(D, TEXT("Generation"), TEXT("Model generation"), TEXT(""), S.ModelGeneration, true, 0);
        Add(D, TEXT("EngineFPS"), TEXT("Engine FPS (wall avg)"), TEXT("fps"), 0., false);
        const double Delta = Fighter->GetWorld() ? Fighter->GetWorld()->GetDeltaSeconds() : 0.;
        Add(D, TEXT("GameFPS"), TEXT("Game delta rate"), TEXT("fps"), Delta > 0. ? 1. / Delta : 0., Delta > 0.);
        Add(D, TEXT("GameFrame"), TEXT("Game frame delta"), TEXT("ms"), Delta * 1000., Delta > 0., 3);
        Add(D, TEXT("SolverHz"), TEXT("Fixed solver rate"), TEXT("Hz"), 120., true, 0);
        Add(D, TEXT("Steps"), TEXT("Last frame steps"), TEXT(""), FDM->SimulationStepsLastFrame, true, 0);
        Add(D, TEXT("Alpha"), TEXT("Display interpolation"), TEXT(""), FDM->SimulationInterpolationAlpha, true, 3);
        Add(D, TEXT("Dropped"), TEXT("Dropped simulation time"), TEXT("s"), FDM->DroppedSimulationTimeSeconds, true, 4);
        Add(D, TEXT("Time"), TEXT("Simulation time"), TEXT("s"), S.SimulationSeconds, S.bHasSimulationTime, 3);
        Text(D, TEXT("Legacy"), TEXT("Legacy green output"), FDM->DrawDebug ? TEXT("ON (unchanged)") : TEXT("OFF (unchanged)"));
        break;
    }
    default: break;
    }
    return S;
}

void FFighterFlightDebugHistory::Reset() { Samples.Reset(); Next = Count = 0; }
void FFighterFlightDebugHistory::Add(const FSample& Sample)
{
    if (!FMath::IsFinite(Sample.Time)) return;
    if (Count && Sample.Time <= Samples[(Next + Capacity - 1) % Capacity].Time)
    {
        if (Sample.Time == Samples[(Next + Capacity - 1) % Capacity].Time) return;
        Reset();
    }
    if (Samples.IsEmpty()) Samples.SetNum(Capacity);
    FSample Clean = Sample;
    if (!FMath::IsFinite(Clean.Alpha)) { Clean.Valid &= ~1; Clean.Alpha = 0.; }
    if (!FMath::IsFinite(Clean.Nz)) { Clean.Valid &= ~2; Clean.Nz = 0.; }
    if (!FMath::IsFinite(Clean.Q)) { Clean.Valid &= ~4; Clean.Q = 0.; }
    Samples[Next] = Clean; Next = (Next + 1) % Capacity; Count = FMath::Min(Count + 1, Capacity);
}
TArray<FFighterFlightDebugHistory::FSample> FFighterFlightDebugHistory::Window(double Now) const
{
    TArray<FSample> Result;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const auto& Sample = Samples[(Next - Count + Capacity + Index) % Capacity];
        if (Sample.Time >= Now - 10. && Sample.Time <= Now) Result.Add(Sample);
    }
    return Result;
}
