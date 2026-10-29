// Fill out your copyright notice in the Description page of Project Settings.


#include "JSBSimMovementComponent.h"
#include "JSBSimModule.h"

// UE treats warning as errors. JSBSim has some warnings in its include files, so if we don't catch them inside this push/pop pragma, we won't be able to build...
// FGOutputType.h(151): warning C4263: 'bool JSBSim::FGOutputType::Run(void)': member function does not override any base class virtual member function
// FGOutputType.h(215): warning C4264: 'bool JSBSim::FGModel::Run(bool)': no override available for virtual member function from base 'JSBSim::FGModel'; function is hidden --- And others
// compiler.h(58): warning C4005: 'DEPRECATED': macro redefinition with UE_5.0\Engine\Source\Runtime\Core\Public\Windows\WindowsPlatformCompilerPreSetup.h(55): note: see previous definition of 'DEPRECATED'
// FGXMLElement.h(369): error C4458: declaration of 'name' hides class member
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4263 )
#pragma warning( disable : 4264 )
#pragma warning( disable : 4005 )
#pragma warning( disable : 4458 )
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#pragma clang diagnostic ignored "-Wshadow"
#endif

#include "FGFDMExec.h"
#include "math/FGLocation.h"
#include "models/FGFCS.h"
#include "models/FGPropulsion.h"
#include "models/propulsion/FGEngine.h"
#include "models/atmosphere/FGWinds.h"
#include "models/FGAerodynamics.h"
#include "models/FGAircraft.h"
#include "models/FGAtmosphere.h"
#include "models/FGAuxiliary.h"
#include "models/FGLGear.h"
#include "models/FGMassBalance.h"
#include "models/propulsion/FGThruster.h"
#include "models/propulsion/FGPiston.h"
#include "models/propulsion/FGTurbine.h"
#include "models/propulsion/FGTurboProp.h"
#include "models/propulsion/FGTank.h"
#include "initialization/FGInitialCondition.h"
#include "initialization/FGTrim.h"
#include "Interfaces/IPluginManager.h"
#include "simgear/props/props.hxx"

#ifdef _MSC_VER
#pragma warning( pop )
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "UEGroundCallback.h"

#include "DrawDebugHelpers.h"
#include "GeoReferencingSystem.h"
#include "Components/ActorComponent.h"


namespace
{
constexpr double PoundsForceToNewtons = 4.4482216152605;
constexpr double DebugForceCentimetersPerNewton = .001;

double FiniteTelemetry(double Value)
{
  return FMath::IsFinite(Value) ? Value : 0.;
}

bool IsPropertyPathValid(const FString& Path)
{
  if (Path.IsEmpty()) return false;
  for (TCHAR C : Path)
    if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')
      || C == '/' || C == '[' || C == ']' || C == '-' || C == '_' || C == '.')) return false;
  return true;
}

void ReadGearTelemetry(FGear& Out, JSBSim::FGLGear& Gear, const FTransform& BodyToActor, bool RuntimeReady)
{
  const bool Front = Out.IsFrontBogey, Rear = Out.IsRearBogey;
  Out = FGear{};
  Out.IsFrontBogey = Front;
  Out.IsRearBogey = Rear;
  Out.Name = UTF8_TO_TCHAR(Gear.GetName().c_str());
  Out.IsBogey = Gear.IsBogey();
  Out.NormalizedPosition = Gear.GetRetractable() ? FMath::Clamp(FiniteTelemetry(Gear.GetGearUnitPos()), 0., 1.) : 1.;
  Out.IsUp = Gear.GetGearUnitUp();
  Out.IsDown = Gear.GetGearUnitDown();
  const auto Location = Gear.GetBodyLocation() * FEET_TO_CENTIMETER;
  Out.RelativeLocation = BodyToActor.TransformPosition(FVector(Location(1), Location(2), Location(3)));
  if (Out.RelativeLocation.ContainsNaN()) Out.RelativeLocation = FVector::ZeroVector;
  if (!RuntimeReady) return;

  // Wheel spin can legitimately persist after liftoff. Steering is meaningful only with gear down.
  if (Out.IsBogey)
  {
    Out.WheelRollLinearVelocityMetersPerSec = FiniteTelemetry(Gear.GetWheelRollVel() * FEET_TO_METER);
    if (Out.IsDown) Out.SteeringAngleDeg = FiniteTelemetry(Gear.GetSteerAngleDeg());
  }
  // JSBSim can retain WOW/compression/StrutForce when retraction skips its contact branch.
  // In particular StrutForce may never have been initialized on an air start: do not read it.
  Out.HasWeightOnWheel = Out.IsDown && Gear.GetWOW();
  if (!Out.HasWeightOnWheel) return;
  if (Out.IsBogey)
  {
    Out.CompressionMeters = FMath::Max(0., FiniteTelemetry(Gear.GetCompLen() * FEET_TO_METER));
    Out.CompressionVelocityMetersPerSec = FiniteTelemetry(Gear.GetCompVel() * FEET_TO_METER);
    Out.StrutForceNewtons = FiniteTelemetry(Gear.GetCompForce() * PoundsForceToNewtons);
  }
  // These axis getters refresh only the already-solved friction/force cache. The virtual
  // FGLGear::GetBodyForces() must never be called by telemetry: it re-runs contact and wheel dynamics.
  const FVector BodyForce(Gear.GetBodyXForce(), Gear.GetBodyYForce(), Gear.GetBodyZForce());
  // The body/actor axis conversion contains a reflection encoded in FTransform's scale.
  Out.Force = BodyToActor.TransformVector(BodyForce * PoundsForceToNewtons);
  if (Out.Force.ContainsNaN()) Out.Force = FVector::ZeroVector;
}
}

////// Constructor
UJSBSimMovementComponent::UJSBSimMovementComponent()
{
  // Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features off to improve performance if you don't need them.
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.TickGroup = TG_PrePhysics;
}


bool UJSBSimMovementComponent::ReadPropertyValue(const FString& Property, double& OutValue)
{
  check(IsInGameThread());
  OutValue = 0.;
  if (!IsSimulationReady() || !PropertyManager || Property.IsEmpty()) return false;
  SGPropertyNode** Cached = ReadPropertyCache.Find(Property);
  if (!Cached && !IsPropertyPathValid(Property))
  {
    ReadPropertyCache.Add(Property, nullptr);
    return false;
  }
  SGPropertyNode* Node = Cached ? *Cached : PropertyManager->GetNode(TCHAR_TO_UTF8(*Property), false);
  if (!Cached) ReadPropertyCache.Add(Property, Node);
  if (!Node || !Node->getAttribute(SGPropertyNode::READ)) return false;
  const double Value = Node->getDoubleValue();
  if (!FMath::IsFinite(Value)) return false;
  OutValue = Value;
  return true;
}

