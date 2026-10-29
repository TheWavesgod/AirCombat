# 飞行监测 UMG（2026-10-29）

用于 F16、J10CE 飞行模型开发和测试，默认关闭。右上角显示飞行状态，与左侧输入调试面板独立；它不是正式游戏 HUD。

## 使用

在 PIE 的控制台输入：

```text
AirCombat.Flight.Debug 1
AirCombat.Flight.Debug 0
AirCombat.Flight.Debug
AirCombat.Flight.Page overview
AirCombat.Flight.Page response
AirCombat.Flight.Page power
AirCombat.Flight.Page gear
AirCombat.Flight.Page aero
AirCombat.Flight.Page sim
AirCombat.Flight.Dump
```

`Debug` 无参数时切换显示；`Page` 选择页面并显示面板。`Dump` 输出当前页面快照到日志；隐藏时只采集一次，不创建 UMG。输入面板仍使用 `AirCombat.Input.Debug`。

面板不接收键盘焦点和鼠标命中，不切换输入模式。打开后仍可直接操纵飞机。页面通过控制台或 PlayerController 的 `SetFlightDebugPage` 切换，避免为开发面板增加玩家输入动作。面板会按视口高度缩小，适配较小的 PIE 窗口。

## 数据

每页顶部固定显示 CAS、TAS、地速、Mach、海拔/离地高度、垂直速度、CG 法向载荷 Nz、迎角/侧滑角、俯仰/滚转/真航向、总燃油、首台发动机油门和总推力。

| 页面 | 详细数据 |
| --- | --- |
| overview | 机体系角速度 p/q/r |
| response | p/q/r、提交给 JSBSim 的三轴指令与配平、当前机型绑定的实际舵面偏角或归一化位置 |
| power | 各发动机运行/加力、油门、N1/N2、推力、喷口、燃油流量；各油箱燃油质量与余量 |
| gear | 按 JSBSim 名称列出轮式/结构接触点、收放位置、WOW、悬挂压缩量/速度、带符号支柱力、接触力大小、轮速、转向角 |
| aero | 动压、当前总质量、动态 CG 的 Actor/Mesh 坐标、气动力和力矩、NED 风速；最近 10 秒迎角/Nz/q 曲线 |
| sim | Pawn、模型代次、实际墙钟平均帧率、游戏 Delta 帧率、120 Hz 固定频率、最近一帧子步数、显示插值系数、丢弃时间、仿真时间、旧绿色输出开关状态 |

数值单位明确显示：速度 kt，高度 m，垂直速度/风速 m/s，角度 deg，角速度 deg/s，载荷 g，燃油/质量 kg，推力/接触力 kN，动压 kPa，力矩 kNm，悬挂 cm 和 cm/s。缺失、不支持或仿真尚未就绪的数据显示 `N/A`，与有效零值区分。

注意数据含义：

- 速度和姿态来自 JSBSim 物理状态，不从平滑 Mesh 的位置差分估算。
- p/q/r 直接读取 JSBSim 机体角速度，不能用插件的 EulerRates 代替。
- Nz 是 JSBSim `accelerations/Nz` 的 CG 法向载荷，不假定座舱眼点已经标定。
- 舵面由现有 `UFighterAnimationBindings` 按机型读取实际属性；不复制动画同步、不硬编码 F16/J10CE 特有通道。归一化通道显示百分比，角度通道显示度。
- 支柱力沿用插件的有符号遥测，压缩时通常为负；接触力显示 Actor 坐标向量的模长。结构接触点只列状态和接触力，不重复排列不存在的轮式悬挂参数。
- Aero 页的力和力矩明确保留 **JSBSim body axes**（X 前、Y 右、Z 下），不是 UE Actor 力/力矩分量。CG 单独标注为 Actor/Mesh cm。
- `Engine FPS` 使用实际墙钟时间平均值；`Game delta rate` 使用游戏 Delta。固定 Delta 测试时二者可以不同。

## 结构与生命周期

```text
JSBSim 固定子步成功
  → OnAfterSimulationStep（只读通知）
      → 可见的 FlightDebugWidget：采集 alpha / Nz / q

PlayerController 持有独立 FlightDebugWidget
  → 0.1 s 游戏计时器
      → SnapshotBuilder 读取物理状态和已存在的属性
      → 值快照 → UMG 文本 / 最近 10 秒曲线
```

源码位于 `Source/AirCombat/Debug/Flight/`。`SnapshotBuilder` 集中负责来源、单位和可用性；Widget 负责展示、订阅和有界历史；控制台命令负责入口。Fighter 仅提供已有状态的只读访问，没有新增每帧调试同步职责。

UMG 文本以游戏时间 **10 Hz** 刷新；曲线在每个成功的 **120 Hz** JSBSim 固定步后采样，避免低频 UI 漏掉短暂载荷峰值。历史最多 1201 个样本，显示最近 10 秒的 α/Nz/q 和极值。自动化覆盖单子步峰值、时间回退、重复时间和非有限值。

关闭面板即解除固定步订阅、清除计时器和历史并销毁 Widget；无持续采集。切换/解除占有、模型重载、时间回退或仿真失效时清理旧历史；没有有效飞机时显示 `NO AIRCRAFT`。结束 PIE 时解除订阅，不保留跨 World 引用。

原生类即可使用，无需新增 WBP 资产。可在 `BP_AirCombatPlayerController.FlightDebugWidgetClass` 指定 `UFighterFlightDebugWidget` 的蓝图子类，通过默认属性调整宽度、字号和背景；布局由 C++ 创建。

## 本次边界

JSBSim 的旧绿色 `DrawDebugMessage()` 输出和各蓝图保存的 `DrawDebug` 值均保留。它与飞行监测面板和输入面板分别控制。本次没有修改 XML、气动/飞控参数、视觉标定、相机或已有输入映射。

J10CE 在 400 kt、起落架直接展开的空中初始条件下仍存在既有配平限制；飞行监测测试采用原有的收轮空中初始化，随后可在运行中放轮。该限制不通过本次调试 UI 修改模型来掩盖。

编译、自动化和实际 PIE 验证记录见 [validation.json](validation.json)、[runtime.json](runtime.json)。

## 验证结果

以下 34 项是飞行监测加入时的历史验收记录；后续清理了两个重复/一次性测试，当前保留 32 项，见 [测试维护说明](../Testing/README.md)。

`AirCombatEditor Win64 Development` 完整编译成功，34 项自动化全部通过，0 失败、0 测试警告、0 跳过。

F16、J10CE 分别在 30/60/144/240 的固定游戏 Delta 下完成八组空中 PIE 回归：六页数据、只读采样、独立开关、键盘操纵、换机、重载、关闭和 EndPlay 均通过；采样约 120 次/仿真秒，丢弃时间为零。这是 `t.OverrideFPS` 的游戏时间测试，不代表实测渲染达到对应 FPS。另在 `L_JSBSimFlightTest` 完成两组地面验证，面板中的压缩量和支柱力与 FDM 一致。

离屏 D3D12 PIE 截图仅检查 UMG 布局；不用于评价 3D 场景的视觉表现：

- F16：[总览与输入面板同时显示](Screenshots/f16_overview.png)、[起落架和结构接触点](Screenshots/f16_gear.png)、[气动与趋势](Screenshots/f16_aero.png)。
- J10CE：[实际舵面响应](Screenshots/j10ce_response.png)、[起落架](Screenshots/j10ce_gear.png)、[气动与趋势](Screenshots/j10ce_aero.png)。
