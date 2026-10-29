# 飞机动画系统（2026-10-27）

## 当前结构

```text
JSBSim 实际状态 / 只读模型属性
  → UFighterAnimInstance::NativeUpdateAnimation（游戏线程）
      FFighterAnimationStateBuilder + UFighterAnimationBindings
      → FFighterAnimationState 独立数值快照
  → 对应骨架的 AnimBP（只消费快照）
      机构片段 Sequence Evaluator → 舵面 / 转向 / 轮胎 Modify Bone
  → SkeletalMesh 骨骼姿态
```

FDM 先更新，Pawn 只更新动态重心标记，Presentation 随后写共享显示姿态，Airframe 再更新动画。Airframe 为单位变换，相机锚点固定在该显示 Mesh 空间中；平移标定统一使用 StructuralFrameOrigin。
`AFighter` 只装配组件和提供 `GetFlightDynamics()` / `GetAnimationBindings()`；旧的逐字段动画写入函数及参数容器已移除。
公共 `UFighterAnimInstance` 位于 `Source/AirCombat/Animation`，不含机型判断、骨骼名称或输入动作。

`NativeInitializeAnimation` 绑定所属 Pawn；`NativeUpdateAnimation` 在游戏线程生成快照。
模型未就绪、Owner 失效、解绑或模型重建时清空状态。FDM 对象、绑定资产或模型 generation 变化时，轮胎积分状态也重新开始。
AnimGraph 只调用标记为线程安全的快照查询函数，不访问 Actor、JSBSim 对象或控制指令。
模型属性由插件的 `ReadPropertyValue()` 只读数值接口访问；不走字符串控制台，不创建不存在的属性。
属性节点仅在游戏线程使用，缓存随 JSBSim Exec 销毁而清理，支持缺失 / 非法路径与非有限值处理。

## 公共参数

| 数据 | 主要字段与单位 |
|---|---|
| 快照 | `bValid`、`bBindingsMatchModel`、模型名、空速 / 地速（kt） |
| 舵面 / 机构数组 | 稳定 `Id`、`bAvailable`、`AngleDeg`、`bHasAngle`、`Position01` |
| 起落架数组 | `Nose` / `LeftMain` / `RightMain`，实际伸出比例、接地、实际转向角、轮速 cm/s、轮转角 deg |
| 悬挂 | 实际 `CompressionCm`、有符号压缩速度 cm/s、支柱力 N、`SuspensionAlpha` 与行程是否配置 |
| 发动机数组 | 真实发动机索引、运行 / 加力、油门指令、N1 / N2%、推力 N、喷口位置及可用性 |

缺少部件和数值为零不同，必须检查 `bAvailable`。
没有物理角度的机构（舱盖 / 尾钩）只有归一化位置，`bHasAngle=false`，不会把 0..1 伪装成角度。
公共层按机械语义组织数组，不为每个机型添加一套重复成员。发动机数量来自真实模型，不因外观名字虚构第二台发动机。
符号沿用绑定的模型通道；骨骼轴向和方向修正在 AnimBP 中配置。

## 当前机型与绑定

| Pawn 蓝图 | 实际骨架 / AnimBP | 绑定资产 / 物理模型 |
|---|---|---|
| `BP_F16A` | F16C / `ABP_F16C` | `DA_F16_AnimationBindings` / `f16` |
| `BP_J16CE` | J10CE / `ABP_J10CE` | `DA_J10CE_F16_AnimationBindings` / `f16` |

第二架目前是 J10CE 外观配 F16 动力学；本次保留这个配置，不宣称已经具备 J10/J16 专用飞控。
两份绑定源通道相同，但可分别标定轮胎半径和悬挂动画行程。
F16 轮胎半径现已实测写入；J10CE 保持未标定，悬挂动画行程仍等待资产。

绑定包含：

- 左右差动平尾：`fcs/dht-left-pos-rad` / `fcs/dht-right-pos-rad`，弧度转度。
- 左右副翼：模型左右实际副翼输出，弧度转度。
- 左右前缘襟翼：`fcs/lef-pos-deg`。
- 左右后缘襟翼：执行机构输出 `fcs/tef-control`，按模型量纲换算为度，不取玩家襟翼指令。
- 左右减速板：`fcs/speedbrake-pos-deg`，按实际偏角转换 0..1。
- 方向舵、舱盖、尾钩。
- 鸭翼通道 `CanardLeft/Right` 明确为 optional / 不可用；F16 模型没有该输出，J10CE 鸭翼目前保持默认姿态。接入专用模型时修改绑定即可。
- 起落架通过 `NOSE_LG` / `LEFT_MLG` / `RIGHT_MLG` 接触名称匹配，忽略结构接触点，不依赖数组索引。