////// Public functions
FString UJSBSimMovementComponent::GetAircraftScreenName() const
{
  FString ScreenName;
  if (AircraftLoaded && Aircraft)
  {
    ScreenName = FString(Aircraft->GetAircraftName().c_str());
  }

  return ScreenName;
}


void UJSBSimMovementComponent::PropertyManagerNode(TArray<FString>& Catalog)
{
  Catalog.Reset();
  if (!AircraftLoaded || !Exec) return;
  for (const auto& Property : Exec->GetPropertyCatalog()) Catalog.Add(UTF8_TO_TCHAR(Property.c_str()));
}

void UJSBSimMovementComponent::CommandConsole(FString Property, FString InValue, FString& OutValue)
{
  OutValue.Reset();
  if (!AircraftLoaded || !PropertyManager || !IsPropertyPathValid(Property)) return;
  if (SGPropertyNode* Node = PropertyManager->GetNode(TCHAR_TO_UTF8(*Property), false))
  {
    if (!InValue.IsEmpty()) Node->setStringValue(TCHAR_TO_UTF8(*InValue));
    OutValue = UTF8_TO_TCHAR(Node->getStringValue());
  }
}

void UJSBSimMovementComponent::CommandConsoleBatch(TArray<FString> Property, TArray<FString> InValue, TArray<FString>& OutValue)
{
  OutValue.Init(FString(), Property.Num());
  for (int32 Index = 0; Index < Property.Num(); ++Index)
    CommandConsole(Property[Index], InValue.IsValidIndex(Index) ? InValue[Index] : FString(), OutValue[Index]);
}

void UJSBSimMovementComponent::SetWind(FSimpleWindState WindState)
{
    auto ConvertUnrealWindModeToFGWindMode = [](ETurbType Unreal) {
        switch (Unreal)
        {
        case ETurbType::None:
            return JSBSim::FGWinds::ttNone;
            break;
        case ETurbType::Standard:
            return JSBSim::FGWinds::ttStandard;
            break;
        case ETurbType::Culp:
            return JSBSim::FGWinds::ttCulp;
            break;
        case ETurbType::Milspec:
            return JSBSim::FGWinds::ttMilspec;
            break;
        case ETurbType::Tustin:
            return JSBSim::FGWinds::ttTustin;
            break;
        default:
            break;
        }
        return JSBSim::FGWinds::ttNone;
        };
    if (Winds)
    {
        Winds->SetTurbType(ConvertUnrealWindModeToFGWindMode(WindState.TurbType));
        Winds->SetTurbGain(WindState.TurbGain);
        Winds->SetTurbRate(WindState.TurbRate);
        Winds->SetWindNED(*(FGColumnVector3*)&WindState.WindNED);
        Winds->SetProbabilityOfExceedence(WindState.ProbabilityOfExceedence);
    }
}



void UJSBSimMovementComponent::LoadAircraft(bool ResetToDefaultSettings)
{
  UE_LOG(LogJSBSim, Display, TEXT("UJSBSimMovementComponent::LoadAircraft %s"), *AircraftModel);

  TMap<FString, TPair<bool, bool>> GearLabels;
  const TArray<FTank> TankSettings = Tanks;
  if (!ResetToDefaultSettings)
    for (const FGear& Gear : Gears) GearLabels.Add(Gear.Name, {Gear.IsFrontBogey, Gear.IsRearBogey});
  DeInitializeJSBSim();
  InitializeJSBSim();
  AircraftLoaded = Exec->LoadModel(std::string(TCHAR_TO_UTF8(*AircraftModel)));

  if (!AircraftLoaded)
  {
    UE_LOG(LogJSBSim, Error, TEXT("Error while loading Model %s - Please check for typo or your configurations files"), *AircraftModel);
    return;
  }
  {
    UE_LOG(LogJSBSim, Display, TEXT("Model %s Loaded successfully !"), *AircraftModel);
  }

  // Do basic sanity checks
  if (GroundReactions->GetNumGearUnits() <= 0)
  {
    UE_LOG(LogJSBSim, Error, TEXT("Error - Num Gear Units = %d. This is a very bad thing because with 0 gear units, the ground trimming routine will core dump"), GroundReactions->GetNumGearUnits());
    AircraftLoaded = false;
    return;
  }

  UpdateLocalTransforms();

  InitTankDefaultProperties();
  if (!ResetToDefaultSettings)
    for (int32 Index = 0; Index < FMath::Min(TankSettings.Num(), Tanks.Num()); ++Index)
    {
      Tanks[Index].FuelDensityPoundsPerGallon = TankSettings[Index].FuelDensityPoundsPerGallon;
      Tanks[Index].ContentGallons = TankSettings[Index].ContentGallons;
    }
  CopyGearPropertiesFromJSBSim();
  for (FGear& Gear : Gears)
    if (const auto* Labels = GearLabels.Find(Gear.Name))
    {
      Gear.IsFrontBogey = Labels->Key;
      Gear.IsRearBogey = Labels->Value;
    }
  InitEnginesCommandAndStates();
  GetEnginesStates();
}

double UJSBSimMovementComponent::GetAGLevel(const FVector& StartECEFLocation, FVector& ECEFContactPoint, FVector& ECEFNormal, bool* GroundHit)
{
  if (GroundHit) *GroundHit = false;
  ECEFContactPoint = FVector::ZeroVector;
  ECEFNormal = FVector::ZAxisVector;
  if (!GeoReferencingSystem || !GetWorld())
  {
    return 0.0;
  }

  // Get local Up vector at the query ECEF Location
  FTransform TangentTransform = GeoReferencingSystem->GetTangentTransformAtECEFLocation(StartECEFLocation);
  FVector Up = TangentTransform.TransformVector(FVector::ZAxisVector);

  // Compute the raycast Origin point
  FVector StartEngineLocation;
  GeoReferencingSystem->ECEFToEngine(StartECEFLocation, StartEngineLocation);
  FVector LineCheckStart = StartEngineLocation + AGLThresholdMeters * 100 * Up; // slightly above the starting point

  // Compute the raycast end point
  // Estimate raycast length - Altitude + 5% of ellipsoid radius in case of negative altitudes
  FGeographicCoordinates QueryCoordinates;
  GeoReferencingSystem->ECEFToGeographic(StartECEFLocation, QueryCoordinates);
  const double TraceDepthMeters = FMath::Max(1.0, QueryCoordinates.Altitude
    + 0.05 * GeoReferencingSystem->GetGeographicEllipsoidMaxRadius());
  const FVector LineCheckEnd = StartEngineLocation - TraceDepthMeters * 100.0 * Up;

  // Prepare collision query
  FHitResult HitResult = FHitResult();
  static const FName LineTraceSingleName(TEXT("AGLevelLineTrace"));
  FCollisionQueryParams CollisionParams(LineTraceSingleName);
  CollisionParams.bTraceComplex = true;
  CollisionParams.AddIgnoredActor(Parent);

  FCollisionObjectQueryParams ObjectParams = FCollisionObjectQueryParams(ECC_WorldStatic);
  ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
  ObjectParams.AddObjectTypesToQuery(ECC_Pawn);


  // Do Query
  double HAT = 0.0;
  if (GetWorld()->LineTraceSingleByObjectType(HitResult, LineCheckStart, LineCheckEnd, ObjectParams, CollisionParams))
  {

    if (GroundHit) *GroundHit = true;
    FVector DirectionToImpact = HitResult.ImpactPoint - StartEngineLocation;
    HAT = FVector::Dist(StartEngineLocation, HitResult.ImpactPoint) / 100.0 * -FMath::Sign(DirectionToImpact.Dot(Up)); // JSBSim expect a signed distance. Consider that!
    GeoReferencingSystem->EngineToECEF(HitResult.ImpactPoint, ECEFContactPoint);

    // Georeferencing don't provide tools to transform a direction, or access the worldToECEF Matrix - Do it by hand
    FVector ECEFNormalEnd;
    GeoReferencingSystem->EngineToECEF(HitResult.ImpactPoint + HitResult.ImpactNormal * 100.0, ECEFNormalEnd);
    ECEFNormal = ECEFNormalEnd - ECEFContactPoint;
    ECEFNormal.Normalize();
  }
  else
  {
    ECEFContactPoint = FVector();
    ECEFNormal = FVector::ZAxisVector;
  }
  return HAT;
}

