// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Debug/FighterInputDebugWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/AirCombatPlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SWidget.h"

// ----------------------------------------------------------------------
// 布局常量
// ----------------------------------------------------------------------

namespace FighterInputDebugLayout
{
	/** 操纵轴表格：第一列（轴名）宽度 */
	constexpr float AxisLabelWidth = 58.f;

	/** 操纵轴表格：数值列宽度（RAW / STICK / RESP / CMD） */
	constexpr float AxisValueWidth = 56.f;

	/** 参数行：标签列宽度 */
	constexpr float ParamLabelWidth = 124.f;

	/** 参数行：数值列宽度 */
	constexpr float ParamValueWidth = 200.f;
}

// ----------------------------------------------------------------------
// 构造
// ----------------------------------------------------------------------

UFighterInputDebugWidget::UFighterInputDebugWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ⚠ 默认构造出来的 FSlateFontInfo 里 FontObject == nullptr、CompositeFont 也无效，
	//   FSlateFontInfo::GetCompositeFont() 会直接落到 Slate 的 "LastResort" 字体 ——
	//   那个字体的每个字符都是"方框 + 三角"，面板会变成一屏豆腐块。
	//   必须显式给一个 UFont（UFont 实现了 IFontProviderInterface，能提供 composite font）。
	//
	// FontObject 是 UPROPERTY，能正常序列化 → 蓝图子类在字体下拉框里换字体照样生效。
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFontObj(*UWidget::GetDefaultFontName());
	DefaultFont = RobotoFontObj.Object;
	DebugFont = FSlateFontInfo(DefaultFont.Get(), 11.f, FName(TEXT("Regular")));
}

// ----------------------------------------------------------------------
// 生命周期
// ----------------------------------------------------------------------

bool UFighterInputDebugWidget::Initialize()
{
	// Super 先跑：它会建好 WidgetTree（原生类）或把蓝图设计师的树装进来。
	if (!Super::Initialize())
	{
		return false;
	}

	// 蓝图子类可能把 DebugFont 整个改空（FontObject 为空）→ Slate 会退回方框字体。
	// 这里兜底回默认字体。
	if (!DebugFont.FontObject && DefaultFont)
	{
		DebugFont.FontObject = DefaultFont.Get();
		DebugFont.TypefaceFontName = FName(TEXT("Regular"));
		if (DebugFont.Size <= 0.f)
		{
			DebugFont.Size = 11.f;
		}
	}

	// widget tree 必须在 AddToViewport 之前就位，所以放在 Initialize 里建，
	// 不能放 NativeOnInitialized（那里还要求 PlayerContext 有效）。
	BuildWidgetTree();

	return true;
}

void UFighterInputDebugWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshSnapshot();
	RefreshText();
}

// ----------------------------------------------------------------------
// 构建
// ----------------------------------------------------------------------

void UFighterInputDebugWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	ValueCells.Reset();

	// ------------------------------------------------------------------
	// 根：CanvasPanel 铺满视口，里面放一个自动尺寸的面板
	// ------------------------------------------------------------------
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DebugRoot"));
	WidgetTree->RootWidget = Root;

	// ⚠ 面板是"只看不碰"的：HitTestInvisible = 照常绘制，但不参与命中测试。
	//   这块 CanvasPanel 铺满视口，要是保持 Visible，鼠标点击会全被它吃掉 ——
	//   鼠标转视角直接失灵，也点不回游戏视口。面板只是"看"的，别挡操作。
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DebugPanel"));
	Panel->SetBrushColor(BackgroundColor);
	Panel->SetPadding(FMargin(9.f, 7.f));
	Panel->SetDesiredSizeScale(FVector2D(PanelScale));

	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetPosition(PanelOffset);
	PanelSlot->SetAlignment(FVector2D(0.f, 0.f));
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetZOrder(100);

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DebugBox"));
	Panel->SetContent(Box);

	// ------------------------------------------------------------------
	// 小工具
	// ------------------------------------------------------------------
	auto AddSection = [&](const TCHAR* InText)
	{
		UTextBlock* Text = MakeText(FText::FromString(InText), HeaderColor);
		Box->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 7.f, 0.f, 2.f));
	};

	// 加一行"标签 + 数值"，数值格子的句柄按 Key 存下来供刷新用。
	auto AddParam = [&](const TCHAR* InLabel, const FName Key)
	{
		UHorizontalBox* Row = MakeRow(Box);
		AddCell(Row, MakeText(FText::FromString(InLabel), LabelColor), FighterInputDebugLayout::ParamLabelWidth);

		UTextBlock* Value = MakeText(FText::FromString(TEXT("-")), ValueColor);
		AddCell(Row, Value, FighterInputDebugLayout::ParamValueWidth, 6.f);

		RegisterValue(Key, Value);
	};

	// ------------------------------------------------------------------
	// 标题
	// ------------------------------------------------------------------
	{
		FSlateFontInfo TitleFont = DebugFont;
		TitleFont.Size = DebugFont.Size + 2.f;

		UTextBlock* Title = MakeText(FText::FromString(TEXT("FLIGHT INPUT DEBUG")), TitleColor);
		Title->SetFont(TitleFont);
		Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	// ------------------------------------------------------------------
	// 操纵轴表格：AXIS | RAW | STICK | RESP | CMD
	// ------------------------------------------------------------------
	{
		UHorizontalBox* Row = MakeRow(Box);
		AddCell(Row, MakeText(FText::FromString(TEXT("AXIS")), HeaderColor), FighterInputDebugLayout::AxisLabelWidth);
		AddCell(Row, MakeText(FText::FromString(TEXT("RAW")), HeaderColor), FighterInputDebugLayout::AxisValueWidth);
		AddCell(Row, MakeText(FText::FromString(TEXT("STICK")), HeaderColor), FighterInputDebugLayout::AxisValueWidth);
		AddCell(Row, MakeText(FText::FromString(TEXT("RESP")), HeaderColor), FighterInputDebugLayout::AxisValueWidth);
		AddCell(Row, MakeText(FText::FromString(TEXT("CMD")), HeaderColor), FighterInputDebugLayout::AxisValueWidth);
	}

	const TCHAR* const AxisNames[] = { TEXT("Roll"), TEXT("Pitch"), TEXT("Yaw") };
	const TCHAR* const AxisColumns[] = { TEXT("Raw"), TEXT("Stick"), TEXT("Resp"), TEXT("Cmd") };

	for (const TCHAR* AxisName : AxisNames)
	{
		UHorizontalBox* Row = MakeRow(Box);
		AddCell(Row, MakeText(FText::FromString(AxisName), LabelColor), FighterInputDebugLayout::AxisLabelWidth);

		for (const TCHAR* Column : AxisColumns)
		{
			UTextBlock* Value = MakeText(FText::FromString(TEXT("+0.00")), ValueColor);
			AddCell(Row, Value, FighterInputDebugLayout::AxisValueWidth);

			RegisterValue(FName(*FString::Printf(TEXT("Ax.%s.%s"), AxisName, Column)), Value);
		}
	}

	// ------------------------------------------------------------------
	// 配平 / 构型 / 油门 / 视角
	// ------------------------------------------------------------------
	AddSection(TEXT("TRIM / CONFIG"));

	AddParam(TEXT("TRIM PITCH"), TEXT("Trim.Pitch"));
	AddParam(TEXT("TRIM YAW"), TEXT("Trim.Yaw"));
	AddParam(TEXT("TRIM ROLL"), TEXT("Trim.Roll"));
	AddParam(TEXT("FLAP"), TEXT("Flap"));
	AddParam(TEXT("SPEED BRAKE"), TEXT("SpeedBrake"));
	AddParam(TEXT("WHEEL BRAKE"), TEXT("Brake"));
	AddParam(TEXT("THROTTLE UP"), TEXT("Throttle.Up"));
	AddParam(TEXT("THROTTLE DOWN"), TEXT("Throttle.Down"));
	AddParam(TEXT("THROTTLE DELTA"), TEXT("Throttle.Delta"));
	AddParam(TEXT("THROTTLE ABS"), TEXT("Throttle.Abs"));
	AddParam(TEXT("MOUSE DX / DY"), TEXT("Mouse.XY"));

	// ------------------------------------------------------------------
	// 焦点
	//
	// "键盘按了没反应" 十有八九是焦点跑别的地方去了。
	// 这一行显示当前拿到键盘输入的控件，正常情况应该是 SViewport（游戏视口）。
	// ------------------------------------------------------------------
	AddParam(TEXT("KEY FOCUS"), TEXT("Focus.Widget"));

	// ------------------------------------------------------------------
	// 映射 / 曲线
	// ------------------------------------------------------------------
	AddSection(TEXT("MAPPING / CURVES"));

	AddParam(TEXT("IMC"), TEXT("IMC.Name"));
	AddParam(TEXT("IMC ACTIVE"), TEXT("IMC.Active"));
	AddParam(TEXT("MOVE RATE"), TEXT("Rate.Move"));
	AddParam(TEXT("RETURN RATE"), TEXT("Rate.Return"));
	AddParam(TEXT("INVERT PITCH"), TEXT("InvertPitch"));
	AddParam(TEXT("ROLL CURVE"), TEXT("Curve.Roll"));
	AddParam(TEXT("PITCH CURVE"), TEXT("Curve.Pitch"));
	AddParam(TEXT("YAW CURVE"), TEXT("Curve.Yaw"));

	// ------------------------------------------------------------------
	// 飞机
	// ------------------------------------------------------------------
	AddSection(TEXT("AIRCRAFT"));

	AddParam(TEXT("STATE"), TEXT("Aircraft"));
}

