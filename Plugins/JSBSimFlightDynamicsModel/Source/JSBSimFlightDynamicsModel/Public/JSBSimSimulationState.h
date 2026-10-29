#pragma once

#include "CoreMinimal.h"

/** Fixed solver time is independent of render cadence. Excess hitch time is explicitly accounted for. */
class FJSBSimFixedStepClock
{
public:
    static constexpr double StepSeconds = 1.0 / 120.0;
    static constexpr int32 MaxStepsPerFrame = 32;

    struct FAdvance
    {
        int32 Steps = 0;
        double AcceptedSeconds = 0.;
        double DroppedSeconds = 0.;
    };

    FAdvance Advance(double FrameSeconds)
    {
        FAdvance Result;
        if (!FMath::IsFinite(FrameSeconds) || FrameSeconds <= 0.) return Result;
        Result.AcceptedSeconds = FMath::Min(FrameSeconds, StepSeconds * MaxStepsPerFrame);
        Result.DroppedSeconds = FrameSeconds - Result.AcceptedSeconds;
        RemainderSeconds += Result.AcceptedSeconds;
        Result.Steps = FMath::Min(FMath::FloorToInt((RemainderSeconds + 1.e-12) / StepSeconds), MaxStepsPerFrame);
        RemainderSeconds = FMath::Max(0., RemainderSeconds - Result.Steps * StepSeconds);
        if (RemainderSeconds >= StepSeconds)
        {
            const double WholeDebt = FMath::FloorToDouble(RemainderSeconds / StepSeconds) * StepSeconds;
            Result.DroppedSeconds += WholeDebt;
            RemainderSeconds -= WholeDebt;
        }
        return Result;
    }

    double GetAlpha() const { return FMath::Clamp(RemainderSeconds / StepSeconds, 0., 1.); }
    double GetRemainderSeconds() const { return RemainderSeconds; }
    void Reset() { RemainderSeconds = 0.; }

private:
    double RemainderSeconds = 0.;
};

/** ECEF positions survive UE world-origin shifts; rotation is already in the current engine reference frame. */
struct FJSBSimPoseSample
{
    FVector ECEFLocationMeters = FVector::ZeroVector;
    FQuat EngineRotation = FQuat::Identity;
    FVector CGLocalCentimeters = FVector::ZeroVector;
};

/** The final two solver substeps, never the previous two render frames. */
class FJSBSimPoseHistory
{
public:
    bool IsValid() const { return bValid; }
    void Clear() { bValid = false; Previous = Current = FJSBSimPoseSample(); }
    void Reset(const FJSBSimPoseSample& Sample) { Previous = Current = Sample; bValid = true; }
    void Push(const FJSBSimPoseSample& Sample)
    {
        if (!bValid) { Reset(Sample); return; }
        Previous = Current;
        Current = Sample;
    }
    const FJSBSimPoseSample& GetCurrent() const { return Current; }
    FJSBSimPoseSample Interpolate(double Alpha) const
    {
        const double T = FMath::IsFinite(Alpha) ? FMath::Clamp(Alpha, 0., 1.) : 0.;
        FJSBSimPoseSample Result;
        Result.ECEFLocationMeters = FMath::Lerp(Previous.ECEFLocationMeters, Current.ECEFLocationMeters, T);
        Result.EngineRotation = FQuat::Slerp(Previous.EngineRotation, Current.EngineRotation, T).GetNormalized();
        Result.CGLocalCentimeters = FMath::Lerp(Previous.CGLocalCentimeters, Current.CGLocalCentimeters, T);
        return Result;
    }

private:
    bool bValid = false;
    FJSBSimPoseSample Previous;
    FJSBSimPoseSample Current;
};
