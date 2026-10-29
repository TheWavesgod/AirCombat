#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Pawns/Fighter/FighterCommandProcessor.h"
#include "JSBSimMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterSpeedBrakeTest, "AirCombat.FlightControls.SpeedBrakeHoldRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterSpeedBrakeTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FFighterCommandProcessor Processor;
    FFighterControlCommand Input;
    Input.SpeedBrake = 1.f;
    Processor.Update(0.5f, Input, FDM);
    TestEqual(TEXT("Held brake extends at configured rate"), FDM->Commands.SpeedBrake, 0.25);
    Processor.Update(10.f, Input, FDM);
    TestEqual(TEXT("Extension cannot overshoot"), FDM->Commands.SpeedBrake, 1.0);
    Input.SpeedBrake = 0.f;
    Processor.Update(0.5f, Input, FDM);
    TestEqual(TEXT("Release actively retracts"), FDM->Commands.SpeedBrake, 0.75);
    Processor.Update(10.f, Input, FDM);
    TestEqual(TEXT("Fully retracted after release"), FDM->Commands.SpeedBrake, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterTrimHandoffTest, "AirCombat.FlightControls.TrimHandoffAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterTrimHandoffTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FDM->Commands.PitchTrim = 0.25;
    FDM->Commands.YawTrim = -0.125;
    FDM->Commands.RollTrim = 0.5;
    FDM->Commands.GearDown = 0;
    FDM->EngineCommands.SetNum(1);
    FDM->EngineCommands[0].Throttle = 0.375;
    FFighterCommandProcessor Processor;
    FFighterControlCommand Input;
    Processor.Update(1.f / 60.f, Input, FDM);
    TestEqual(TEXT("Initial pitch trim survives neutral input"), FDM->Commands.PitchTrim, 0.25);
    TestEqual(TEXT("Initial yaw trim survives neutral input"), FDM->Commands.YawTrim, -0.125);
    TestEqual(TEXT("Initial roll trim survives neutral input"), FDM->Commands.RollTrim, 0.5);
    TestEqual(TEXT("Trimmed throttle survives neutral input"), (double)FDM->EngineCommands[0].Throttle, 0.375);
    TestFalse(TEXT("Air start retains retracted gear"), Processor.IsGearDown());
    Processor.RequestResetTrim();
    Processor.Update(0.1f, Input, FDM);
    TestEqual(TEXT("Pitch trim resets"), FDM->Commands.PitchTrim, 0.0);
    TestEqual(TEXT("Yaw trim resets"), FDM->Commands.YawTrim, 0.0);
    TestEqual(TEXT("Roll trim resets"), FDM->Commands.RollTrim, 0.0);
    Input.TrimPitch = 1.f;
    Processor.Update(0.5f, Input, FDM);
    TestEqual(TEXT("Reset request is consumed once"), FDM->Commands.PitchTrim, 0.125);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterModelYawTest, "AirCombat.FlightControls.ModelOwnsYawFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterModelYawTest::RunTest(const FString& Parameters)
{
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    FDM->AircraftState.EulerRates = FVector(20., -10., 30.);
    FDM->AircraftState.VelocityNEDfps = FVector(500., 300., -100.);
    FFighterCommandProcessor Processor;
    FFighterControlCommand Input;
    Processor.Update(0.1f, Input, FDM);
    TestEqual(TEXT("Attitude and world velocity do not invent pilot rudder commands"), FDM->Commands.Rudder, 0.0);
    Input.Yaw = 0.5f;
    Processor.Update(0.1f, Input, FDM);
    TestEqual(TEXT("Manual right rudder is passed through"), FDM->Commands.Rudder, 0.5);
    Input.Yaw = -0.75f;
    Processor.Update(0.1f, Input, FDM);
    TestEqual(TEXT("Manual left rudder is passed through"), FDM->Commands.Rudder, -0.75);
    return true;
}
#endif
