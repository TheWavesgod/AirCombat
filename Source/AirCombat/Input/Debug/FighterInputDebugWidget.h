// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Debug/FighterInputDebugTypes.h"
#include "FighterInputDebugWidget.generated.h"

class AAirCombatPlayerController;
class UFont;
class UHorizontalBox;
class UTextBlock;
class UVerticalBox;

/**
 * 飞行输入调试面板。
 *
 * ## 它做什么
 *
 * 把 Enhanced Input 那条四级数据流摊开显示：
 *
 * ```
 * RAW（IA 合并后的原始值） → STICK（键盘虚拟杆） → RESP（死区/曲线） → CMD（交给飞控层）
 * ```
 *
 * 顺带显示配平 / 襟翼 / 刹车 / 油门 / 鼠标增量、IMC 激活状态、虚拟杆速率（属性值 vs 实际值）、
 * 三条轴的手感曲线参数、飞机侧的起落架与高度。
 *
 * ## 怎么用
 *
 * 1. `BP_AirCombatPlayerController.InputDebugWidgetClass` 指定面板类
 *    （不指定就用 C++ 默认类，照样能跑）。
 * 2. PIE 里控制台敲 `AirCombat.Input.Debug 1` 显示，`AirCombat.Input.Debug 0` 隐藏，
 *    裸敲 `AirCombat.Input.Debug` 切换。
 *
 * ## 布局由 C++ 搭
 *
 * 整个 widget tree 在 `Initialize()` 里用 `WidgetTree` 建出来（CanvasPanel → Border →
 * VerticalBox → 若干 HorizontalBox 行）。**蓝图子类的设计师树会被这棵树替换掉** ——
 * 面板布局统一由 C++ 维护，蓝图只用来改类默认值里的字体 / 配色。
 */
UCLASS(Blueprintable)
class AIRCOMBAT_API UFighterInputDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFighterInputDebugWidget(const FObjectInitializer& ObjectInitializer);

	// ------------------------------------------------------------------
	// 数据源
	// ------------------------------------------------------------------

	/** 飞行输入的拥有者。面板每帧从这里拉数据。 */
	UPROPERTY(BlueprintReadWrite, Category = "Input Debug")
	TObjectPtr<AAirCombatPlayerController> SourceController;

	// ------------------------------------------------------------------
	// 样式（蓝图子类改这里的类默认值即可换皮）
	// ------------------------------------------------------------------

	/** 面板字体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FSlateFontInfo DebugFont;

	/**
	 * 兜底字体。
	 *
	 * `FSlateFontInfo` 默认既没有 `FontObject` 也没有 `CompositeFont`，
	 * 这种情况下 `FSlateFontInfo::GetCompositeFont()` 会返回 Slate 的
	 * **LastResort** 字体 —— 所有字符都画成"方框 + 三角"，一个字母也读不出来。
	 * 所以必须显式塞一个 `UFont`（`UFont` 实现了 `IFontProviderInterface`）。
	 *
	 * 这个成员就是那个 `UFont`，蓝图子类把 `DebugFont` 改空时用它兜底。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UFont> DefaultFont;

	/** 标题颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FLinearColor TitleColor = FLinearColor(0.30f, 0.80f, 1.0f);

	/** 表头颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FLinearColor HeaderColor = FLinearColor(0.55f, 0.72f, 0.90f);

	/** 标签颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FLinearColor LabelColor = FLinearColor(0.72f, 0.76f, 0.82f);

	/** 数值颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FLinearColor ValueColor = FLinearColor::White;

	/** 面板底色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FLinearColor BackgroundColor = FLinearColor(0.02f, 0.03f, 0.05f, 0.72f);

	/** 面板左上角偏移（屏幕像素） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style")
	FVector2D PanelOffset = FVector2D(16.0f, 16.0f);

	/** 面板整体缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Debug|Style", meta = (ClampMin = "0.2", ClampMax = "3.0"))
	float PanelScale = 1.0f;

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// ------------------------------------------------------------------
	// 构建
	// ------------------------------------------------------------------

	/** 在 WidgetTree 上搭出整块面板 */
	void BuildWidgetTree();

	/** 造一个 UTextBlock */
	UTextBlock* MakeText(const FText& InText, const FLinearColor& Color);

	/** 在纵向盒子里加一个空行，返回它方便继续加格子 */
	UHorizontalBox* MakeRow(UVerticalBox* Parent);

	/** 往某行里塞一个定宽格子 */
	void AddCell(UHorizontalBox* Row, UTextBlock* Cell, float Width, float LeftPadding = 0.0f);

	/** 记下一个数值格子的句柄（刷新时按 key 取） */
	void RegisterValue(FName Key, UTextBlock* Cell);

	// ------------------------------------------------------------------
	// 刷新
	// ------------------------------------------------------------------

	/** 从 SourceController 拉一帧数据 */
	void RefreshSnapshot();

	/** 把快照写进各个文本格子 */
	void RefreshText();

	/** 按 key 更新一个数值格子 */
	void SetValue(FName Key, const FString& Value);

	/** 按 key 更新一个数值格子（浮点，统一 ±x.xx 格式） */
	void SetValueFloat(FName Key, float Value);

	/** 当前拿到键盘输入的控件名（没有就是 "none"） */
	FString DescribeKeyboardFocus() const;

	// ------------------------------------------------------------------
	// 状态
	// ------------------------------------------------------------------

	/** 最近一帧的快照 */
	FFighterInputDebugSnapshot Snapshot;

	/** 数值格子句柄表：key → 文本控件 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTextBlock>> ValueCells;
};
