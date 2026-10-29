#include "AircraftCalibrationLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Animation/AnimSequenceHelpers.h"
#include "AssetCompilingManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

namespace
{
TMap<UAnimSequence*, TUniquePtr<UE::Anim::Compression::FScopedCompressionGuard>> EditingGuards;
bool IsCalibrationAsset(const UAnimSequenceBase* Asset)
{
    return Asset && Asset->GetPathName().StartsWith(TEXT("/Game/Assets/Models/Jets/J10CE/Calibration/"));
}
class FCalibrationModels : public UE::Anim::DataModel::IAnimationDataModels
{
    virtual UClass* GetModelClass(UAnimSequenceBase* Asset) const override
    {
        return IsCalibrationAsset(Asset) ? UAnimDataModel::StaticClass() : nullptr;
    }
};
class FAircraftCalibrationToolsModule : public IModuleInterface
{
    FCalibrationModels Models;
public:
    virtual void StartupModule() override
    {
        FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("AnimationData"));
        auto& Registry = IModularFeatures::Get();
        const FName Feature = UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName();
        auto Existing = Registry.GetModularFeatureImplementations<UE::Anim::DataModel::IAnimationDataModels>(Feature);
        // Preserve existing provider order; the scoped provider wins only for calibration assets.
        for (auto* Provider : Existing) Registry.UnregisterModularFeature(Feature, Provider);
        Registry.RegisterModularFeature(Feature, &Models);
        for (int32 Index = Existing.Num() - 1; Index >= 0; --Index) Registry.RegisterModularFeature(Feature, Existing[Index]);
    }
    virtual void ShutdownModule() override
    {
        EditingGuards.Empty();
        IModularFeatures::Get().UnregisterModularFeature(UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(), &Models);
    }
};
}
IMPLEMENT_MODULE(FAircraftCalibrationToolsModule, AircraftCalibrationTools)

bool UAircraftCalibrationLibrary::BeginCalibrationAnimation(UAnimSequence* Sequence)
{
    if (!IsCalibrationAsset(Sequence) || EditingGuards.Contains(Sequence)) return false;
    FAssetCompilingManager::Get().FinishAllCompilation();
    EditingGuards.Add(Sequence, MakeUnique<UE::Anim::Compression::FScopedCompressionGuard>(Sequence));
    return true;
}

bool UAircraftCalibrationLibrary::FinalizeCalibrationAnimation(UAnimSequence* Sequence)
{
    if (!IsCalibrationAsset(Sequence)) return false;
    IAnimationDataController& Controller = Sequence->GetController();
    Controller.NotifyPopulated();
    EditingGuards.Remove(Sequence);
    Sequence->MarkPackageDirty();
    Sequence->PostEditChange();
    return Controller.GetModelInterface().GetObject()->IsA<UAnimDataModel>()
        && Sequence->GetSamplingFrameRate() == Controller.GetModel()->GetFrameRate();
}

bool UAircraftCalibrationLibrary::ExportCalibrationGeometry(USkeletalMeshComponent* Component, const FString& Filename)
{
    if (!Component) return false;
    const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
    if (!Mesh || Mesh->GetPathName() != TEXT("/Game/Assets/Models/Jets/J10CE/SK_Mesh_J10CE.SK_Mesh_J10CE")) return false;
    const FSkeletalMeshModel* Imported = Mesh->GetImportedModel();
    if (!Imported || Imported->LODModels.IsEmpty()) return false;
    const FSkeletalMeshLODModel& LOD = Imported->LODModels[0];
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    const auto& InverseReference = Mesh->GetRefBasesInvMatrix();
    const auto& Pose = Component->GetComponentSpaceTransforms();
    if (Pose.Num() != Skeleton.GetNum()) return false;
    auto VectorJSON = [](const FVector& V) {
        TArray<TSharedPtr<FJsonValue>> A;
        A.Add(MakeShared<FJsonValueNumber>(V.X)); A.Add(MakeShared<FJsonValueNumber>(V.Y)); A.Add(MakeShared<FJsonValueNumber>(V.Z));
        return A;
    };
    TArray<TSharedPtr<FJsonValue>> Vertices; Vertices.SetNum(LOD.NumVertices);
    TArray<TSharedPtr<FJsonValue>> Sections;
    for (const FSkelMeshSection& Section : LOD.Sections)
    {
        TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
        S->SetNumberField(TEXT("base_vertex"), Section.BaseVertexIndex);
        S->SetNumberField(TEXT("vertices"), Section.SoftVertices.Num());
        S->SetNumberField(TEXT("material_index"), Section.MaterialIndex);
        if (Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex))
            S->SetStringField(TEXT("material_slot"), Mesh->GetMaterials()[Section.MaterialIndex].MaterialSlotName.ToString());
        Sections.Add(MakeShared<FJsonValueObject>(S));
        for (int32 Index = 0; Index < Section.SoftVertices.Num(); ++Index)
        {
            const FSoftSkinVertex& Vertex = Section.SoftVertices[Index];
            FVector Position = FVector::ZeroVector;
            uint32 Total = 0, Maximum = 0; int32 Primary = INDEX_NONE;
            for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
            {
                const uint32 Weight = Vertex.InfluenceWeights[Influence];
                if (!Weight || !Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Influence])) continue;
                const int32 Bone = Section.BoneMap[Vertex.InfluenceBones[Influence]];
                const FVector3f Local(InverseReference[Bone].TransformPosition(Vertex.Position));
                Position += Pose[Bone].TransformPosition(FVector(Local)) * static_cast<double>(Weight);
                Total += Weight;
                if (Weight > Maximum) { Maximum = Weight; Primary = Bone; }
            }
            if (!Total || Primary == INDEX_NONE) return false;
            Position /= static_cast<double>(Total);
            TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
            V->SetArrayField(TEXT("p"), VectorJSON(Position));
            V->SetStringField(TEXT("bone"), Skeleton.GetBoneName(Primary).ToString());
            V->SetNumberField(TEXT("weight"), static_cast<double>(Maximum) / Total);
            V->SetNumberField(TEXT("section"), Sections.Num() - 1);
            Vertices[Section.BaseVertexIndex + Index] = MakeShared<FJsonValueObject>(V);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Indices;
    for (uint32 Index : LOD.IndexBuffer) Indices.Add(MakeShared<FJsonValueNumber>(Index));
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    Result->SetStringField(TEXT("space"), TEXT("Mesh/component-local centimeters, current evaluated pose"));
    Result->SetArrayField(TEXT("vertices"), Vertices);
    Result->SetArrayField(TEXT("indices"), Indices);
    Result->SetArrayField(TEXT("sections"), Sections);
    FString JSON;
    return FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&JSON)) && FFileHelper::SaveStringToFile(JSON, *Filename);
}
