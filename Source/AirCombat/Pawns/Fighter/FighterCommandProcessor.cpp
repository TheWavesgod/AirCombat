// Fill out your copyright notice in the Description page of Project Settings.

#include "FighterCommandProcessor.h"

#include "FDMTypes.h"
#include "JSBSimMovementComponent.h"

void FFighterCommandProcessor::Update(float DeltaTime, const FFighterControlCommand& Command, UJSBSimMovementComponent* FDM)
{
	if (!FDM)
	{
		return;
	}

	// ------------------------------------------------------------------
	// 首次进入：从 FDM 同步初值
	//
	// 插件在 DoTrim() 里把配平结果写回了 Commands（Commands.PitchTrim =
	// FCS->GetPitchTrimCmd()）。如果我们从 0 开始写配平，会把配平好的飞机
	// 破坏掉（地面上直接低头/抬头）。所以第一帧以插件配平出来的值为基准。
	// ------------------------------------------------------------------
	if (!bSynced)
	{
		bSynced = true;
		PitchTrim = (float)FDM->Commands.PitchTrim;
		YawTrim   = (float)FDM->Commands.YawTrim;
		RollTrim  = (float)FDM->Commands.RollTrim;
		bGearDown = FDM->Commands.GearDown > 0.5;
		Flap      = (float)FDM->Commands.Flap;
		SpeedBrake= (float)FDM->Commands.SpeedBrake;
		if (FDM->EngineCommands.Num() > 0)
		{
			Throttle = FMath::Clamp((float)FDM->EngineCommands[0].Throttle, 0.f, 1.f);
		}
	}

	FFlightControlCommands& Cmd = FDM->Commands;

	// ------------------------------------------------------------------
	// ① 基本操纵
	//
	// F-16 的 JSBSim 模型自带 FBW：
	//   - 副翼指令 = 滚转率指令（gain 0.31821，1.0 ≈ 180°/s）
	//   - 平尾指令 = G 载指令（含 alpha limiter 28~30°、负过载限制 44%）
	// 所以 UE 侧只给"归一化杆量"，绝不自己写飞控律。
	// ------------------------------------------------------------------
	Cmd.Aileron  = FMath::Clamp((double)Command.Roll,  -1.0, 1.0);
	Cmd.Elevator = FMath::Clamp((double)Command.Pitch, -1.0, 1.0);

	// The aircraft model owns yaw damping and coordination, in body/aerodynamic axes.
	Cmd.Rudder = FMath::Clamp((double)Command.Yaw, -1.0, 1.0);

	// ------------------------------------------------------------------
	// ② 手动配平偏置（速率调整）
	//
	// 插件内部是 FCS->SetYawTrimCmd(-Commands.YawTrim)，符号在插件侧对冲，
	// 这里保持和 FDM->Commands 同号即可。
	// ------------------------------------------------------------------
	if (bResetTrimRequested)
	{
		bResetTrimRequested = false;
		PitchTrim = 0.f;
		YawTrim = 0.f;
		RollTrim = 0.f;
	}
	PitchTrim = FMath::Clamp(PitchTrim + Command.TrimPitch * TrimRate * DeltaTime, -1.f, 1.f);
	YawTrim   = FMath::Clamp(YawTrim   + Command.TrimYaw   * TrimRate * DeltaTime, -1.f, 1.f);
	RollTrim  = FMath::Clamp(RollTrim  + Command.TrimRoll  * TrimRate * DeltaTime, -1.f, 1.f);
	Cmd.PitchTrim = PitchTrim;
	Cmd.YawTrim   = YawTrim;
	Cmd.RollTrim  = RollTrim;

	// ------------------------------------------------------------------
	// ③ 油门
	//
	// F-16 的 Throttle 通道是 pure_gain(gain=2)，引擎 augmethod=2
	// （"油门范围在 FCS 里被展开，1.0 以上是加力段"）：
	//   杆量 0.0 → 0    = 怠速
	//   杆量 0.5 → 1.0  = 军用满推（mil，17800 lbf）
	//   杆量 1.0 → 2.0  = 全加力（AB，29000 lbf）
	// 天然对上玩法文档里的"加力"机制。
	//
	// 油门是**有状态**的：Command 里传的是增量意图（−1..1），
	// 绝对油门在这里按 ThrottleRate 缓慢变化并保持 —— 和原实现完全一致。
	// ------------------------------------------------------------------
	Throttle = FMath::Clamp(Throttle + Command.ThrottleDelta * ThrottleRate * DeltaTime, 0.f, 1.f);
	if (FDM->EngineCommands.Num() > 0)
	{
		FDM->EngineCommands[0].Throttle = Throttle;

		// ⚠ 绝对不要写 EngineCommands[0].Augmentation！
		//   augmethod=2 时加力段是由 FCS 展开后的油门值决定的，
		//   UE 侧强行置位会和模型自己的逻辑打架。
		//   要读加力状态请读 EngineStates[0].Augmentation（JSBSim 自己算的）。
	}

	// ------------------------------------------------------------------
	// ④ 构型
	// ------------------------------------------------------------------
	if (bGearToggleRequested)
	{
		bGearToggleRequested = false;
		bGearDown = !bGearDown;
	}
	Cmd.GearDown = bGearDown ? 1.0 : 0.0;   // 插件约定：0 = 收上，1 = 放下

	Flap = FMath::Clamp(Flap + Command.Flap * FlapRate * DeltaTime, 0.f, 1.f);
	SpeedBrake = FMath::FInterpConstantTo(SpeedBrake, FMath::Clamp(Command.SpeedBrake, 0.f, 1.f),
		DeltaTime, FMath::Max(SpeedBrakeRate, 0.f));
	Cmd.Flap = Flap;
	Cmd.SpeedBrake = SpeedBrake;

	Cmd.LeftBrake = Cmd.RightBrake = Cmd.CenterBrake = FMath::Clamp((double)Command.Brake, 0.0, 1.0);
}
