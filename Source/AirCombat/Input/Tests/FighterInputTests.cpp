// Fill out your copyright notice in the Description page of Project Settings.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Input/FighterInputConfig.h"
#include "Input/KeyboardVirtualStick.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace FighterInputTest
{
	/** DA_FighterInputConfig 的资产路径（测试里唯一一处按路径取配置资产的地方）。 */
	const TCHAR* ConfigAssetPath = TEXT("/Game/Blueprints/Input/DA_FighterInputConfig.DA_FighterInputConfig");

	/** 期望存在的玩家可重绑 Mapping Name（与 IMC 资产里的 PMKS 一一对应）。 */
	const TCHAR* ExpectedMappingNames[] =
	{
		TEXT("Flight.RollPositive"),
		TEXT("Flight.RollNegative"),
		TEXT("Flight.PitchPositive"),
		TEXT("Flight.PitchNegative"),
		TEXT("Flight.YawPositive"),
		TEXT("Flight.YawNegative"),
		TEXT("Flight.TrimPitchPositive"),
		TEXT("Flight.TrimPitchNegative"),
		TEXT("Flight.TrimYawPositive"),
		TEXT("Flight.TrimYawNegative"),
		TEXT("Flight.TrimRollPositive"),
		TEXT("Flight.TrimRollNegative"),
		TEXT("Flight.FlapExtend"),
		TEXT("Flight.FlapRetract"),
		TEXT("Flight.ThrottleIncrease"),
		TEXT("Flight.ThrottleDecrease"),
		TEXT("Flight.SpeedBrake"),
		TEXT("Flight.WheelBrake"),
		TEXT("Aircraft.ToggleLandingGear"),
		TEXT("Aircraft.ToggleCamera"),
		TEXT("Aircraft.ResetTrim"),
	};

	UFighterInputConfig* LoadConfig()
	{
		return LoadObject<UFighterInputConfig>(nullptr, ConfigAssetPath);
	}
}

// ======================================================================
// 纯逻辑：键盘虚拟杆
// ======================================================================