////// ActorComponent overridables
void UJSBSimMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  SimulationStepsLastFrame = 0;
  if (IsSimulationReady() && !AircraftState.Crashed)
  {
    const auto Frame = SimulationClock.Advance(DeltaTime);
    DroppedSimulationTimeSeconds += Frame.DroppedSeconds;
    SimulationInterpolationAlpha = SimulationClock.GetAlpha();
    if (Frame.DroppedSeconds > 0.)
      UE_LOG(LogJSBSim, Verbose, TEXT("%s fixed-step hitch budget discarded %.6f seconds (total %.6f)."),
        *GetNameSafe(GetOwner()), Frame.DroppedSeconds, DroppedSimulationTimeSeconds);

    Exec->Setdt(FJSBSimFixedStepClock::StepSeconds);
    OnBeforeSimulation.Broadcast(static_cast<float>(Frame.AcceptedSeconds));
    CopyToJSBSim();
    for (int32 Step = 0; Step < Frame.Steps; ++Step)
    {
      FJSBSimPoseSample Pose;
      if (!Exec->Run() || !CaptureSimulationPose(Pose))
      {
        PoseHistory.Clear();
        CrashedEvent();
        break;
      }
      PoseHistory.Push(Pose);
      ++SimulationStepsLastFrame;
      OnAfterSimulationStep.Broadcast();
    }

    UpdateLocalTransforms();
    CopyFromJSBSim();
    // Keep the last valid physical pose on a crash; invalid samples must not replace it or rebroadcast the event.
    if (Parent && PoseHistory.IsValid() && !ApplyCurrentActorTransform() && !AircraftState.Crashed) CrashedEvent();
    if (DrawDebug)
    {
      DrawDebugMessage();
      DrawDebugObjects();
    }
  }
}

void UJSBSimMovementComponent::ResetSimulationPresentation()
{
  SimulationClock.Reset();
  PoseHistory.Clear();
  SimulationStepsLastFrame = 0;
  SimulationInterpolationAlpha = 0.;
  DroppedSimulationTimeSeconds = 0.;
}

bool UJSBSimMovementComponent::CaptureSimulationPose(FJSBSimPoseSample& OutPose) const
{
  if (!Propagate || !MassBalance || !GeoReferencingSystem) return false;
  const auto& Location = Propagate->GetLocation();
  OutPose.ECEFLocationMeters = FVector(Location(1), Location(2), Location(3)) * FEET_TO_METER;
  FRotator Rotation(FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::eTht)),
    FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::ePsi)),
    FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::ePhi)));
  Rotation.Yaw -= 90.;
  if (GeoReferencingSystem->PlanetShape == EPlanetShape::FlatPlanet) Rotation.Yaw -= 180.;
  const FTransform Tangent = GeoReferencingSystem->GetTangentTransformAtECEFLocation(OutPose.ECEFLocationMeters);
  OutPose.EngineRotation = Tangent.TransformRotation(Rotation.Quaternion()).GetNormalized();
  // Keep the existing structural/body mapping and mass properties unchanged.
  const auto CG = MassBalance->StructuralToBody(JSBSim::FGColumnVector3()) * FEET_TO_CENTIMETER;
  OutPose.CGLocalCentimeters = StructuralToActor.TransformPosition(FVector(CG(1), CG(2), CG(3)));
  return !OutPose.ECEFLocationMeters.ContainsNaN() && !OutPose.EngineRotation.ContainsNaN()
    && !OutPose.CGLocalCentimeters.ContainsNaN();
}

bool UJSBSimMovementComponent::PoseToActorTransform(const FJSBSimPoseSample& Pose, FTransform& OutTransform) const
{
  if (!GeoReferencingSystem) return false;
  FVector CGWorld;
  GeoReferencingSystem->ECEFToEngine(Pose.ECEFLocationMeters, CGWorld);
  const FVector Location = CGWorld - Pose.EngineRotation.RotateVector(Pose.CGLocalCentimeters);
  if (Location.ContainsNaN() || Pose.EngineRotation.ContainsNaN()) return false;
  OutTransform = FTransform(Pose.EngineRotation, Location, Parent ? Parent->GetActorScale3D() : FVector::OneVector);
  return true;
}

bool UJSBSimMovementComponent::GetInterpolatedActorTransform(FTransform& OutTransform) const
{
  if (!IsSimulationReady() || AircraftState.Crashed || !PoseHistory.IsValid()) return false;
  return PoseToActorTransform(PoseHistory.Interpolate(SimulationClock.GetAlpha()), OutTransform);
}

bool UJSBSimMovementComponent::ApplyCurrentActorTransform()
{
  FTransform Pose;
  if (!Parent || !PoseHistory.IsValid() || !PoseToActorTransform(PoseHistory.GetCurrent(), Pose)) return false;
  Parent->SetActorLocationAndRotation(Pose.GetLocation(), Pose.GetRotation());
  FVector Horizontal = ECEFForwardHorizontal;
  if (GeoReferencingSystem->PlanetShape == EPlanetShape::FlatPlanet) Horizontal.Y *= -1.;
  AircraftState.UEForwardHorizontal = GeoReferencingSystem->GetTangentTransformAtECEFLocation(AircraftState.ECEFLocation).TransformVector(Horizontal);
  return true;
}

void UJSBSimMovementComponent::BeginDestroy()
{
  Super::BeginDestroy();

  // Make sure we destroy JSBSim too
  DeInitializeJSBSim();
}

