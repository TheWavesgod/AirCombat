#pragma once
#include "CoreMinimal.h"
#include "FighterFlightDebugSnapshot.generated.h"

class AFighter;

UENUM(BlueprintType)
enum class EFighterFlightDebugPage : uint8 { Overview, Response, Power, Gear, Aero, Simulation };

/** Display values own their storage; missing/invalid measurements are never represented by a fake zero. */
USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterFlightDebugMetric
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName Id;
    UPROPERTY(BlueprintReadOnly) FString Label;
    UPROPERTY(BlueprintReadOnly) FString Unit;
    UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
    UPROPERTY(BlueprintReadOnly) double Value = 0.;
    UPROPERTY(BlueprintReadOnly) FString Text;
    UPROPERTY(BlueprintReadOnly) bool bIsText = false;
    UPROPERTY(BlueprintReadOnly) int32 Decimals = 1;
    FString Format() const;
};

USTRUCT(BlueprintType)
struct AIRCOMBAT_API FFighterFlightDebugSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bHasAircraft = false;
    UPROPERTY(BlueprintReadOnly) bool bReady = false;
    UPROPERTY(BlueprintReadOnly) bool bCrashed = false;
    UPROPERTY(BlueprintReadOnly) FString Model;
    UPROPERTY(BlueprintReadOnly) FString PawnName;
    UPROPERTY(BlueprintReadOnly) FString Status = TEXT("NO AIRCRAFT");
    UPROPERTY(BlueprintReadOnly) int32 ModelGeneration = 0;
    UPROPERTY(BlueprintReadOnly) double SimulationSeconds = 0.;
    UPROPERTY(BlueprintReadOnly) bool bHasSimulationTime = false;
    UPROPERTY(BlueprintReadOnly) TArray<FFighterFlightDebugMetric> Overview;
    UPROPERTY(BlueprintReadOnly) TArray<FFighterFlightDebugMetric> Details;
};

class AIRCOMBAT_API FFighterFlightDebugSnapshotBuilder
{
public:
    static FFighterFlightDebugSnapshot Build(AFighter* Fighter, EFighterFlightDebugPage Page);
    static const TCHAR* PageName(EFighterFlightDebugPage Page);
};

/** 120 Hz samples while the monitor is visible. Bounded storage, no simulation pointers. */
class AIRCOMBAT_API FFighterFlightDebugHistory
{
public:
    struct FSample
    {
        double Time = 0.;
        double Alpha = 0., Nz = 0., Q = 0.;
        uint8 Valid = 0;
    };
    void Reset();
    void Add(const FSample& Sample);
    TArray<FSample> Window(double Now) const;
    int32 Num() const { return Count; }
private:
    static constexpr int32 Capacity = 1201;
    TArray<FSample> Samples;
    int32 Next = 0, Count = 0;
};
