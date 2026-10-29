#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Debug/Flight/FighterFlightDebugSnapshot.h"
#include "FighterFlightDebugWidget.generated.h"

class UTextBlock;
class UVerticalBox;
class USizeBox;
class UFont;
class UJSBSimMovementComponent;

/** Passive flight-test UMG. Independent of the input debugger and the legacy JSBSim overlay. */
UCLASS(Blueprintable)
class AIRCOMBAT_API UFighterFlightDebugWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFighterFlightDebugWidget(const FObjectInitializer& Initializer);
    virtual bool Initialize() override;
    virtual void NativeDestruct() override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const override;

    UFUNCTION(BlueprintCallable, Category="Flight Debug") void StartMonitoring();
    UFUNCTION(BlueprintCallable, Category="Flight Debug") void StopMonitoring();
    UFUNCTION(BlueprintCallable, Category="Flight Debug") void RefreshNow();
    UFUNCTION(BlueprintCallable, Category="Flight Debug") void SetPage(EFighterFlightDebugPage InPage);
    UFUNCTION(BlueprintPure, Category="Flight Debug") bool IsMonitoring() const { return bMonitoring; }
    UPROPERTY(BlueprintReadOnly, Category="Flight Debug") FFighterFlightDebugSnapshot Snapshot;
    UPROPERTY(BlueprintReadOnly, Category="Flight Debug") EFighterFlightDebugPage Page = EFighterFlightDebugPage::Overview;
    UPROPERTY(BlueprintReadOnly, Category="Flight Debug") int32 RefreshCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Flight Debug") int32 PhysicsSamples = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight Debug|Style") float PanelWidth = 500.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight Debug|Style") float FontSize = 12.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Flight Debug|Style") FLinearColor Background = FLinearColor(.015f,.025f,.04f,.88f);
private:
    void BuildTree();
    void BindSource();
    void SamplePhysicsStep();
    UTextBlock* MakeText(const FString& Value, const FLinearColor& Color);
    void UpdateText();
    UPROPERTY(Transient) TObjectPtr<UFont> Font;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OverviewText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PageTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Statistics;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> DetailsBox;
    UPROPERTY(Transient) TObjectPtr<USizeBox> TrendArea;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Values;
    TWeakObjectPtr<UJSBSimMovementComponent> BoundSource;
    uint32 BoundGeneration = 0;
    FDelegateHandle StepHandle;
    FTimerHandle RefreshTimer;
    bool bMonitoring = false;
    FString Schema;
    double LastRefreshWallSeconds = 0.;
    uint64 LastRefreshFrame = 0;
    FFighterFlightDebugHistory History;
    TArray<FFighterFlightDebugHistory::FSample> GraphSamples;
};