void UJSBSimMovementComponent::BeginPlay()
{
  Super::BeginPlay();

  // Init local variables from Level Objects
  Parent = GetOwner();

  // A GeoReferencingSystem Actor is mandatory!
  if (!GeoReferencingSystem)
  {
    GeoReferencingSystem = AGeoReferencingSystem::GetGeoReferencingSystem(GetWorld());

    if (!GeoReferencingSystem)
    {
      UE_LOG(LogJSBSim, Error, TEXT("Impossible to use a UJSBSimMovementComponent without a GeoReferencingSystem."));
      SetComponentTickEnabled(false);
      return;
    }
  }

  if (Parent)
  {
    Parent->GetRootComponent()->SetMobility(EComponentMobility::Movable);
    LoadAircraft(false); // Start with a Fresh JSBSim object, but potentially with overridden properties
    PrepareJSBSim();
  }
}

void UJSBSimMovementComponent::OnRegister()
{
  Super::OnRegister();

  if (!GeoReferencingSystem)
  {
    GeoReferencingSystem = AGeoReferencingSystem::GetGeoReferencingSystem(GetWorld());

    if (!GeoReferencingSystem)
    {
      UE_LOG(LogJSBSim, Error, TEXT("Impossible to use a UJSBSimMovementComponent without a GeoReferencingSystem."));
    }
  }
}

/////////// JSBSim Protected methods

void UJSBSimMovementComponent::InitializeJSBSim()
{
  if (!JSBSimInitialized)
  {
    // Construct the JSBSim FDM
    Exec = new JSBSim::FGFDMExec();

    // Get pointers to main components
    Atmosphere = Exec->GetAtmosphere();
    Winds = Exec->GetWinds();
    FCS = Exec->GetFCS();
    MassBalance = Exec->GetMassBalance();
    Propulsion = Exec->GetPropulsion();
    Aircraft = Exec->GetAircraft();
    Propagate = Exec->GetPropagate();
    Auxiliary = Exec->GetAuxiliary();
    Inertial = Exec->GetInertial();
    Inertial->SetGroundCallback(new UEGroundCallback(this)); // Register ground callback.
    Aerodynamics = Exec->GetAerodynamics();
    GroundReactions = Exec->GetGroundReactions();
    Accelerations = Exec->GetAccelerations();
    IC = Exec->GetIC();
    PropertyManager = Exec->GetPropertyManager();

    // Initialize the Models location, relatively to this plugin

    // Get the base directory of this plugin
    FString BaseDir = IPluginManager::Get().FindPlugin("JSBSimFlightDynamicsModel")->GetBaseDir();
    // Add on the relative location of the third party dll and load it
    FString RootDirRelative = FPaths::Combine(*BaseDir, TEXT("Resources/JSBSim"));
    const FString& RootDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RootDirRelative);
    UE_LOG(LogJSBSim, Display, TEXT("Initializing JSBSimFlightDynamicsModel using Data in '%s'"), *RootDir);

    // Set data paths...
    FString AircraftPath(TEXT("aircraft"));
    FString EnginePath(TEXT("engine"));
    FString SystemPath(TEXT("systems"));

    Exec->SetRootDir(SGPath(TCHAR_TO_UTF8(*RootDir)));
    Exec->SetAircraftPath(SGPath(TCHAR_TO_UTF8(*AircraftPath)));
    Exec->SetEnginePath(SGPath(TCHAR_TO_UTF8(*EnginePath)));
    Exec->SetSystemsPath(SGPath(TCHAR_TO_UTF8(*SystemPath)));
    // Prepare Initial Conditions
    TrimNeeded = true;
    Trimmed = false;
    ResetSimulationPresentation();

    // Base setup done so far. The other part of initial setup will be done on begin play, in InitJSBSim.
    JSBSimInitialized = true;
  }
}

void UJSBSimMovementComponent::PrepareJSBSim()
{
  UE_LOG(LogJSBSim, Display, TEXT("PrepareJSBSim - Setting Initial Conditiond and computing initial state"));

  // The Aircraft should have been loaded first
  if (!AircraftLoaded)
  {
    return;
  }

  ResetSimulationPresentation();
  // Reset the current Aircraft State
  AircraftState.Reset();

  // First, consider the aircraft Transform in UE to define the Location and Orientation in JSBSim
  FGeographicCoordinates GeographicCoordinates;
  if (Parent)
  {
    // Get CG location in Geographic
    FVector CGWorldPosition = Parent->GetTransform().TransformPosition(CGLocalPosition);
    GeoReferencingSystem->EngineToGeographic(CGWorldPosition, GeographicCoordinates);

    // Computes Rotation in engine frame
    FTransform ENUTransform = GeoReferencingSystem->GetTangentTransformAtGeographicLocation(GeographicCoordinates);
    FQuat LocalECEFRotation = ENUTransform.InverseTransformRotation(Parent->GetActorQuat());
    FRotator PsiThetaPhi = LocalECEFRotation.Rotator();
    int AddYaw = 90; // JSBSim to UE is differnet by 90 degrees
    if (GeoReferencingSystem->PlanetShape == EPlanetShape::FlatPlanet) // Flat Planet has bug, off by 180 degrees.
    {
      AddYaw += 180;
    }

    // Set it as initial conditions
    IC->SetLongitudeDegIC(GeographicCoordinates.Longitude);
    IC->SetGeodLatitudeDegIC(GeographicCoordinates.Latitude);
    IC->SetAltitudeASLFtIC(GeographicCoordinates.Altitude * METER_TO_FEET);
    IC->SetPhiDegIC(PsiThetaPhi.Roll);
    IC->SetPsiDegIC(PsiThetaPhi.Yaw + AddYaw);
    IC->SetThetaDegIC(PsiThetaPhi.Pitch);
  }


  // Init our commands from the initial state
  Commands.Elevator = FCS->GetDeCmd();
  Commands.Aileron = FCS->GetDaCmd();
  Commands.Rudder = -FCS->GetDrCmd();
  Commands.PitchTrim = FCS->GetPitchTrimCmd();
  Commands.YawTrim = -FCS->GetYawTrimCmd();
  Commands.RollTrim = FCS->GetRollTrimCmd();
  // TODO - Others commands ?

  // Atmosphere
  if (ControlFDMAtmosphere)
  {
    Atmosphere->SetTemperature(TemperatureCelsius, GeographicCoordinates.Altitude * METER_TO_FEET, JSBSim::FGAtmosphere::eCelsius);
    Atmosphere->SetPressureSL(JSBSim::FGAtmosphere::ePascals, PressureSeaLevelhPa * 100.0);
    Winds->SetTurbType(JSBSim::FGWinds::ttNone);
    Winds->SetTurbGain(0.0);
    Winds->SetTurbRate(0.0);
    Winds->SetWindspeed20ft(0.0);
    Winds->SetProbabilityOfExceedence(0.0);
  }

  // Wind Speed
  IC->SetWindDirDegIC(WindHeading);
  IC->SetWindMagKtsIC(WindIntensityKts);

  // Aircraft Speed
  IC->SetVcalibratedKtsIC(InitialCalibratedAirSpeedKts);


  // Aircraft State
  // Flaps position
  Commands.Flap = FlapPositionAtStart;
  FCS->SetDfPos(ofNorm, FlapPositionAtStart);

  // Gear position
  if (bStartWithGearDown)
  {
    FCS->SetGearPos(1.0);
    Commands.GearDown = 1.0;
  }
  else
  {
    FCS->SetGearPos(0.0);
    Commands.GearDown = 0.0;
  }

  // Run IC to pre-initialize the JSBSim Initial Conditions for the model
  CopyToJSBSim();
  Exec->RunIC();
  UpdateLocalTransforms();

  if (bStartWithEngineRunning)
  {
    Propulsion->InitRunning(-1);

    int32 EngineCount = EngineCommands.Num();
    for (int32 i = 0; i < EngineCount; i++)
    {
      EngineCommands[i].Throttle = 0.0;
      EngineCommands[i].Mixture = 1.0;
      EngineCommands[i].Magnetos = EMagnetosMode::Both;
      EngineCommands[i].Running = true;
    }
  }

  if (TrimNeeded)
  {
    if (StartOnGround)
    {
      IC->SetVNorthFpsIC(0.0);
      IC->SetVEastFpsIC(0.0);
      IC->SetVDownFpsIC(0.0);
    }
    TrimNeeded = false;
    if (!DoTrim()) return;
  }

  // Seed both display endpoints from the trimmed state, with no first-frame visual jump.
  UpdateLocalTransforms();
  CopyFromJSBSim();
  FJSBSimPoseSample InitialPose;
  if (CaptureSimulationPose(InitialPose))
  {
    PoseHistory.Reset(InitialPose);
    if (Parent) ApplyCurrentActorTransform();
  }
}

