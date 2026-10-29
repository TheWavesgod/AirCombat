# JSBSim 初始化与飞控修复（2026-10-26）

飞行状态、实际舵面、发动机、起落架和固定步进的开发期监测，见 [飞行监测 UMG](../FlightDebug/README.md)。它与输入调试和 JSBSim 旧绿色输出分别控制。

## 地面配平失败

`test_fighter` 的 F-16 实测：原实现地面配平失败；原插件却始终打印 `Trim Complete`。
问题来自地面桥接回调，而非增加配平迭代次数可以解决。

JSBSim 构造初始条件对象（包括配平器内部副本）时，会查询临时经纬度，例如本关卡碰撞覆盖范围之外的 `(0, 0)`。
旧 `GetAGLevel()` 在未命中时返回离地高度 0、接触点 ECEF `(0,0,0)`，把地心当成地面，破坏后续初始化与求解。
同条件下改用 JSBSim 默认地面回调可成功配平；保留 UE 真实碰撞、只为未命中查询提供有效 WGS84 椭球回退，同样成功。

当前处理：

- 命中真实碰撞时，继续使用 UE 接触点和法线。
- 未命中时，桥接层调用 JSBSim 默认椭球地面实现，返回有效接触点、法线和高度。
- 实际地面出生点必须命中 UE 碰撞，否则明确中止配平。椭球回退不会让缺少地面的出生点假装成功。
- 射线长度从当前查询点高度计算，并统一米 / 厘米单位；删除将 Visibility 追踪通道当成对象类型的查询。
- `Trimmed` 暴露为实例只读状态；仅成功后继续仿真。失败不再打印成功日志，也不提交无效仿真状态。
- 成功后回传三轴配平、操纵指令和所有发动机油门。修正初始方向舵 / 偏航配平符号，以及空中起落架收起命令遗漏。
- 地面模式明确使用零初速并放下起落架；缺少 GeoReferencing 时停止初始化，避免继续解引用空指针。

## 同帧控制更新

```text
PlayerController（Enhanced Input、虚拟杆、命令）
  → JSBSimMovementComponent
      OnBeforeSimulation → Fighter::PrepareFlightControls → CommandProcessor.Update
      → CopyToJSBSim → 固定 120Hz 仿真步 → CopyFromJSBSim
  → Fighter（动态重心参考标记）
  → Presentation（统一显示插值）→ Airframe / AnimInstance（实际动画状态快照）
  → CameraController → SpringArm → UE 相机更新
```

FDM 显式依赖当前 Controller，换控制器时移除旧依赖。
Pawn 保持在 FDM 之后读本帧结果；控制生成移到插件原生回调，避免“Pawn 写完命令，下帧仿真才读到”的延迟。
固定步累积不足一个 1/120 秒时不推进物理。该次飞控修复没有处理显示插值；后续高速追尾修复已接入双精度时钟与共享显示插值，见 [Presentation](../Presentation/README.md)。

## 减速板与方向舵

`FFighterControlCommand.SpeedBrake` 表示 0..1 目标，不再作为只增不减的速率输入。
当前 IMC 绑定为 `C`：按住展开，松开收回；由 `SpeedBrakeRate` 双向限速，默认完整收放各需 2 秒。
模型自己的减速板行程 / 起落架联动限制继续生效。轮刹也绑定 `C`，本次保留现有映射。

F-16 XML 的 `Yaw` 通道已有机体偏航率、横向载荷和 PID 反馈。
UE 只提交手动方向舵和配平，移除 `AutoRudderStrength/RollGain/SideslipGain`、Euler 滚转率反馈，以及基于世界地速投影的 `ComputeSideslipDeg()`。
这些旧蓝图参数已经失去作用，直接删除；四个相关蓝图重新编译通过，无需替代参数迁移。
其他机型如需自动协调，应在其 JSBSim 模型中明确设计，避免给全部飞机强加同一套反馈。