UTextBlock* UFighterInputDebugWidget::MakeText(const FText& InText, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(InText);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetFont(DebugFont);
	Text->SetJustification(ETextJustify::Left);
	return Text;
}

UHorizontalBox* UFighterInputDebugWidget::MakeRow(UVerticalBox* Parent)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (Parent)
	{
		Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
	}
	return Row;
}

void UFighterInputDebugWidget::AddCell(UHorizontalBox* Row, UTextBlock* Cell, float Width, float LeftPadding)
{
	if (!Row || !Cell)
	{
		return;
	}

	// 固定宽度 → 列对齐不依赖等宽字体。
	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SizeBox->SetWidthOverride(Width);
	SizeBox->AddChild(Cell);

	UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(SizeBox);
	CellSlot->SetPadding(FMargin(LeftPadding, 0.f, 0.f, 0.f));
	CellSlot->SetVerticalAlignment(VAlign_Center);
}

void UFighterInputDebugWidget::RegisterValue(FName Key, UTextBlock* Cell)
{
	if (Cell)
	{
		ValueCells.Add(Key, Cell);
	}
}

// ----------------------------------------------------------------------
// 刷新
// ----------------------------------------------------------------------

void UFighterInputDebugWidget::RefreshSnapshot()
{
	if (!SourceController)
	{
		return;
	}

	SourceController->BuildInputDebugSnapshot(Snapshot);
}