void UJSBSimMovementComponent::ResetTelemetry()
{
  AircraftState.Reset();
  Gears.Reset();
  EngineStates.Reset();
  CGLocalPosition = EPLocalPosition = VRPLocalPosition = ECEFForwardHorizontal = FVector::ZeroVector;
  StructuralToActor = BodyToActor = FTransform::Identity;
  for (FTank& Tank : Tanks)
    Tank.CapacityGallons = Tank.FillPercentage = Tank.TemperatureCelcius = 0.;
}

void UJSBSimMovementComponent::DeInitializeJSBSim()
{
  ReadPropertyCache.Reset();
  ResetSimulationPresentation();
  ResetTelemetry();
  ++ModelGeneration;
  Trimmed = AircraftLoaded = false;
#if WITH_EDITOR
  IsReadyForCompVisualizer = false;
#endif
  // Release observers while Exec still owns the models and their property tree.
  Atmosphere = nullptr;
  Winds = nullptr;
  FCS = nullptr;
  MassBalance = nullptr;
  Propulsion = nullptr;
  Aircraft = nullptr;
  Propagate = nullptr;
  Auxiliary = nullptr;
  Inertial = nullptr;
  Aerodynamics = nullptr;
  GroundReactions = nullptr;
  Accelerations = nullptr;
  IC = nullptr;
  PropertyManager = nullptr;
  delete Exec;
  Exec = nullptr;
  JSBSimInitialized = false;
}

void UJSBSimMovementComponent::CopyToJSBSim()
{
  // Basic flight controls
  FCS->SetDaCmd(Commands.Aileron);
  FCS->SetRollTrimCmd(Commands.RollTrim);
  FCS->SetDeCmd(Commands.Elevator);
  FCS->SetPitchTrimCmd(Commands.PitchTrim);
  FCS->SetDrCmd(-Commands.Rudder); // Rudder
  FCS->SetDsCmd(Commands.Rudder); // Steering
  FCS->SetYawTrimCmd(-Commands.YawTrim);
  FCS->SetDfCmd(Commands.Flap);
  FCS->SetDsbCmd(Commands.SpeedBrake);
  FCS->SetDspCmd(Commands.Spoiler);

  // Gears and Brake controls
  FCS->SetLBrake(FMath::Max(Commands.LeftBrake, Commands.ParkingBrake));
  FCS->SetRBrake(FMath::Max(Commands.RightBrake, Commands.ParkingBrake));
  FCS->SetCBrake(FMath::Max(Commands.CenterBrake, Commands.ParkingBrake));
  FCS->SetGearCmd(Commands.GearDown);

  ApplyEnginesCommands();

  // TODO - Update atmosphere
/* Atmosphere->SetTemperature(temperature->getDoubleValue(), get_Altitude(), FGAtmosphere::eCelsius);
  Atmosphere->SetPressureSL(FGAtmosphere::eInchesHg, pressureSL->getDoubleValue());

  Winds->SetTurbType((FGWinds::tType)TURBULENCE_TYPE_NAMES[turbulence_model->getStringValue()]);
  switch (Winds->GetTurbType()) {
  case FGWinds::ttStandard:
  case FGWinds::ttCulp: {
    double tmp = turbulence_gain->getDoubleValue();
    Winds->SetTurbGain(tmp * tmp * 100.0);
    Winds->SetTurbRate(turbulence_rate->getDoubleValue());
    break;
  }
  case FGWinds::ttMilspec:
  case FGWinds::ttTustin: {
    // milspec turbulence: 3=light, 4=moderate, 6=severe turbulence
    // turbulence_gain normalized: 0: none, 1/3: light, 2/3: moderate, 3/3: severe
    double tmp = turbulence_gain->getDoubleValue();
    Winds->SetProbabilityOfExceedence(
      SGMiscd::roundToInt(TurbulenceSeverityTable.GetValue(tmp))
    );
    Winds->SetWindspeed20ft(ground_wind->getDoubleValue());
    break;
  }

  default:
    break;
  }

  Winds->SetWindNED(-wind_from_north->getDoubleValue(),
    -wind_from_east->getDoubleValue(),
    -wind_from_down->getDoubleValue());
*/

  CopyTankPropertiesToJSBSim();
}

