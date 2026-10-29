#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Pawns/Fighter/Fighter.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterFixedVisualFrameTest, "AirCombat.Visual.FixedFrameAndCameraAnchors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterFixedVisualFrameTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UClass* GeoClass = LoadClass<AActor>(nullptr, TEXT("/Script/GeoReferencing.GeoReferencingSystem"));
    if (!TestNotNull(TEXT("The FDM's georeferencing dependency is loaded"), GeoClass))
    {
        World->DestroyWorld(false);
        return false;
    }
    World->SpawnActor<AActor>(GeoClass);
    AFighter* Fighter = World->SpawnActor<AFighter>();
    auto* Display = FindObject<USceneComponent>(Fighter, TEXT("PresentationRoot"));
    auto* CG = FindObject<USceneComponent>(Fighter, TEXT("AircraftCG"));
    auto* Mesh = FindObject<USkeletalMeshComponent>(Fighter, TEXT("Airframe"));
    auto* CockpitAnchor = FindObject<USceneComponent>(Fighter, TEXT("CockpitAnchor"));
    auto* Focus = FindObject<USceneComponent>(Fighter, TEXT("ChaseFocus"));
    auto* Arm = FindObject<USpringArmComponent>(Fighter, TEXT("ChaseArm"));
    auto* Cockpit = FindObject<UCameraComponent>(Fighter, TEXT("CockpitCamera"));
    if (!Display || !CG || !Mesh || !CockpitAnchor || !Focus || !Arm || !Cockpit)
    {
        AddError(TEXT("The fixed visual hierarchy is incomplete"));
        World->DestroyWorld(false);
        return false;
    }
    TestNull(TEXT("There is no second mesh calibration component"), FindObject<USceneComponent>(Fighter, TEXT("VisualRoot")));
    TestTrue(TEXT("Imported mesh space equals the shared display frame"), Mesh->GetAttachParent() == Display
        && Mesh->GetRelativeTransform().Equals(FTransform::Identity));
    TestTrue(TEXT("Both camera anchors share mesh space"), CockpitAnchor->GetAttachParent() == Display && Focus->GetAttachParent() == Display);
    Fighter->GetFlightDynamics()->StructuralFrameOrigin = FVector(-550., 0., 190.);
    CockpitAnchor->SetRelativeLocation(FVector(320., 0., 260.));
    Focus->SetRelativeLocation(FVector(-200., 0., 190.));
    Fighter->SetActorLocationAndRotation(FVector(123., 456., 789.), FRotator(25., 70., 40.));
    const FTransform MeshBefore = Mesh->GetComponentTransform();
    const FVector EyeBefore = Cockpit->GetComponentLocation();
    const FVector FocusBefore = Focus->GetComponentLocation();
    auto* FDM = Fighter->GetFlightDynamics();
    const FVector StructuralOriginBefore = FDM->StructuralFrameOrigin;
    FDM->CGLocalPosition = FVector(487., 0., -9.);
    Fighter->Tick(.016f);
    FDM->CGLocalPosition = FVector(450., 20., -15.);
    Fighter->Tick(.016f);
    TestTrue(TEXT("Fuel/load CG changes cannot move visual geometry within the aircraft"), Mesh->GetComponentTransform().Equals(MeshBefore));
    TestTrue(TEXT("Pilot eye position stays rigidly attached to the cockpit"), Cockpit->GetComponentLocation().Equals(EyeBefore));
    TestTrue(TEXT("Chase focus is independent of CG drift"), Focus->GetComponentLocation().Equals(FocusBefore));
    TestTrue(TEXT("CG marker still reports the current physical CG"), CG->GetRelativeLocation().Equals(FDM->CGLocalPosition));
    TestTrue(TEXT("CG drift cannot change the authored structural datum"), FDM->StructuralFrameOrigin.Equals(StructuralOriginBefore));
    TestTrue(TEXT("SpringArm uses the authored visual focus"), Arm->GetAttachParent() == Focus);
    World->DestroyWorld(false);
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterF16GeometryTest, "AirCombat.Visual.F16ReferenceGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFighterF16GeometryTest::RunTest(const FString& Parameters)
{
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/VigilanteMovement/Vehicles/West_Fighter_F16C/SK_West_Fighter_F16C.SK_West_Fighter_F16C"));
    if (!TestNotNull(TEXT("F16C visual asset"), Mesh)) return false;
    const FSkeletalMeshModel* Imported = Mesh->GetImportedModel();
    if (!Imported || Imported->LODModels.IsEmpty())
    {
        AddError(TEXT("F16C imported LOD0 geometry is required for calibration"));
        return false;
    }
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    TArray<FTransform> ComponentPose;
    for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
    {
        const int32 Parent = Skeleton.GetParentIndex(Index);
        const FTransform Local = Skeleton.GetRefBonePose()[Index];
        ComponentPose.Add(Parent == INDEX_NONE ? Local : Local * ComponentPose[Parent]);
    }
    TArray<FString> Rows;
    Rows.Add(TEXT("bone,vertices,center_x,center_y,center_z,min_x,min_y,min_z,max_x,max_y,max_z"));
    for (const FName Bone : {FName(TEXT("front_wheel_jnt")), FName(TEXT("lf_wheel_jnt")), FName(TEXT("rt_wheel_jnt"))})
    {
        const int32 BoneIndex = Skeleton.FindBoneIndex(Bone);
        if (!TestTrue(TEXT("Calibration bone exists"), BoneIndex != INDEX_NONE)) return false;
        FBox Bounds(ForceInit);
        int32 Count = 0;
        for (const FSkelMeshSection& Section : Imported->LODModels[0].Sections)
            for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
            {
                uint32 Weight = 0;
                for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
                    if (Vertex.InfluenceWeights[Influence] && Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Influence])
                        && Section.BoneMap[Vertex.InfluenceBones[Influence]] == BoneIndex)
                        Weight += Vertex.InfluenceWeights[Influence];
                if (Weight >= 32768)
                {
                    Bounds += FVector(Vertex.Position);
                    ++Count;
                }
            }
        const FVector Center = ComponentPose[BoneIndex].GetLocation();
        if (!TestTrue(TEXT("Calibration uses actual skinned vertices"), Count > 0)) return false;
        Rows.Add(FString::Printf(TEXT("%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f"),
            *Bone.ToString(), Count, Center.X, Center.Y, Center.Z,
            Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));
    }
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Tests");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("Measured F16 geometry report is available for migration and verification"),
        FFileHelper::SaveStringArrayToFile(Rows, *(Folder / TEXT("F16VisualGeometry.csv"))));
    return true;
}
#endif
#endif
