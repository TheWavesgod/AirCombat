#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Pawns/Fighter/FighterTypes.h"
#include "FighterFlightTestPlayerStart.generated.h"

/** Initial conditions belonging to a test location, independent of the aircraft asset. */
UCLASS()
class AIRCOMBAT_API AFighterFlightTestPlayerStart : public APlayerStart
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight Test")
    EFighterStartMode StartMode = EFighterStartMode::Ground;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight Test", meta=(ClampMin="1", EditCondition="StartMode==EFighterStartMode::Air"))
    float AirStartSpeedKts = 400.f;
};