void UJSBSimMovementComponent::CopyFromJSBSim()
{
  // Collect JSBSim data

  // Get Aircraft forward vector in local (ECEF tangent) space.
  // TODO - IDK if for the horizon indicator I should use the forward vector or the aircraft speed.
  // Maybe the aircraft speed would include some kind of lateral slip ---> May one expert fix it if needed...
  JSBSim::FGColumnVector3 ForwardLocal = Propagate->GetTb2l() * JSBSim::FGColumnVector3(1, 0, 0);
  ECEFForwardHorizontal = FVector(ForwardLocal(2), -ForwardLocal(1), 0);

  // Update Moving part state
  AircraftState.ElevatorPosition = FCS->GetDePos(JSBSim::ofDeg);
  AircraftState.LeftAileronPosition = FCS->GetDaLPos(JSBSim::ofDeg);
  AircraftState.RightAileronPosition = FCS->GetDaRPos(JSBSim::ofDeg);
  AircraftState.RudderPosition = -1 * FCS->GetDrPos(JSBSim::ofDeg);
  AircraftState.FlapPosition = FCS->GetDfPos(JSBSim::ofDeg);
  AircraftState.SpeedBrakePosition = FCS->GetDsbPos(JSBSim::ofDeg);
  AircraftState.SpoilersPosition = FCS->GetDspPos(JSBSim::ofDeg);

  // Speed
  AircraftState.CalibratedAirSpeedKts = Auxiliary->GetVcalibratedKTS();
  AircraftState.GroundSpeedKts = Auxiliary->GetVground() * FEET_PER_SEC_TO_KNOT;
  AircraftState.TotalVelocityKts = Auxiliary->GetVt() * FEET_PER_SEC_TO_KNOT;
  AircraftState.VelocityNEDfps.Set(Propagate->GetVel(JSBSim::FGJSBBase::eNorth), Propagate->GetVel(JSBSim::FGJSBBase::eEast), -Propagate->GetVel(JSBSim::FGJSBBase::eDown));
  AircraftState.AltitudeASLFt = Propagate->GetAltitudeASL();
  AircraftState.AltitudeRateFtps = Propagate->Gethdot();
  AircraftState.StallWarning = Aerodynamics->GetStallWarn();

  // Transformation
  JSBSim::FGLocation LocationVRP = Propagate->GetLocation();
  AircraftState.ECEFLocation = FVector(LocationVRP(1), LocationVRP(2), LocationVRP(3)) * FEET_TO_METER;
  AircraftState.Latitude = LocationVRP.GetGeodLatitudeDeg();
  AircraftState.Longitude = LocationVRP.GetLongitudeDeg();
  AircraftState.LocalEulerAngles.Yaw = FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::ePsi));
  AircraftState.LocalEulerAngles.Pitch = FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::eTht));
  AircraftState.LocalEulerAngles.Roll = FMath::RadiansToDegrees(Propagate->GetEuler(JSBSim::FGJSBBase::ePhi));
  AircraftState.EulerRates.Set(Auxiliary->GetEulerRates(JSBSim::FGJSBBase::ePhi), Auxiliary->GetEulerRates(JSBSim::FGJSBBase::eTht), Auxiliary->GetEulerRates(JSBSim::FGJSBBase::ePsi));
  AircraftState.AltitudeAGLFt = Propagate->GetDistanceAGL();
  // force a sim crashed if crashed (altitude AGL < 0)
  if (AircraftState.AltitudeAGLFt < -10.0 || AircraftState.AltitudeASLFt < -10.0) {
      CrashedEvent();
  }

  // Copy the fuel levels from JSBSim if fuel
  // freeze not enabled.
  CopyTankPropertiesFromJSBSim();
  CopyGearPropertiesFromJSBSim();
  GetEnginesStates();
}

/////////// JSBSim Private methods
bool UJSBSimMovementComponent::DoTrim()
{
  // A real collision surface is required at the requested ground start.
  // Provisional IC queries elsewhere may use the ellipsoid fallback in UEGroundCallback.
  if (StartOnGround)
  {
    const auto& Position = IC->GetPosition();
    const FVector ECEF(Position(1) * FEET_TO_METER, Position(2) * FEET_TO_METER, Position(3) * FEET_TO_METER);
    FVector Contact, Normal;
    bool GroundHit = false;
    GetAGLevel(ECEF, Contact, Normal, &GroundHit);
    if (!GroundHit)
    {
      Trimmed = false;
      UE_LOG(LogJSBSim, Error, TEXT("Initial ground trim aborted for %s: no ground collision below the start position."),
        *GetNameSafe(GetOwner()));
      return false;
    }
  }

  JSBSim::FGTrim Trim(Exec, StartOnGround ? JSBSim::tGround : JSBSim::tFull);
  Trimmed = Trim.DoTrim();
  if (!Trimmed)
  {
    Trim.Report();
    Trim.TrimStats();
    UE_LOG(LogJSBSim, Error, TEXT("Initial %s trim failed for %s; simulation stopped. Check ground collision and initial conditions."),
      StartOnGround ? TEXT("ground") : TEXT("flight"), *GetNameSafe(GetOwner()));
    return false;
  }

  // Preserve the complete trim solution when UE first submits commands.
  Commands.Elevator = FCS->GetDeCmd();
  Commands.Aileron = FCS->GetDaCmd();
  Commands.Rudder = -FCS->GetDrCmd();
  Commands.PitchTrim = FCS->GetPitchTrimCmd();
  Commands.YawTrim = -FCS->GetYawTrimCmd();
  Commands.RollTrim = FCS->GetRollTrimCmd();
  for (int32 Index = 0; Index < EngineCommands.Num(); ++Index)
  {
    EngineCommands[Index].Throttle = FCS->GetThrottleCmd(Index);
  }
  UE_LOG(LogJSBSim, Display, TEXT("Initial %s trim succeeded for %s."),
    StartOnGround ? TEXT("ground") : TEXT("flight"), *GetNameSafe(GetOwner()));
  return true;
}

void UJSBSimMovementComponent::UpdateLocalTransforms()
{
  if (MassBalance == nullptr || Aircraft == nullptr || GroundReactions == nullptr)
    return;

  // Structural Frame To Actor Frame
  FMatrix StructuralToActorMatrix(FMatrix::Identity);
  StructuralToActorMatrix.SetAxis(0, FVector(-1, 0, 0));
  StructuralToActorMatrix.SetAxis(1, FVector(0, 1, 0));
  StructuralToActorMatrix.SetAxis(2, FVector(0, 0, 1));
  StructuralToActorMatrix.SetOrigin(StructuralFrameOrigin);
  StructuralToActor.SetFromMatrix(StructuralToActorMatrix);

  // Get Gravity Center
  JSBSim::FGColumnVector3 CGLocationStructural = MassBalance->StructuralToBody(JSBSim::FGColumnVector3()) * FEET_TO_CENTIMETER;
  CGLocalPosition = StructuralToActor.TransformPosition(FVector(CGLocationStructural(1), CGLocationStructural(2), CGLocationStructural(3)));

  // Body Frame to Actor Frame
  FMatrix BodyToActorMatrix(FMatrix::Identity);
  BodyToActorMatrix.SetAxis(0, FVector(1, 0, 0));
  BodyToActorMatrix.SetAxis(1, FVector(0, 1, 0));
  BodyToActorMatrix.SetAxis(2, FVector(0, 0, -1));
  BodyToActorMatrix.SetOrigin(CGLocalPosition);
  BodyToActor.SetFromMatrix(BodyToActorMatrix);

  // Eye Position
  JSBSim::FGColumnVector3 EPLocationStructural = Aircraft->GetXYZep() * INCH_TO_CENTIMETER;
  EPLocalPosition = StructuralToActor.TransformPosition(FVector(EPLocationStructural(1), EPLocationStructural(2), EPLocationStructural(3)));

  // Visual Reference Position
  JSBSim::FGColumnVector3 VRPLocationStructural = Aircraft->GetXYZvrp() * INCH_TO_CENTIMETER;
  VRPLocalPosition = StructuralToActor.TransformPosition(FVector(VRPLocationStructural(1), VRPLocationStructural(2), VRPLocationStructural(3)));

}