源绑定资产不含骨骼名。AnimBP 中的骨骼名、轴向与动画片段只与对应骨架有关。

## 机型动画图

- 两个 AnimBP 的父类都是公共 `UFighterAnimInstance`，不增加每机型 C++ 子类。
- F16C 使用已有的 3 条起落架收放和 4 条减速板加法动画，并用 Modify Bone 驱动平尾、副翼、前后缘襟翼和方向舵。
- F16C 已提供真实喷口参数，但当前动画图没有专用喷口片段。
- J10CE 使用已有的起落架、减速板、舱盖、喷口片段，以分骨骼混合隔离机构；独立舵面通过 Modify Bone 驱动。
- 前轮转向和轮胎滚动由实际起落架状态驱动。两套骨架旋转轴不同，已根据参考姿态分别设置。
- 所有 Sequence Evaluator 禁用循环、使用显式时间跳转，不用播放进度作为真实机构状态，不提取 Root Motion / Notifies。
- `GetGearRetractionTime(Id, Length)` 返回 `(1 - Extension01) * Length`，适合“完全放下 → 完全收起”的片段。空中出生和中途反向直接到正确采样位置。
- 轮胎角度根据真实轮速和已配置半径积分。半径未配置时 `bHasWheelRadius=false`，不猜测轮胎尺寸。
- 悬挂数据和采样接口已接入。最终悬挂片段待制作，当前不自动添加未知骨骼位移或猜测支柱行程，详见 [悬挂资产接入](Suspension.md)。

公共快照也可供尾焰 / 材质表现读取发动机数据；本次不在 AnimBP 中增加每帧修改材质或新的特效系统。

## 源码与资产布局

```text
Source/AirCombat/Animation/
  FighterAnimInstance.h / .cpp
  FighterAnimationState.h
  FighterAnimationBindings.h
  FighterAnimationStateBuilder.h / .cpp
  Tests/FighterAnimationTests.cpp
Content/Blueprints/Pawns/Jets/F16A/ABP_F16C
Content/Blueprints/Pawns/Jets/J16CE/ABP_J10CE
Content/Data/Animation/DA_F16_AnimationBindings
Content/Data/Animation/DA_J10CE_F16_AnimationBindings
```

插件额外导出真实压缩量、压缩速度、支柱力、转向角和涡轮喷口位置；未增加 UE 物理弹簧或第二套飞行动力学。

## 验证

AirCombatEditor Win64 Development 完整原生编译成功；19 项自动化测试全部通过（0 失败、0 警告）。
两套 AnimBP 和两架 Pawn 蓝图编译通过，并在新进程中重新加载确认保存引用正确。
以上为动画系统初次重构的验证，记录见 [validation.json](validation.json)。
后续固定视觉参考系与 F16 标定的最新结果见 [视觉对齐验证](../VisualAlignment/validation.json)。
真实 PIE 对 F16C 地面起始、J10CE 外观空中起始验证：

- 实际模型通道、收放进度与悬挂厘米数在本帧同步；骨骼姿态随操纵面变化。
- 空中起落架有 0..1 中间过程，不再由起落架目标布尔值驱动。
- 地面存在真实压缩量；悬挂归一化与临时测试行程一致。
- 缺失属性、非法属性路径及不支持的鸭翼保持不可用，且只读查询不创建属性。
- 模型重载后快照清空，不保留旧舵面或轮胎状态。

简要结果见 [pie_results.json](pie_results.json)。测试中的 20 cm 行程 / 半径仅是临时运行对象的已知校准量，用于核对数据流；不代表真实机型尺寸，也没有写入正式绑定资产。
本次未处理之前暂缓的整机 / 镜头抽搐问题。


模型与物理坐标的整体对齐、当前相机锚点以及按用户要求保留的起落架几何差异，见 [F16 视觉对齐](../VisualAlignment/README.md)。


整机位姿的显示插值见 [Presentation](../Presentation/README.md)。动画快照仍读取实际模型状态，AnimGraph 不再负责任何整机跟随平滑。


## 起落架遥测契约

未触地或起落架收放时，支柱力、压缩量及压缩速度由 FDM 适配层明确归零，离地轮胎空转仍保留。
StrutForceNewtons 保留压缩为负的 JSBSim 轴向符号；Force 是 Actor 轴系中的牛顿向量，不含位置偏移。
本次没有改动画蓝图、绑定、悬挂行程或模型参数。详见 [Telemetry](../FlightDynamics/Telemetry/README.md)。
