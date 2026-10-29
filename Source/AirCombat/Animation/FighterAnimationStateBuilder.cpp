#include "Animation/FighterAnimationStateBuilder.h"
#include "JSBSimMovementComponent.h"

namespace
{
float Finite(double Value, float Fallback = 0.f)
{
    return FMath::IsFinite(Value) && FMath::Abs(Value) <= MAX_flt ? static_cast<float>(Value) : Fallback;
}
}

void UFighterAnimationBindings::CollectProblems(TArray<FString>& Problems) const
{
    if (AircraftModel.IsEmpty()) Problems.Add(TEXT("AircraftModel is empty"));
    TSet<FName> SurfaceIds, GearIds;
    TSet<FString> Contacts;
    for (const auto& Binding : Surfaces)
    {
        if (Binding.Id.IsNone() || SurfaceIds.Contains(Binding.Id)) Problems.Add(TEXT("Surface IDs must be nonempty and unique"));
        SurfaceIds.Add(Binding.Id);
        if (Binding.Property.IsEmpty() && !Binding.bOptional) Problems.Add(TEXT("Surface property is empty"));
        if (!FMath::IsFinite(Binding.DegreesPerUnit) || !FMath::IsFinite(Binding.OffsetDeg)
            || !FMath::IsFinite(Binding.MinimumDeg) || !FMath::IsFinite(Binding.MaximumDeg)
            || Binding.MaximumDeg <= Binding.MinimumDeg) Problems.Add(TEXT("Surface conversion/range is invalid"));
    }
    for (const auto& Binding : Gears)
    {
        if (Binding.Id.IsNone() || GearIds.Contains(Binding.Id)) Problems.Add(TEXT("Gear IDs must be nonempty and unique"));
        GearIds.Add(Binding.Id);
        if (Binding.ContactName.IsEmpty() || Contacts.Contains(Binding.ContactName)) Problems.Add(TEXT("Gear contact names must be nonempty and unique"));
        Contacts.Add(Binding.ContactName);
        if (!FMath::IsFinite(Binding.MaximumCompressionCm) || Binding.MaximumCompressionCm < 0.f
            || !FMath::IsFinite(Binding.WheelRadiusCm) || Binding.WheelRadiusCm < 0.f) Problems.Add(TEXT("Gear travel/radius must be finite and nonnegative"));
    }
}

FFighterSurfaceAnimationState FFighterAnimationStateBuilder::Surface(const FFighterSurfaceAnimationBinding& Binding, bool bFound, double Value)
{
    FFighterSurfaceAnimationState Result;
    Result.Id = Binding.Id;
    const double Angle = Value * Binding.DegreesPerUnit + Binding.OffsetDeg;
    if (!bFound || !FMath::IsFinite(Angle) || FMath::Abs(Angle) > MAX_flt) return Result;
    Result.bAvailable = true;
    if (Binding.bNormalizedPositionOnly)
    {
        Result.Position01 = FMath::Clamp(static_cast<float>(Angle), 0.f, 1.f);
        return Result;
    }
    Result.bHasAngle = true;
    Result.AngleDeg = static_cast<float>(Angle);
    const float Range = Binding.MaximumDeg - Binding.MinimumDeg;
    if (FMath::IsFinite(Range) && Range > UE_SMALL_NUMBER)
        Result.Position01 = FMath::Clamp((Result.AngleDeg - Binding.MinimumDeg) / Range, 0.f, 1.f);
    return Result;
}

