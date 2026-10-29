#include "Input/AirCombatPlayerController.h"
#include "Debug/Flight/FighterFlightDebugWidget.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightDebug, Log, All);
namespace
{
AAirCombatPlayerController* Controller(UWorld* World)
{
    auto* PC = World ? Cast<AAirCombatPlayerController>(World->GetFirstPlayerController()) : nullptr;
    return PC && PC->IsLocalController() ? PC : nullptr;
}
void Toggle(const TArray<FString>& Args, UWorld* World)
{
    if (auto* PC = Controller(World))
    {
        bool Visible = !PC->IsFlightDebugVisible();
        if (!Args.IsEmpty())
        {
            const FString Arg = Args[0].ToLower();
            if (Arg == TEXT("1") || Arg == TEXT("on") || Arg == TEXT("true")) Visible = true;
            else if (Arg == TEXT("0") || Arg == TEXT("off") || Arg == TEXT("false")) Visible = false;
            else { UE_LOG(LogFlightDebug, Display, TEXT("AirCombat.Flight.Debug [0|1]")); return; }
        }
        PC->SetFlightDebugVisible(Visible);
    }
}
void Page(const TArray<FString>& Args, UWorld* World)
{
    static const TMap<FString, EFighterFlightDebugPage> Pages = {
        {TEXT("overview"), EFighterFlightDebugPage::Overview}, {TEXT("response"), EFighterFlightDebugPage::Response},
        {TEXT("power"), EFighterFlightDebugPage::Power}, {TEXT("gear"), EFighterFlightDebugPage::Gear},
        {TEXT("aero"), EFighterFlightDebugPage::Aero}, {TEXT("sim"), EFighterFlightDebugPage::Simulation}};
    if (auto* PC = Controller(World))
    {
        const auto* Selected = Args.IsEmpty() ? nullptr : Pages.Find(Args[0].ToLower());
        if (!Selected) { UE_LOG(LogFlightDebug, Display, TEXT("AirCombat.Flight.Page overview|response|power|gear|aero|sim")); return; }
        PC->SetFlightDebugPage(*Selected); PC->SetFlightDebugVisible(true);
    }
}
void Dump(const TArray<FString>& Args, UWorld* World)
{
    if (auto* PC = Controller(World))
    {
        FFighterFlightDebugSnapshot Snapshot; PC->BuildFlightDebugSnapshot(Snapshot);
        UE_LOG(LogFlightDebug, Display, TEXT("[Flight] %s | %s | t=%.3f | generation=%d"), *Snapshot.Model, *Snapshot.Status, Snapshot.SimulationSeconds, Snapshot.ModelGeneration);
        for (const auto* Rows : {&Snapshot.Overview, &Snapshot.Details})
            for (const auto& M : *Rows) UE_LOG(LogFlightDebug, Display, TEXT("[Flight] %s: %s"), *M.Label, *M.Format());
    }
}
FAutoConsoleCommandWithWorldAndArgs FlightToggle(TEXT("AirCombat.Flight.Debug"), TEXT("Toggle the independent flight monitor, or set 0/1."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Toggle));
FAutoConsoleCommandWithWorldAndArgs FlightPage(TEXT("AirCombat.Flight.Page"), TEXT("Show page: overview|response|power|gear|aero|sim. Does not capture mouse or keyboard focus."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Page));
FAutoConsoleCommandWithWorldAndArgs FlightDump(TEXT("AirCombat.Flight.Dump"), TEXT("Log the visible monitor snapshot, or take one read-only snapshot while hidden."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump));
}

void AAirCombatPlayerController::SetFlightDebugVisible(bool Visible)
{
    if (!IsLocalController()) return;
    bShowFlightDebug = Visible;
    if (Visible) CreateFlightDebugWidget(); else DestroyFlightDebugWidget();
}
void AAirCombatPlayerController::SetFlightDebugPage(EFighterFlightDebugPage Page)
{
    FlightDebugPage = Page;
    if (FlightDebugWidget) FlightDebugWidget->SetPage(Page);
}
void AAirCombatPlayerController::CreateFlightDebugWidget()
{
    if (FlightDebugWidget || !IsLocalController() || !GetLocalPlayer() || !GetWorld() || !GetWorld()->GetGameViewport()) return;
    TSubclassOf<UFighterFlightDebugWidget> Class = FlightDebugWidgetClass;
    if (!Class) Class = UFighterFlightDebugWidget::StaticClass();
    FlightDebugWidget = CreateWidget<UFighterFlightDebugWidget>(this, Class);
    if (!FlightDebugWidget) return;
    FlightDebugWidget->SetPage(FlightDebugPage);
    FlightDebugWidget->AddToViewport(101);
    FlightDebugWidget->StartMonitoring();
    RestoreGameViewportKeyboardFocus();
}
void AAirCombatPlayerController::DestroyFlightDebugWidget()
{
    if (!FlightDebugWidget) return;
    FlightDebugWidget->StopMonitoring();
    FlightDebugWidget->RemoveFromParent();
    FlightDebugWidget = nullptr;
}
void AAirCombatPlayerController::BuildFlightDebugSnapshot(FFighterFlightDebugSnapshot& OutSnapshot)
{
    OutSnapshot = FlightDebugWidget && FlightDebugWidget->IsMonitoring() ? FlightDebugWidget->Snapshot
        : FFighterFlightDebugSnapshotBuilder::Build(GetFighter(), FlightDebugPage);
}
