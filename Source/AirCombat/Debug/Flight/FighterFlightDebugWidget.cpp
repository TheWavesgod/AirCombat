#include "Debug/Flight/FighterFlightDebugWidget.h"
#include "Pawns/Fighter/Fighter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FLinearColor Cyan(.3f,.85f,1.f), Light(.88f,.93f,.98f), Muted(.5f,.62f,.7f);
}

UFighterFlightDebugWidget::UFighterFlightDebugWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(*UWidget::GetDefaultFontName());
    Font = DefaultFont.Object;
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
bool UFighterFlightDebugWidget::Initialize()
{
    if (!Super::Initialize()) return false;
    BuildTree();
    return true;
}
UTextBlock* UFighterFlightDebugWidget::MakeText(const FString& Value, const FLinearColor& Color)
{
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetFont(FSlateFontInfo(Font.Get(), FontSize, TEXT("Regular")));
    Text->SetText(FText::FromString(Value)); Text->SetColorAndOpacity(Color);
    return Text;
}
void UFighterFlightDebugWidget::BuildTree()
{
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FlightMonitorRoot"));
    Root->SetVisibility(ESlateVisibility::HitTestInvisible); WidgetTree->RootWidget = Root;
    auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
    Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly);
    auto* PanelSlot = Root->AddChildToCanvas(Fit);
    PanelSlot->SetAnchors(FAnchors(1.f,0.f,1.f,1.f)); PanelSlot->SetAlignment(FVector2D(1.f,0.f));
    PanelSlot->SetOffsets(FMargin(-16.f,16.f,PanelWidth + 24.f,16.f));
    auto* Panel = WidgetTree->ConstructWidget<UBorder>(); Panel->SetBrushColor(Background); Panel->SetPadding(FMargin(12.f,10.f)); Fit->SetContent(Panel);
    auto* FitSlot = CastChecked<UScaleBoxSlot>(Panel->Slot);
    FitSlot->SetHorizontalAlignment(HAlign_Right); FitSlot->SetVerticalAlignment(VAlign_Top);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(PanelWidth); Panel->SetContent(Size);
    auto* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Size->SetContent(Box);
    Title = MakeText(TEXT("FLIGHT MONITOR"), Cyan); Box->AddChildToVerticalBox(Title);
    Status = MakeText(TEXT("NO AIRCRAFT"), Muted); Status->SetAutoWrapText(true); Box->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,3,0,6));
    OverviewText = MakeText(TEXT(""), Light); OverviewText->SetAutoWrapText(true); Box->AddChildToVerticalBox(OverviewText);
    PageTitle = MakeText(TEXT("OVERVIEW"), Cyan); Box->AddChildToVerticalBox(PageTitle)->SetPadding(FMargin(0,9,0,4));
    DetailsBox = WidgetTree->ConstructWidget<UVerticalBox>(); Box->AddChildToVerticalBox(DetailsBox);
    Statistics = MakeText(TEXT(""), Muted); Statistics->SetAutoWrapText(true); Box->AddChildToVerticalBox(Statistics)->SetPadding(FMargin(0,7,0,0));
    TrendArea = WidgetTree->ConstructWidget<USizeBox>(); TrendArea->SetHeightOverride(144.f); Box->AddChildToVerticalBox(TrendArea);
    auto* Footer = MakeText(TEXT("AirCombat.Flight.Page overview | response | power | gear | aero | sim"), Muted);
    Footer->SetAutoWrapText(true); Box->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,6,0,0));
    UpdateText();
}
void UFighterFlightDebugWidget::StartMonitoring()
{
    if (bMonitoring || !GetWorld()) return;
    bMonitoring = true;
    LastRefreshWallSeconds = 0.; LastRefreshFrame = GFrameCounter;
    RefreshNow();
    GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UFighterFlightDebugWidget::RefreshNow, .1f, true);
}
void UFighterFlightDebugWidget::StopMonitoring()
{
    bMonitoring = false;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
    if (auto* Source = BoundSource.Get()) Source->OnAfterSimulationStep.Remove(StepHandle);
    StepHandle.Reset(); BoundSource.Reset(); History.Reset(); GraphSamples.Reset();
    Snapshot = {}; Schema.Reset();
    UpdateText();
}
void UFighterFlightDebugWidget::NativeDestruct()
{
    StopMonitoring(); Super::NativeDestruct();
}
void UFighterFlightDebugWidget::BindSource()
{
    const auto* PC = GetOwningPlayer();
    const auto* Fighter = PC ? Cast<AFighter>(PC->GetPawn()) : nullptr;
    auto* Source = Fighter ? Fighter->GetFlightDynamics() : nullptr;
    const uint32 Generation = Source ? Source->GetModelGeneration() : 0;
    if (Source != BoundSource.Get() || Generation != BoundGeneration)
    {
        if (auto* Old = BoundSource.Get()) Old->OnAfterSimulationStep.Remove(StepHandle);
        StepHandle.Reset(); BoundSource = Source; BoundGeneration = Generation;
        History.Reset(); GraphSamples.Reset(); PhysicsSamples = 0;
        if (Source && bMonitoring) StepHandle = Source->OnAfterSimulationStep.AddUObject(this, &UFighterFlightDebugWidget::SamplePhysicsStep);
    }
}
void UFighterFlightDebugWidget::SamplePhysicsStep()
{
    if (!bMonitoring || !IsVisible()) return;
    const auto* PC = GetOwningPlayer(); const auto* Fighter = PC ? Cast<AFighter>(PC->GetPawn()) : nullptr;
    auto* Source = BoundSource.Get();
    if (!Source || !Fighter || Fighter->GetFlightDynamics() != Source || !Source->IsSimulationReady()) return;
    if (BoundGeneration != Source->GetModelGeneration())
    {
        BoundGeneration = Source->GetModelGeneration(); History.Reset(); PhysicsSamples = 0;
    }
    FFighterFlightDebugHistory::FSample Sample;
    if (!Source->ReadPropertyValue(TEXT("simulation/sim-time-sec"), Sample.Time)) return;
    if (Source->ReadPropertyValue(TEXT("aero/alpha-deg"), Sample.Alpha)) Sample.Valid |= 1;
    if (Source->ReadPropertyValue(TEXT("accelerations/Nz"), Sample.Nz)) Sample.Valid |= 2;
    if (Source->ReadPropertyValue(TEXT("velocities/q-rad_sec"), Sample.Q)) { Sample.Valid |= 4; Sample.Q *= 180. / PI; }
    History.Add(Sample); ++PhysicsSamples;
}
void UFighterFlightDebugWidget::RefreshNow()
{
    if (!bMonitoring || !IsVisible()) return;
    BindSource();
    const auto* PC = GetOwningPlayer();
    Snapshot = FFighterFlightDebugSnapshotBuilder::Build(PC ? Cast<AFighter>(PC->GetPawn()) : nullptr, Page);
    const double Wall = FPlatformTime::Seconds();
    for (auto& M : Snapshot.Details) if (M.Id == TEXT("EngineFPS"))
    {
        M.bAvailable = LastRefreshWallSeconds > 0. && Wall > LastRefreshWallSeconds;
        M.Value = M.bAvailable ? (GFrameCounter - LastRefreshFrame) / (Wall - LastRefreshWallSeconds) : 0.;
    }
    LastRefreshWallSeconds = Wall; LastRefreshFrame = GFrameCounter;
    if (!Snapshot.bReady) { History.Reset(); GraphSamples.Reset(); PhysicsSamples = 0; }
    else GraphSamples = History.Window(Snapshot.SimulationSeconds);
    ++RefreshCount;
    UpdateText();
}
void UFighterFlightDebugWidget::SetPage(EFighterFlightDebugPage InPage)
{
    Page = InPage; Schema.Reset();
    if (bMonitoring) RefreshNow(); else UpdateText();
}
void UFighterFlightDebugWidget::UpdateText()
{
    if (!Title || !DetailsBox) return;
    Title->SetText(FText::FromString(TEXT("FLIGHT MONITOR  ") + Snapshot.Model));
    Status->SetText(FText::FromString(Snapshot.Status));
    Status->SetColorAndOpacity(Snapshot.bCrashed ? FLinearColor(1.f,.3f,.2f) : Snapshot.bReady ? FLinearColor(.45f,1.f,.65f) : FLinearColor(1.f,.7f,.3f));
    FString Overview;
    for (int32 Index = 0; Index < Snapshot.Overview.Num(); ++Index)
    {
        const auto& M = Snapshot.Overview[Index];
        Overview += M.Label + TEXT(" ") + M.Format();
        Overview += Index % 2 ? TEXT("\n") : TEXT("    |    ");
    }
    OverviewText->SetText(FText::FromString(Overview.TrimEnd()));
    PageTitle->SetText(FText::FromString(FFighterFlightDebugSnapshotBuilder::PageName(Page)));
    FString NewSchema = FString::FromInt(static_cast<int32>(Page));
    for (const auto& M : Snapshot.Details) NewSchema += TEXT("|") + M.Id.ToString() + M.Label;
    if (Schema != NewSchema)
    {
        Schema = NewSchema; DetailsBox->ClearChildren(); Values.Reset();
        for (const auto& M : Snapshot.Details)
        {
            auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>(); DetailsBox->AddChildToVerticalBox(Row);
            auto* LabelSize = WidgetTree->ConstructWidget<USizeBox>(); LabelSize->SetWidthOverride(PanelWidth * .53f); LabelSize->SetContent(MakeText(M.Label, Muted)); Row->AddChildToHorizontalBox(LabelSize);
            auto* Value = MakeText(TEXT("N/A"), Light); Row->AddChildToHorizontalBox(Value); Values.Add(Value);
        }
    }
    for (int32 Index = 0; Index < Values.Num(); ++Index)
    {
        const auto& M = Snapshot.Details[Index]; Values[Index]->SetText(FText::FromString(M.Format()));
        Values[Index]->SetColorAndOpacity(M.bAvailable ? Light : Muted);
    }
    double MinG = BIG_NUMBER, MaxG = -BIG_NUMBER, MaxAlpha = 0., MaxQ = 0.; uint8 Valid = 0;
    for (const auto& S : GraphSamples)
    {
        if (S.Valid & 1) { MaxAlpha = FMath::Max(MaxAlpha, FMath::Abs(S.Alpha)); Valid |= 1; }
        if (S.Valid & 2) { MinG = FMath::Min(MinG, S.Nz); MaxG = FMath::Max(MaxG, S.Nz); Valid |= 2; }
        if (S.Valid & 4) { MaxQ = FMath::Max(MaxQ, FMath::Abs(S.Q)); Valid |= 4; }
    }
    const FString G = Valid & 2 ? FString::Printf(TEXT("%.2f..%.2f g"), MinG, MaxG) : TEXT("N/A");
    const FString A = Valid & 1 ? FString::Printf(TEXT("%.1f deg"), MaxAlpha) : TEXT("N/A");
    const FString Q = Valid & 4 ? FString::Printf(TEXT("%.1f deg/s"), MaxQ) : TEXT("N/A");
    Statistics->SetText(FText::FromString(FString::Printf(TEXT("Last 10 s: Nz %s | max |alpha| %s | max |q| %s\n120 Hz samples: %d  | UI refresh: 10 Hz"), *G, *A, *Q, GraphSamples.Num())));
    TrendArea->SetVisibility(Page == EFighterFlightDebugPage::Aero ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

int32 UFighterFlightDebugWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool ParentEnabled) const
{
    Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, ParentEnabled);
    if (Page != EFighterFlightDebugPage::Aero || !TrendArea || GraphSamples.Num() < 2) return Layer;
    const FGeometry Area = TrendArea->GetCachedGeometry();
    const float Width = Area.GetLocalSize().X;
    if (Width < 20.f) return Layer;
    for (int32 Series = 0; Series < 3; ++Series)
    {
        const uint8 Mask = 1 << Series; double Min = BIG_NUMBER, Max = -BIG_NUMBER;
        for (const auto& S : GraphSamples) if (S.Valid & Mask)
        {
            const double V = Series == 0 ? S.Alpha : Series == 1 ? S.Nz : S.Q;
            Min = FMath::Min(Min, V); Max = FMath::Max(Max, V);
        }
        if (Min > Max) continue;
        const double Margin = FMath::Max(.1, (Max - Min) * .1); Min -= Margin; Max += Margin;
        TArray<FVector2f> Points;
        for (const auto& S : GraphSamples) if (S.Valid & Mask)
        {
            const double V = Series == 0 ? S.Alpha : Series == 1 ? S.Nz : S.Q;
            Points.Add(FVector2f(Width * FMath::Clamp((S.Time - Snapshot.SimulationSeconds + 10.) / 10., 0., 1.),
                Series * 48.f + 15.f + 29.f * (1. - (V - Min) / (Max - Min))));
        }
        const FLinearColor Color = Series == 0 ? Cyan : Series == 1 ? FLinearColor(.55f,1.f,.5f) : FLinearColor(1.f,.75f,.3f);
        FSlateDrawElement::MakeLines(Elements, ++Layer, Area.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 1.f);
        const FString Label = FString::Printf(TEXT("%s  %.2f..%.2f %s"), Series == 0 ? TEXT("alpha") : Series == 1 ? TEXT("Nz") : TEXT("q"), Min, Max, Series == 1 ? TEXT("g") : Series == 0 ? TEXT("deg") : TEXT("deg/s"));
        FSlateDrawElement::MakeText(Elements, ++Layer, Area.ToPaintGeometry(FVector2D(Width,15), FSlateLayoutTransform(FVector2D(0.f, Series * 48.f))), Label, FSlateFontInfo(Font.Get(), FontSize - 1.f, TEXT("Regular")), ESlateDrawEffect::None, Color);
    }
    return Layer;
}