FFighterGearAnimationState FFighterAnimationStateBuilder::Gear(const FFighterGearAnimationBinding& Binding,
    const FGear* Source, float DeltaSeconds, float PreviousRotationDeg)
{
    FFighterGearAnimationState Result;
    Result.Id = Binding.Id;
    if (!Source || !Source->IsBogey) return Result;
    Result.bAvailable = true;
    Result.Extension01 = FMath::Clamp(Finite(Source->NormalizedPosition), 0.f, 1.f);
    Result.bWeightOnWheel = Source->HasWeightOnWheel;
    Result.SteeringAngleDeg = Finite(Source->SteeringAngleDeg);
    Result.WheelSpeedCmPerSec = Finite(Source->WheelRollLinearVelocityMetersPerSec * 100.0);
    Result.CompressionCm = FMath::Max(0.f, Finite(Source->CompressionMeters * 100.0));
    Result.CompressionSpeedCmPerSec = Finite(Source->CompressionVelocityMetersPerSec * 100.0);
    Result.StrutForceNewtons = Finite(Source->StrutForceNewtons);
    Result.bHasSuspensionRange = FMath::IsFinite(Binding.MaximumCompressionCm) && Binding.MaximumCompressionCm > UE_SMALL_NUMBER;
    if (Result.bHasSuspensionRange) Result.SuspensionAlpha = FMath::Clamp(Result.CompressionCm / Binding.MaximumCompressionCm, 0.f, 1.f);
    Result.bHasWheelRadius = FMath::IsFinite(Binding.WheelRadiusCm) && Binding.WheelRadiusCm > UE_SMALL_NUMBER;
    if (Result.bHasWheelRadius)
    {
        const double Dt = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.0;
        const double Rotation = Finite(PreviousRotationDeg) + FMath::RadiansToDegrees(Result.WheelSpeedCmPerSec / Binding.WheelRadiusCm) * Dt;
        Result.WheelRotationDeg = Finite(FMath::Fmod(Rotation, 360.0));
    }
    return Result;
}

FFighterAnimationState FFighterAnimationStateBuilder::Build(UJSBSimMovementComponent& FDM,
    const UFighterAnimationBindings* Bindings, float DeltaSeconds, const FFighterAnimationState& Previous)
{
    check(IsInGameThread());
    
    FFighterAnimationState Result;
    if (!FDM.IsSimulationReady()) return Result;
    Result.bValid = true;
    Result.Model = FName(*FDM.AircraftModel);
    Result.AirspeedKts = Finite(FDM.AircraftState.CalibratedAirSpeedKts);
    Result.GroundSpeedKts = Finite(FDM.AircraftState.GroundSpeedKts);
    Result.bBindingsMatchModel = Bindings && Bindings->AircraftModel.Equals(FDM.AircraftModel, ESearchCase::IgnoreCase);
    
    if (Result.bBindingsMatchModel)
    {
        for (const auto& Binding : Bindings->Surfaces)
        {
            double Value = 0.;
            const bool bFound = FDM.ReadPropertyValue(Binding.Property, Value);
            Result.Surfaces.Add(Surface(Binding, bFound, Value));
        }
        for (const auto& Binding : Bindings->Gears)
        {
            const FGear* Source = FDM.Gears.FindByPredicate([&](const FGear& Item) { return Item.Name == Binding.ContactName && Item.IsBogey; });
            const auto* Old = Previous.Gears.FindByPredicate([&](const auto& Item) { return Item.Id == Binding.Id; });
            Result.Gears.Add(Gear(Binding, Source, DeltaSeconds, Old ? Old->WheelRotationDeg : 0.f));
        }
    }
    
    for (int32 Index = 0; Index < FDM.EngineStates.Num(); ++Index)
    {
        const FEngineState& Source = FDM.EngineStates[Index];
        FFighterEngineAnimationState Engine;
        Engine.EngineIndex = Index;
        Engine.bAvailable = true;
        Engine.bRunning = Source.Running;
        Engine.bAfterburner = Source.Augmentation;
        Engine.ThrottleCommand01 = FDM.EngineCommands.IsValidIndex(Index) ? FMath::Clamp(Finite(FDM.EngineCommands[Index].Throttle), 0.f, 1.f) : 0.f;
        Engine.N1Percent = Finite(Source.N1);
        Engine.N2Percent = Finite(Source.N2);
        Engine.ThrustNewtons = Finite(Source.Thrust * 4.4482216152605);
        Engine.bNozzleAvailable = Source.HasNozzlePosition;
        Engine.NozzlePosition01 = Engine.bNozzleAvailable ? FMath::Clamp(Finite(Source.NozzlePosition), 0.f, 1.f) : 0.f;
        Result.Engines.Add(Engine);
    }
    
    return Result;
}
