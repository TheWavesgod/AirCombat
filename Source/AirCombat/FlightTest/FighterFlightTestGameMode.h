#pragma once

#include "CoreMinimal.h"
#include "GameMode/AirCombatGameMode.h"
#include "FighterFlightTestGameMode.generated.h"

/** Optional test-level spawn policy. Production flight and input behavior stays in the base classes. */
UCLASS()
class AIRCOMBAT_API AFighterFlightTestGameMode : public AAirCombatGameMode
{
    GENERATED_BODY()
public:
    /** PlayerStartTag selected by default; ?FlightTestStart=Air overrides it for one session. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Flight Test")
    FName DefaultStartTag = TEXT("Ground");

    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
};
