#include "FlightTest/FighterFlightTestGameMode.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "FlightTest/FighterFlightTestPlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Pawns/Fighter/Fighter.h"

AActor* AFighterFlightTestGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const FString Option = UGameplayStatics::ParseOption(OptionsString, TEXT("FlightTestStart"));
    const FName RequestedTag = Option.IsEmpty() ? DefaultStartTag : FName(*Option);
    AFighterFlightTestPlayerStart* Fallback = nullptr;
    for (TActorIterator<AFighterFlightTestPlayerStart> It(GetWorld()); It; ++It)
    {
        if (It->PlayerStartTag == RequestedTag) return *It;
        if (It->PlayerStartTag == DefaultStartTag) Fallback = *It;
    }
    if (Fallback)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unknown FlightTestStart '%s'; using '%s'."),
            *RequestedTag.ToString(), *DefaultStartTag.ToString());
        return Fallback;
    }
    return Super::ChoosePlayerStart_Implementation(Player);
}

APawn* AFighterFlightTestGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
    const auto* Start = Cast<AFighterFlightTestPlayerStart>(StartSpot);
    UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
    if (!Start || !PawnClass || !PawnClass->IsChildOf(AFighter::StaticClass()))
        return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);

    const FTransform Transform(FRotator(0.f, Start->GetActorRotation().Yaw, 0.f), Start->GetActorLocation());
    FActorSpawnParameters Params;
    Params.Instigator = GetInstigator();
    Params.ObjectFlags |= RF_Transient;
    Params.bDeferConstruction = true;
    AFighter* Fighter = GetWorld()->SpawnActor<AFighter>(PawnClass, Transform, Params);
    if (Fighter)
    {
        // Configure before PostInitializeComponents and the FDM component's BeginPlay/trim.
        Fighter->ConfigureStartState(Start->StartMode, Start->AirStartSpeedKts);
        Fighter->GetFlightDynamics()->bStartWithGearDown = Start->StartMode == EFighterStartMode::Ground;
        Fighter->FinishSpawning(Transform);
    }
    return Fighter;
}