/** 按住时按推出速率恒速逼近目标（不越界、不指数收敛）。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterVirtualStickPushTest,
	"AirCombat.Input.KeyboardVirtualStick.PushRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterVirtualStickPushTest::RunTest(const FString& Parameters)
{
	FKeyboardVirtualStick Stick(3.f, 6.f);

	FFighterStickPosition Target;
	Target.Roll = 1.f;
	Target.Pitch = -1.f;
	Target.Yaw = 0.5f;

	const FFighterStickPosition Result = Stick.Update(Target, 0.1f);

	TestEqual(TEXT("Roll 推杆 0.1s = 3.0 * 0.1"), Result.Roll, 0.3f);
	TestEqual(TEXT("Pitch 推杆 0.1s = -3.0 * 0.1"), Result.Pitch, -0.3f);
	// 恒速逼近（FInterpConstantTo）：速率与目标大小无关，
	// 0.5 的目标同样按 3.0/秒推，0.1 秒后到 0.3（还没到 0.5）。
	TestEqual(TEXT("Yaw 推杆 0.1s = 3.0 * 0.1"), Result.Yaw, 0.3f);

	// 继续按住 → 线性逼近，1 秒左右推满，不会超过 1
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Stick.Update(Target, 0.1f);
	}
	TestEqual(TEXT("Roll 推满后停在 1"), Stick.GetPosition().Roll, 1.f);
	TestEqual(TEXT("Pitch 推满后停在 -1"), Stick.GetPosition().Pitch, -1.f);
	TestEqual(TEXT("Yaw 推满后停在 0.5"), Stick.GetPosition().Yaw, 0.5f);

	return true;
}

/** 松键后按回中速率回零。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterVirtualStickReturnTest,
	"AirCombat.Input.KeyboardVirtualStick.ReturnToCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterVirtualStickReturnTest::RunTest(const FString& Parameters)
{
	FKeyboardVirtualStick Stick(3.f, 6.f);

	FFighterStickPosition Target;
	Target.Roll = 1.f;
	Stick.Update(Target, 1.f);
	TestEqual(TEXT("按住后满杆"), Stick.GetPosition().Roll, 1.f);

	// 松键：Target = 0 → 用回中速率 6.0
	FFighterStickPosition Released;
	Stick.Update(Released, 0.1f);
	TestEqual(TEXT("松键 0.1s 回中 6.0 * 0.1"), Stick.GetPosition().Roll, 0.4f);

	// 回到底
	Stick.Update(Released, 1.f);
	TestEqual(TEXT("松键后回中到 0"), Stick.GetPosition().Roll, 0.f);

	return true;
}

/** Reset() 清空杆位（失去控制权 / 换飞机时用）。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterVirtualStickResetTest,
	"AirCombat.Input.KeyboardVirtualStick.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterVirtualStickResetTest::RunTest(const FString& Parameters)
{
	FKeyboardVirtualStick Stick(3.f, 6.f);

	FFighterStickPosition Target;
	Target.Roll = 1.f;
	Target.Pitch = -1.f;
	Target.Yaw = 1.f;
	Stick.Update(Target, 1.f);

	Stick.Reset();

	TestEqual(TEXT("Reset 后 Roll = 0"), Stick.GetPosition().Roll, 0.f);
	TestEqual(TEXT("Reset 后 Pitch = 0"), Stick.GetPosition().Pitch, 0.f);
	TestEqual(TEXT("Reset 后 Yaw = 0"), Stick.GetPosition().Yaw, 0.f);

	return true;
}

// ======================================================================
// 资产级：DA_FighterInputConfig
// ======================================================================

/** 配置资产的每个引用都填了、值类型正确、IMC 里都有对应键位。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterInputConfigTest,
	"AirCombat.Input.Config.DA_FighterInputConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterInputConfigTest::RunTest(const FString& Parameters)
{
	UFighterInputConfig* Config = FighterInputTest::LoadConfig();
	if (!Config)
	{
		AddError(TEXT("找不到 DA_FighterInputConfig（/Game/Blueprints/Input）。"
					  "它是 BP_AirCombatPlayerController 的 InputConfig 来源。"));
		return false;
	}

	TArray<FString> Problems;
	Config->CollectProblems(Problems);

	for (const FString& Problem : Problems)
	{
		AddError(FString::Printf(TEXT("配置问题：%s"), *Problem));
	}

	TestEqual(TEXT("配置资产没有校验问题"), Problems.Num(), 0);

	return true;
}

/**
 * 每一根物理键位都必须由 IMC 资产承载：
 *  - 两个键共用一个 IA 的轴动作必须是 Cumulative（正负键同时按正好抵消成 0）
 *  - 负方向那一条必须挂 UInputModifierNegate
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterImcAxisMappingTest,
	"AirCombat.Input.Config.ImcOppositeKeyMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterImcAxisMappingTest::RunTest(const FString& Parameters)
{
	UFighterInputConfig* Config = FighterInputTest::LoadConfig();
	if (!Config)
	{
		AddError(TEXT("找不到 DA_FighterInputConfig。"));
		return false;
	}

	const UInputMappingContext* IMC = Config->FlightKeyboardContext;
	if (!IMC)
	{
		AddError(TEXT("DA_FighterInputConfig 没有指定 FlightKeyboardContext。"));
		return false;
	}

	// 两个键共用一个 IA 的轴动作
	const UInputAction* TwoKeyAxes[] =
	{
		Config->RollAction,
		Config->PitchAction,
		Config->YawAction,
		Config->TrimPitchAction,
		Config->TrimYawAction,
		Config->TrimRollAction,
		Config->FlapAction,
	};

	for (const UInputAction* Action : TwoKeyAxes)
	{
		if (!Action)
		{
			continue;
		}

		TestEqual(
			*FString::Printf(TEXT("%s 的合并方式必须是 Cumulative（正负键同时按要能抵消）"),
							  *Action->GetName()),
			Action->AccumulationBehavior,
			EInputActionAccumulationBehavior::Cumulative);

		int32 NumMappings = 0;
		int32 NumNegated = 0;

		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (Mapping.Action.Get() != Action)
			{
				continue;
			}

			++NumMappings;

			for (const UInputModifier* Modifier : Mapping.Modifiers)
			{
				if (Modifier && Modifier->IsA<UInputModifierNegate>())
				{
					++NumNegated;
				}
			}
		}

		TestEqual(
			*FString::Printf(TEXT("%s 在 IMC 里应该正好有 2 条映射（正/负两个键）"),
							  *Action->GetName()),
			NumMappings, 2);

		TestEqual(
			*FString::Printf(TEXT("%s 应该正好有 1 条映射挂 Negate 修饰器（负方向）"),
							  *Action->GetName()),
			NumNegated, 1);
	}

	return true;
}

/**
 * 玩家可重绑的映射：每个可独立重绑的物理映射都必须有**稳定且唯一**的 Mapping Name。
 * （这正是 UEnhancedInputUserSettings::RegisterInputMappingContext 用来建映射行的依据。）
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFighterPlayerMappableNamesTest,
	"AirCombat.Input.Config.PlayerMappableMappingNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFighterPlayerMappableNamesTest::RunTest(const FString& Parameters)
{
	UFighterInputConfig* Config = FighterInputTest::LoadConfig();
	if (!Config)
	{
		AddError(TEXT("找不到 DA_FighterInputConfig。"));
		return false;
	}

	const UInputMappingContext* IMC = Config->FlightKeyboardContext;
	if (!IMC)
	{
		AddError(TEXT("DA_FighterInputConfig 没有指定 FlightKeyboardContext。"));
		return false;
	}

	// 收集 IMC 里所有"玩家可重绑"的映射名
	TSet<FName> FoundNames;
	TMap<FName, int32> NameCounts;

	for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
	{
		if (!Mapping.IsPlayerMappable())
		{
			continue;
		}

		const FName MappingName = Mapping.GetMappingName();
		if (MappingName.IsNone())
		{
			AddError(FString::Printf(
				TEXT("IMC 里 '%s' 的映射标记为可重绑，但 Mapping Name 是空的。"),
				Mapping.Action ? *Mapping.Action->GetName() : TEXT("?")));
			continue;
		}

		FoundNames.Add(MappingName);
		// ⚠ UE 5.8 的 TMap::operator[] 找不到 key 时会直接断言（FindChecked），
		//    所以这里必须用 FindOrAdd 显式插入。
		++NameCounts.FindOrAdd(MappingName);
	}

	// 唯一性：同一个 Mapping Name 不能对应两条不同的物理映射
	for (const TPair<FName, int32>& Pair : NameCounts)
	{
		if (Pair.Value > 1)
		{
			AddError(FString::Printf(
				TEXT("Mapping Name '%s' 对应了 %d 条物理映射 —— 每个可独立重绑的物理映射都必须有唯一名字。"),
				*Pair.Key.ToString(), Pair.Value));
		}
	}

	// 期望的名字一个都不能少
	for (const TCHAR* Expected : FighterInputTest::ExpectedMappingNames)
	{
		TestTrue(
			*FString::Printf(TEXT("IMC 里应该有玩家可重绑的映射 '%s'"), Expected),
			FoundNames.Contains(FName(Expected)));
	}

	TestEqual(
		TEXT("玩家可重绑的映射数量"),
		FoundNames.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(FighterInputTest::ExpectedMappingNames)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