## 清理与验证

清理了无效的配平占位变量、空的 `CopyGearPropertiesToJSBSim()`、重复 Pawn → Controller 依赖、每帧 `DumpState()` 和不用的历史位置变量。
原生回归覆盖未命中地面时的有效接触点、减速板按住 / 松开、配平交接 / 复位、模型独占方向舵反馈。

真实 PIE 使用当前 IMC，分别验证地面起始和 400 kt 空中起始：

- 两次初始配平成功。
- 空中初始油门约 0.3101、起落架收起命令为 0，均保留到玩家控制接管。
- 减速板命令完整达到 1，松开后命令与实际偏角都回到 0。
- 每个采样帧比较 UE 命令与 JSBSim 已接收值，最大误差小于 5e-11；确认本帧提交。
- 手动方向舵有效，回中后 UE 不再叠加自动舵量；测试期间未坠毁。
- `BP_F16A`、`BP_J16CE`、`BP_AirCombatGameMode`、`BP_AirCombatPlayerController` 编译均为 `BS_UP_TO_DATE`。

简要机器记录见 [pie_regression.json](pie_regression.json)。编译和完整自动化结果见 [validation.json](validation.json)。
临时 PIE 飞机和关卡调整均未保存。新增测试源码和生产代码保留，诊断开关 / 逐次地面查询日志已移除。

最终验证：`AirCombatEditor Win64 Development` 编译成功；15 项自动化全部通过，0 失败、0 警告、0 跳过。


## 指令处理器职责与配平策略（2026-10-27）

`FFighterCommandProcessor` 保存飞行员设置，将 `FFighterControlCommand` 映射到 FDM 命令，并对油门、手动配平、襟翼与减速板施加变化率限制。
JSBSim 提供飞控系统组件，具体功能由机型 XML 定义。命令处理器不计算机体反馈控制律；F-16 的反馈和限制逻辑在本项目 `Resources/JSBSim/aircraft/f16/f16.xml` 内。

| 机制 | 实现与作用 | 当前设计结论 |
|---|---|---|
| 初始化配平 | 插件 `DoTrim()` 调用 JSBSim 求解地面平衡或给定空中初始状态；成功结果交给命令处理器接管 | 保留自动初始化流程；无需玩家开关 |
| 持续飞控反馈 | F-16 模型中的滚转率、俯仰率 / 载荷与偏航反馈、迎角限制等 | 由机型模型管理，不能统称为一个可开关的自动配平功能 |
| 手动配平命令 | 玩家通过现有输入增减持续偏置，处理器保留它并写入 FDM | 保留为可选操纵能力；F-16 常规飞行可以不使用；具体支持轴取决于机型 |

本次不新增 `bAutoTrim` 一类布尔开关。当前 XML 没有独立自动配平模式；`fcs/fbw-override` 只旁路部分滚转 / 俯仰飞控输出，不能作为“关闭自动配平”的替代。
以后若机型提供明确的可切换自动配平功能，再为该机型定义模式与切换规则。
若需要限制玩家使用手动配平，应另设“接受手动配平输入”的机型能力，不应关闭整个 FBW；该能力开关本次未实现。

当前 F-16 的 `fcs/pitch-trim-cmd-norm` 进入俯仰指令求和，`fcs/yaw-trim-cmd-norm` 进入方向舵调度；滚转通道未引用 `fcs/roll-trim-cmd-norm`。
因此 UE 的三轴配平命令接口并不意味着每架飞机都会响应全部三个轴。保留通用接口，本次不改 XML 或键位。
`ResetTrim()` 当前将三轴配平命令清零；它不重新执行 `DoTrim()`，也不恢复最初的配平基准。

