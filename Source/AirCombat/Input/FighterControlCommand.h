// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 飞行员操纵意图 —— **输入层与飞控层的数据边界**。
 *
 * 这个结构里装的是"已经被解释过的逻辑量"：
 *  - 玩家想往哪推杆（−1..1）
 *  - 玩家想动哪个配平、哪个构型
 *
 * 它**不含**任何物理键名、不含任何资产引用、不含任何 JSBSim 类型。
 * 输入层（AAirCombatPlayerController）负责把 Enhanced Input 的动作值
 * 翻译成它；AFighter / FFighterCommandProcessor 负责消费它。
 *
 * ## 关于油门
 *
 * F-16 的油门是**有状态**的：按住"加油门"时油门按 ThrottleRate 缓慢爬升，
 * 松手后停在原地。所以这里传的是**增量意图**（ThrottleDelta，−1..1），
 * 绝对油门仍然由 FFighterCommandProcessor 按速率保持 —— 这样"第一帧从 JSBSim
 * 配平结果同步油门"的行为原样保留，不会因为换了层就丢掉怠速。
 */
struct FFighterControlCommand
{
	// ---- 连续操纵量（归一化杆量） ----

	/** 俯仰杆量，−1..1。正 = 拉杆抬头（与键盘 W 一致） */
	float Pitch = 0.f;

	/** 滚转杆量，−1..1。正 = 右滚（与键盘 D 一致） */
	float Roll = 0.f;

	/** 方向舵，−1..1。正 = 右舵（与键盘 E 一致） */
	float Yaw = 0.f;

	// ---- 配平（速率调整，−1..1） ----

	float TrimPitch = 0.f;
	float TrimYaw = 0.f;
	float TrimRoll = 0.f;

	// ---- 构型 ----

	/** 襟翼。正 = 放下，负 = 收起 */
	float Flap = 0.f;

	/** 减速板目标，0..1；键盘按住 = 1，松开 = 0，由飞控按速率收放。 */
	float SpeedBrake = 0.f;

	/** 轮刹 */
	float Brake = 0.f;

	// ---- 油门意图 ----

	/** 油门增量意图，−1..1。正 = 加油门，负 = 收油门。绝对油门由飞控层保持。 */
	float ThrottleDelta = 0.f;

	// 观察输入直接交给 CameraController，不进入飞行命令。

	/** 清空（失去控制权 / 换飞机时用） */
	void Reset()
	{
		*this = FFighterControlCommand();
	}
};
