#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "JSBSimMovementComponent.h"
#include "UEGroundCallback.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJSBSimGroundMissTest, "AirCombat.FlightDynamics.GroundMissReturnsEarthContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FJSBSimGroundMissTest::RunTest(const FString& Parameters)
{
    // IC construction queries provisional positions outside the loaded UE terrain.
    // A component without a world reproduces a miss without needing a level fixture.
    auto* FDM = NewObject<UJSBSimMovementComponent>();
    UEGroundCallback Callback(FDM);
    for (double Latitude : {0.0, 0.7853981633974483})
    {
        JSBSim::FGLocation Location, Contact;
        Location.SetEllipse(20925646.32546, 20855486.5951);
        Location.SetPositionGeodetic(0.1, Latitude, 1000.0);
        JSBSim::FGColumnVector3 Normal, Velocity, AngularVelocity;
        const double AGL = Callback.GetAGLevel(0., Location, Contact, Normal, Velocity, AngularVelocity);
        TestTrue(TEXT("Miss preserves finite height above the ellipsoid"), FMath::IsNearlyEqual(AGL, 1000.0, 1.e-5));
        TestTrue(TEXT("Contact stays on Earth instead of the ECEF origin"), Contact.GetRadius() > 2.e7);
        TestTrue(TEXT("Fallback contact has sea-level height"), FMath::IsNearlyZero(Contact.GetGeodAltitude(), 1.e-5));
        TestTrue(TEXT("Normal is normalized"), FMath::IsNearlyEqual(Normal.Magnitude(), 1., 1.e-9));
        TestEqual(TEXT("Stationary ground velocity"), Velocity.Magnitude(), 0.0);
        TestEqual(TEXT("Stationary ground angular velocity"), AngularVelocity.Magnitude(), 0.0);
        JSBSim::FGColumnVector3 Dummy;
        TestTrue(TEXT("Aliased unused outputs supported by JSBSim IC queries"),
            FMath::IsNearlyEqual(Callback.GetAGLevel(0., Location, Contact, Dummy, Dummy, Dummy), 1000., 1.e-5));
    }
    return true;
}
#endif