已将原类、头文件 / 源文件、Pawn 成员、测试文件与所有当前文档引用统一为 `FighterCommandProcessor` / `FFighterCommandProcessor` / `CommandProcessor`。
这是原生 C++ 非反射类型和非 UPROPERTY 成员的重命名，不需要 Blueprint Core Redirect 或蓝图参数迁移；控制算法、默认参数、输入资产和相机行为保持原样。
本次构建与回归记录见 [command_processor_validation.json](command_processor_validation.json)；上面的 2026-10-26 记录保留为前次功能修复证据。

重命名后验证：Editor Development 编译成功；15 项自动化通过，0 失败 / 警告 / 跳过；四个相关蓝图编译正常。未重复执行前次 PIE 飞行试验，本轮通过代码等价检查确认处理器行为保持不变。

动画数据现由公共 AnimInstance 自行采集，悬挂导出与机型图配置见 [Animation](../Animation/README.md)。


## 固定视觉参考系

模型、座舱和追尾焦点直接使用 PresentationRoot 的显示 Mesh 空间，动态 CG 仅作为参考标记。
插件从 CG 世界位置反算 Actor 位置的流程保留；F16 XML 和起落架数据不变，StructuralFrameOrigin 已等价迁移为 (-550.521082,0,182.883548) cm，详见 VisualAlignment。
对齐结果、相机位置及仍存在的起落架几何差异见 [F16 视觉对齐](../VisualAlignment/README.md)。


## 固定时钟与显示状态

最新实现显式使用 PrePhysics、双精度 120 Hz 累积和 32 子步追赶预算。每个物理子步保存 ECEF 位姿，
权威 Actor 保持最新状态，显示组件仅插值整机表现；没有修改 F16 物理模型。详情见 [Presentation](../Presentation/README.md)。


## 专用试飞关卡

F16 地面与空中出生、参考地形跑道、碰撞和独立材质、测试结果见 [FlightTest](../FlightTest/README.md)。


## 自制游戏飞行模型的目标

目标是在没有现成 JSBSim XML 时，建立有资料依据、能体现机型特点、稳定可玩的游戏模型，并支持后续更多机型。
不以工程级真机复刻为验收要求；这也不意味着放弃资料依据或物理一致性。

- 可核对的公开几何、质量、发动机与性能资料尽量采用，并记录来源、单位、构型和适用条件。
- Mesh 实测用于可见机构的位置与尺寸，不用于反推 CG、惯性或未知气动系数。
- 无资料的系数、曲线和飞控参数作为有标记的估计，通过配平、操稳与构型测试逐步校准。公开性能指标不能自动确定完整气动曲线。
- 机型布局特征应落实到气动与飞控，再由状态驱动动画；J10CE 的鸭式布局不只是骨骼外观，但具体驾驶特性仍须资料或测试支撑。
- 游戏操纵与平衡调整单独标记，避免将调校值误记为真实数据。

当前 J10CE 继续使用假定空机 CG=(0,0,0) 的固定结构基准；它不代表已实测真机 CG。
载荷和燃油使当前总 CG 改变，Body 坐标原点随当前 CG，StructuralFrameOrigin 保持固定。
[官方坐标说明](https://jsbsim-team.github.io/jsbsim-reference-manual/user/concepts/frames-of-reference/)明确允许任意结构原点。
现成 F16 等模型尊重原始基准，不强制统一为空机 CG 原点。

本轮只落实 [J10CE 接地点几何标定](../VisualAlignment/J10CE_Geometry/ContactCalibration/README.md)，
没有顺带调整气动、飞控、质量或动力参数。后续调校从有资料来源的目标指标与测试工况开始。


## 遥测生命周期修复（2026-10-28）

起落架收起时支柱力异常已在 UE 适配层修复，并清理了重载、发动机状态和属性查询的残留。
接触力现在是 Actor 轴系牛顿向量；采集不会再执行额外接触计算，真实离地轮速继续保留。
没有修改 XML 或气动/飞控参数。契约、31 项自动化、8 组 PIE 及 J10CE 的独立配平限制见 [Telemetry](Telemetry/README.md)。