void UJSBSimMovementComponent::CopyGearPropertiesFromJSBSim()
{
  if (!AircraftLoaded || !GroundReactions) { Gears.Reset(); return; }
  Gears.SetNum(GroundReactions->GetNumGearUnits());
  for (int32 Index = 0; Index < Gears.Num(); ++Index)
    ReadGearTelemetry(Gears[Index], *GroundReactions->GetGearUnit(Index), BodyToActor, IsSimulationReady());
}

// Tanks
void UJSBSimMovementComponent::InitTankDefaultProperties()
{
  // Set initial fuel levels if overridden by the user.
  uint32 TanksCount = Propulsion->GetNumTanks();
  Tanks.Empty();
  if (TanksCount > 0)
  {
    Tanks.SetNum(TanksCount);
    for (unsigned int i = 0; i < TanksCount; i++)
    {
      std::shared_ptr<JSBSim::FGTank> Tank = Propulsion->GetTank(i);

      Tanks[i].FuelDensityPoundsPerGallon = Tank->GetDensity();
      Tanks[i].ContentGallons = Tank->GetContentsGallons();
      Tanks[i].CapacityGallons = Tank->GetCapacityGallons();
      Tanks[i].FillPercentage = Tank->GetPctFull();
      Tanks[i].TemperatureCelcius = Tank->GetTemperature_degC();
    }
  }
}

void UJSBSimMovementComponent::CopyTankPropertiesToJSBSim()
{
  for (unsigned i = 0; i < Propulsion->GetNumTanks(); i++)
  {
    std::shared_ptr<JSBSim::FGTank> tank = Propulsion->GetTank(i);

    if ((int32)i < Tanks.Num())
    {
      FTank UETank = Tanks[i];
      double fuelDensity = UETank.FuelDensityPoundsPerGallon;

      if (fuelDensity < 0.1)
        fuelDensity = 6.0; // Use average fuel value

      // Only editable properties
      tank->SetDensity(fuelDensity);
      tank->SetContentsGallons(UETank.ContentGallons);
    }
  }
}

void UJSBSimMovementComponent::CopyTankPropertiesFromJSBSim()
{
  FuelFreeze = Propulsion->GetFuelFreeze();
  for (unsigned int i = 0; i < Propulsion->GetNumTanks(); i++)
  {
    std::shared_ptr<JSBSim::FGTank> Tank = Propulsion->GetTank(i);

    if ((int32)i < Tanks.Num())
    {
      Tanks[i].FuelDensityPoundsPerGallon = Tank->GetDensity();
      Tanks[i].ContentGallons = Tank->GetContentsGallons();
      Tanks[i].CapacityGallons = Tank->GetCapacityGallons();
      Tanks[i].FillPercentage = Tank->GetPctFull();
      Tanks[i].TemperatureCelcius = Tank->GetTemperature_degC();
    }
  }
}


// Engines
void UJSBSimMovementComponent::InitEnginesCommandAndStates()
{
  EngineCommands.Empty();
  EngineStates.Empty();

  uint32 EngineCount = Propulsion->GetNumEngines();
  if (EngineCount > 0)
  {
    // Allocate UE equivalent structures
    EngineCommands.SetNum(EngineCount);
    EngineStates.SetNum(EngineCount);

  }
}

void UJSBSimMovementComponent::ApplyEnginesCommands()
{
  // Global to all engines
  Propulsion->SetFuelFreeze(FuelFreeze);

  // For each engine
  int32 EngineCount = EngineCommands.Num();
  for (int32 i = 0; i < EngineCount; i++)
  {
    FEngineCommand EngineCommand = EngineCommands[i];

    // Global FCS Commands
    FCS->SetThrottleCmd(i, EngineCommand.Throttle);
    FCS->SetMixtureCmd(i, EngineCommand.Mixture);
    FCS->SetPropAdvanceCmd(i, EngineCommand.PropellerAdvance);
    FCS->SetFeatherCmd(i, EngineCommand.PropellerFeather);

    // Common FGEngine code block
    std::shared_ptr < JSBSim::FGEngine> CommonEngine = Propulsion->GetEngine(i);
    CommonEngine->SetStarter(EngineCommand.Starter);
    CommonEngine->SetRunning(EngineCommand.Running);

    switch (Propulsion->GetEngine(i)->GetType())
    {
    case JSBSim::FGEngine::etPiston:
    {
      // FGPiston code block
      std::shared_ptr < JSBSim::FGPiston> PistonEngine = std::static_pointer_cast<JSBSim::FGPiston>(Propulsion->GetEngine(i));
      PistonEngine->SetMagnetos((int)EngineCommand.Magnetos);
      break;
    }
    case JSBSim::FGEngine::etTurbine:
    {
      // FGTurbine code block
      std::shared_ptr < JSBSim::FGTurbine> TurbineEngine = std::static_pointer_cast<JSBSim::FGTurbine>(Propulsion->GetEngine(i));
      TurbineEngine->SetReverse(EngineCommand.Reverse);
      TurbineEngine->SetCutoff(EngineCommand.CutOff);
      TurbineEngine->SetIgnition(EngineCommand.Ignition);
      TurbineEngine->SetAugmentation(EngineCommand.Augmentation);
      TurbineEngine->SetInjection(EngineCommand.Injection);
      break;
    }
    case JSBSim::FGEngine::etRocket:
    {
      // FGRocket code block
      // FGRocket* RocketEngine = (FGRocket*)Propulsion->GetEngine(i);
      break;
    }
    case JSBSim::FGEngine::etTurboprop:
    {
      // FGTurboProp code block
      std::shared_ptr < JSBSim::FGTurboProp> TurboPropEngine = std::static_pointer_cast<JSBSim::FGTurboProp>(Propulsion->GetEngine(i));
      TurboPropEngine->SetReverse(EngineCommand.Reverse);
      TurboPropEngine->SetCutoff(EngineCommand.CutOff);
      TurboPropEngine->SetGeneratorPower(EngineCommand.GeneratorPower);
      TurboPropEngine->SetCondition(EngineCommand.Condition);
      break;
    }
    default:
      break;
    }
  }
}