void UFighterInputDebugWidget::RefreshText()
{
	// ---- 操纵轴四级数据流 ----
	for (const FFighterInputDebugAxis& Axis : Snapshot.Axes)
	{
		const FString Prefix = FString::Printf(TEXT("Ax.%s."), *Axis.Name);

		SetValueFloat(FName(*(Prefix + TEXT("Raw"))), Axis.Raw);
		SetValueFloat(FName(*(Prefix + TEXT("Stick"))), Axis.Stick);
		SetValueFloat(FName(*(Prefix + TEXT("Resp"))), Axis.Response);
		SetValueFloat(FName(*(Prefix + TEXT("Cmd"))), Axis.Command);
	}

	// ---- 配平 / 构型 / 油门 / 视角 ----
	SetValueFloat(TEXT("Trim.Pitch"), Snapshot.TrimPitch);
	SetValueFloat(TEXT("Trim.Yaw"), Snapshot.TrimYaw);
	SetValueFloat(TEXT("Trim.Roll"), Snapshot.TrimRoll);
	SetValueFloat(TEXT("Flap"), Snapshot.Flap);
	SetValueFloat(TEXT("SpeedBrake"), Snapshot.SpeedBrake);
	SetValueFloat(TEXT("Brake"), Snapshot.Brake);
	SetValueFloat(TEXT("Throttle.Up"), Snapshot.ThrottleUp);
	SetValueFloat(TEXT("Throttle.Down"), Snapshot.ThrottleDown);
	SetValueFloat(TEXT("Throttle.Delta"), Snapshot.ThrottleDelta);
	SetValueFloat(TEXT("Throttle.Abs"), Snapshot.ThrottleAbsolute);
	SetValue(TEXT("Mouse.XY"), FString::Printf(TEXT("%+.1f / %+.1f"), Snapshot.MouseDeltaX, Snapshot.MouseDeltaY));

	// ---- Slate 焦点 ----
	// 焦点决定键盘事件送给谁。游戏在跑的时候它应该在游戏视口（SViewport）上；
	// 要是显示成别的（比如控制台的输入框），那就解释得通"按了键没反应"。
	SetValue(TEXT("Focus.Widget"), DescribeKeyboardFocus());

	// ---- 映射 / 曲线 ----
	SetValue(TEXT("IMC.Name"), Snapshot.IMCName);
	SetValue(TEXT("IMC.Active"), Snapshot.bIMCActive ? TEXT("ACTIVE") : TEXT("INACTIVE"));
	SetValue(TEXT("Rate.Move"), FString::Printf(TEXT("%.2f/s  p %.2f  e %.2f"),
		Snapshot.MoveRateEffective, Snapshot.MoveRateProperty, Snapshot.MoveRateEffective));
	SetValue(TEXT("Rate.Return"), FString::Printf(TEXT("%.2f/s  p %.2f  e %.2f"),
		Snapshot.ReturnRateEffective, Snapshot.ReturnRateProperty, Snapshot.ReturnRateEffective));
	SetValue(TEXT("InvertPitch"), Snapshot.bInvertPitch ? TEXT("true") : TEXT("false"));

	const auto FormatCurve = [](const FFighterInputDebugCurve& Curve)
	{
		return FString::Printf(TEXT("dz %.3f  exp %.2f  s %.2f"),
			Curve.DeadZone, Curve.Exponent, Curve.Sensitivity);
	};

	SetValue(TEXT("Curve.Roll"), FormatCurve(Snapshot.RollCurve));
	SetValue(TEXT("Curve.Pitch"), FormatCurve(Snapshot.PitchCurve));
	SetValue(TEXT("Curve.Yaw"), FormatCurve(Snapshot.YawCurve));

	// ---- 飞机 ----
	{
		FString AircraftText(TEXT("none"));

		if (Snapshot.bHasAircraft)
		{
			// JSBSim 的状态值在坠毁 / 仿真发散之后会变成 NaN、Inf，或者
			// -1073741824 ft 这种离谱值（实测坠毁后就是这样）。
			// 原样打出来没法看，超出合理范围就显示 n/a。
			const bool bAltitudeSane = FMath::IsFinite(Snapshot.AltitudeFt)
				&& FMath::Abs(Snapshot.AltitudeFt) < 1.0e6f;

			AircraftText = FString::Printf(TEXT("gear %s  alt %s"),
				Snapshot.bGearDown ? TEXT("DOWN") : TEXT("UP"),
				bAltitudeSane
					? *FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Snapshot.AltitudeFt))
					: TEXT("n/a"));
		}

		SetValue(TEXT("Aircraft"), AircraftText);
	}
}

FString UFighterInputDebugWidget::DescribeKeyboardFocus() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return TEXT("slate not ready");
	}

	// 优先看"用户焦点"—— 真正决定键盘事件发给谁的就是它。
	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(0);

	if (!Focused.IsValid())
	{
		// 没有用户焦点就退而求其次看键盘焦点。
		Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}

	if (!Focused.IsValid())
	{
		return TEXT("none");
	}

	return FString::Printf(TEXT("%s"), *Focused->GetTypeAsString());
}

void UFighterInputDebugWidget::SetValue(FName Key, const FString& Value)
{
	if (const TObjectPtr<UTextBlock>* Cell = ValueCells.Find(Key))
	{
		if (*Cell)
		{
			(*Cell)->SetText(FText::FromString(Value));
		}
	}
}

void UFighterInputDebugWidget::SetValueFloat(FName Key, float Value)
{
	// -0.0 会被 %+.2f 打成 "-0.00" —— 看着像"有负值但又归零"，纯误导。
	// 归一化到 +0.0（日志 dump 用的是同一个函数，两边显示一致）。
	SetValue(Key, FString::Printf(TEXT("%+.2f"), FighterInputDebugNormalizeZero(Value)));
}