void UJSBSimMovementComponent::GetEnginesStates()
{
  if (!AircraftLoaded || !Propulsion) { EngineStates.Reset(); return; }
  EngineStates.SetNum(Propulsion->GetNumEngines());
  for (int32 Index = 0; Index < EngineStates.Num(); ++Index)
  {
    FEngineState& State = EngineStates[Index];
    State = FEngineState{};
    auto Engine = Propulsion->GetEngine(Index);
    State.EngineType = static_cast<EEngineType>(Engine->GetType());
    if (!IsSimulationReady()) continue;
    State.Starter = Engine->GetStarter();
    State.Running = Engine->GetRunning();
    State.Thrust = FiniteTelemetry(Engine->GetThrust());
    if (Engine->GetThruster()) State.EngineRPM = FiniteTelemetry(Engine->GetThruster()->GetEngineRPM());
    switch (Engine->GetType())
    {
    case JSBSim::FGEngine::etPiston:
      State.Magnetos = static_cast<EMagnetosMode>(std::static_pointer_cast<JSBSim::FGPiston>(Engine)->GetMagnetos());
      break;
    case JSBSim::FGEngine::etTurbine:
    {
      auto Turbine = std::static_pointer_cast<JSBSim::FGTurbine>(Engine);
      const double Nozzle = Turbine->GetNozzle();
      State.HasNozzlePosition = FMath::IsFinite(Nozzle);
      State.NozzlePosition = State.HasNozzlePosition ? Nozzle : 0.;
      State.N1 = FiniteTelemetry(Turbine->GetN1());
      State.N2 = FiniteTelemetry(Turbine->GetN2());
      State.Augmentation = Turbine->GetAugmentation();
      State.Reversed = Turbine->GetReversed();
      State.Injection = Turbine->GetInjection();
      State.CutOff = Turbine->GetCutoff();
      State.Ignition = Turbine->GetIgnition();
      break;
    }
    default:
      break;
    }
  }
}

/////////// Logging and Debugging Methods

void UJSBSimMovementComponent::CrashedEvent()
{
    Exec->SuspendIntegration();
    AircraftState.Crashed = true;
    AircraftCrashed.Broadcast();
}

void UJSBSimMovementComponent::DrawDebugMessage()
{
  // Build message string before displaying it at once
  FString DebugMessage;

  // Commands
  DebugMessage += Commands.GetDebugMessage();

  // Engines
  int32 NumEngines = EngineCommands.Num();
  // Engine Commands
  DebugMessage += LINE_TERMINATOR;
  DebugMessage += FString::Printf(TEXT("Engines Commands (%d) : "), NumEngines) + LINE_TERMINATOR;
  for (int32 i = 0; i < NumEngines; i++)
  {
    DebugMessage += FString::Printf(TEXT("    #%d    "), i) + EngineCommands[i].GetDebugMessage();
  }
  // Engine States
  DebugMessage += LINE_TERMINATOR;
  DebugMessage += FString::Printf(TEXT("Engines States (%d) : "), NumEngines) + LINE_TERMINATOR;
  for (int32 i = 0; i < NumEngines; i++)
  {
    DebugMessage += FString::Printf(TEXT("    #%d    "), i) + EngineStates[i].GetDebugMessage();
  }

  // Tanks
  DebugMessage += LINE_TERMINATOR;
  int32 NumTanks = Tanks.Num();
  DebugMessage += FString::Printf(TEXT("Tanks (%d) : "), NumTanks) + LINE_TERMINATOR;
  for (int32 i = 0; i < NumTanks; i++)
  {
    DebugMessage += FString::Printf(TEXT("    #%d    "), i) + Tanks[i].GetDebugMessage();
  }

  // Gears
  DebugMessage += LINE_TERMINATOR;
  int32 NumGears = Gears.Num();
  DebugMessage += FString::Printf(TEXT("Landing Gears (%d) : "), NumGears) + LINE_TERMINATOR;
  for (int32 i = 0; i < NumGears; i++)
  {
    if (Gears[i].IsBogey)
    {
      DebugMessage += FString::Printf(TEXT("    #%d    "), i) + Gears[i].GetDebugMessage();
    }
  }

  // Aircraft State
  DebugMessage += LINE_TERMINATOR;
  DebugMessage += AircraftState.GetDebugMessage();

  // Draw
  FVector2D TextScale = FVector2D::UnitVector;
  GEngine->AddOnScreenDebugMessage(1, 0, FColor::Green, *DebugMessage, false, TextScale);
}

void UJSBSimMovementComponent::DrawDebugObjects()
{
  for (const FGear& Gear : Gears)
  {
    FVector WorldPosition = GetOwner()->GetTransform().TransformPosition(Gear.RelativeLocation);

    if (Gear.IsBogey)
    {
      if (Gear.HasWeightOnWheel)
      {
        DrawDebugPoint(GetWorld(), WorldPosition, 8, FColor::Red, false);
        DrawDebugLine(GetWorld(), WorldPosition, WorldPosition + GetOwner()->GetActorQuat().RotateVector(Gear.Force) * DebugForceCentimetersPerNewton, FColor::Red, false, -1, 0, 3);
      }
      else
      {
        DrawDebugPoint(GetWorld(), WorldPosition, 8, FColor::Green, false);

      }
    }
    else
    {
      DrawDebugPoint(GetWorld(), WorldPosition, 8, FColor(128, 128, 128), false);
    }
  }



}


/////////// In-Editor Specific
#if WITH_EDITOR

void UJSBSimMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
  static const FName NAME_AircraftModel = GET_MEMBER_NAME_CHECKED(UJSBSimMovementComponent, AircraftModel);
  if (PropertyChangedEvent.Property != nullptr && PropertyChangedEvent.Property->GetFName() == NAME_AircraftModel)
  {
    // Load the aircraft, and make sure we recreate the Component properties for this new one.
    LoadAircraft(true);
  }

  static const FName NAME_StructuralFrameOrigin = GET_MEMBER_NAME_CHECKED(UJSBSimMovementComponent, StructuralFrameOrigin);
  if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == NAME_StructuralFrameOrigin)
  {
    UpdateLocalTransforms();
    CopyGearPropertiesFromJSBSim();
  }

  Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UJSBSimMovementComponent::PrepareModelForCompVisualizer()
{
  if (!IsReadyForCompVisualizer)
  {
    if (!AircraftLoaded || !Exec || AircraftModel.Compare(UTF8_TO_TCHAR(Exec->GetModelName().c_str()), ESearchCase::IgnoreCase) != 0)
      LoadAircraft(false);
    if (AircraftLoaded && !Trimmed) PrepareJSBSim();
    IsReadyForCompVisualizer = IsSimulationReady();
  }
}

#endif
