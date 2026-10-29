# 固定步进与统一显示插值

更新：2026-10-28。针对高速 F16 追尾视角前后抽动，关闭追尾平移 Lag，并分离物理与显示位姿。

## 修复的机制

旧实现以 120 Hz 求解，却每帧直接把最新物理状态作为显示状态。144 FPS 时，每六帧约有一帧不推进物理，
高速机体表现为停顿后跳到下一物理点。追尾相机同时做位置 Lag，使相机与机体按不同节奏移动。
当前引擎的 SpringArm Lag 子步实现还有帧时间依赖，60 / 30 FPS 交替也会改变跟随距离。
审计中约 235 m/s 飞行时，144 FPS 下追踪点前后波动约 1.58 m，60 / 30 交替时约 0.65 m；
关闭平移 Lag 后这些相对位移波动降至数值误差级。原输入指令与最终相机缓存未发现不同步。

## 物理状态与显示状态

```text
AircraftRoot                         JSBSim 最新物理 Actor 位姿，原点等于 Mesh 原点
├─ PresentationRoot                  共享插值显示位姿，无静态标定
│  ├─ Airframe                       单位变换，模型轴向和尺寸在导入时统一
│  ├─ CockpitAnchor → CockpitCamera   Mesh 空间驾驶员视点
│  └─ ChaseFocus → ChaseArm → ChaseCamera
└─ AircraftCG                        动态 CG 标记，不作为模型或相机的父节点
```

`UFighterPresentationComponent` 只写 PresentationRoot，不修改 Actor、JSBSim 结构原点或相机锚点参数。
权威 Actor、AircraftState、地面查询和控制命令保持最新物理状态。整机显示与两种相机共同使用显示姿态。
追尾航向也读取 PresentationRoot，避免位置使用插值、朝向却使用最新物理 Actor。
机械动画快照继续采集最新实际执行机构状态；本次只对整机位置、姿态及其 CG 几何换算做统一插值。

## 120 Hz 时钟

`FJSBSimFixedStepClock` 使用双精度秒累积，固定步长 `h = 1 / 120`：

1. 接收本帧有效时间，保留不足一步的余量。
2. 同帧提交玩家指令，所有本帧物理子步消费这份命令。
3. 每步使用固定 h 推进，保存该子步的位姿。
4. `alpha = 剩余时间 / h`，显示端对最后两个物理子步插值。

30 FPS 通常执行 4 步，60 FPS 通常执行 2 步；144 / 240 FPS 可以出现 0 步帧，但显示姿态仍随 alpha 连续推进。
保存的是最后两个子步，不是最后两个渲染帧，所以低帧率的一帧多个物理步也能正确采样。
位置以双精度 ECEF 米保存，显示时通过当前 GeoReferencing 映射到 UE；姿态使用四元数最短路径插值，
重心局部偏移也在同一时间点插值，再得到对应的 Actor 显示位姿。

显示时间通常落后最新可用物理状态 0..一个固定步长，相对于连续显示时钟使用约 8.33 ms 的插值缓冲。
这不是额外的操纵输入平滑，也不会把显示结果反馈给 JSBSim。
高速时不要直接用本帧物理调试点与较早时刻的显示模型比较世界坐标；静态标定和同一时间的机体几何保持一致。

## 长卡顿与生命周期

每帧最多追赶 32 个物理子步，最多接收约 0.266667 s。超出的时间明确丢弃并累计记录，
不在后续帧留下无限追赶债务。持续低于该预算的帧率会造成仿真时间慢于游戏时间；这是有上限的过载策略，
不是为了掩盖普通帧率下的抖动。正常 30..240 FPS 及测试的波动帧率不丢时间。

- `SimulationStepsLastFrame`：本帧实际成功执行的物理步数。
- `SimulationInterpolationAlpha`：当前显示插值相位。
- `DroppedSimulationTimeSeconds`：本次模型生命周期累计丢弃的过载时间。

无效或非正帧时间不推进时钟。模型重载时清空时钟与历史；配平完成后用初始姿态填充两个端点，避免出生时拉回旧位置。
预览、模型不可用或仿真失效时，PresentationRoot 恢复单位变换，固定标定不会被覆盖。
ECEF 位置在读取显示姿态时才映射到 UE，避免把 UE 原点平移写进物理历史。
专用服务器停用显示组件，继续使用权威物理 Actor。

## 显式更新顺序

```text
PlayerController / Enhanced Input             PrePhysics
  → JSBSim / OnBeforeSimulation / 固定子步      PrePhysics
  → Fighter（动态 CG 参考标记）                 PrePhysics
  → Presentation（共享显示姿态）               PrePhysics
  → Airframe / AnimInstance                    PrePhysics
  → CameraController（读取显示航向）            PostPhysics
  → SpringArm                                  PostPhysics
  → UE PlayerCameraManager 最终缓存             PostPhysics 之后
```

FDM 不再依赖默认 DuringPhysics 帧组和隐式推迟。Presentation 同时依赖 Pawn 和 FDM；
Airframe 与 CameraController 显式依赖 Presentation，SpringArm 继续依赖 CameraController。
现有 Controller → FDM 依赖保留，当前输入在仿真前生效。

## 相机与蓝图参数

原生默认值和当前两份 Pawn 蓝图的 `ChaseArm.EnableCameraLag` 均设为 false。
原 CameraLagSpeed、子步设置、最大距离数值仍保留为 UE 组件配置，但平移 Lag 关闭时不参与计算。
保留追尾距离、高度、碰撞、FOV、宽高比、后处理、观察限制、灵敏度和回正参数；旋转 Lag 保持迁移前配置。
当前 Airframe 为单位变换；结构原点与出生点已做等价重表达，相机在 Mesh 内的位置及两个相机自身变换保持原值。

当前机型蓝图路径：

- `/Game/Blueprints/Pawns/Jets/F16A/BP_F16A`
- `/Game/Blueprints/Pawns/Jets/J16CE/BP_J16CE`

J10CE 仅接入公共显示层与关闭追尾平移 Lag，没有新增机型标定。
F16 的 StructuralFrameOrigin 已等价迁移为 (-550.521082,0,182.883548) cm；XML、起落架几何、质量、惯性及气动数据保持原样。

## 验证

编译、保存后参数校验、完整自动化以及不同帧率 PIE 数据见 [validation.json](validation.json) 和 [frame_rates.json](frame_rates.json)。
测试覆盖固定时钟、最后两个子步、四元数跨 ±180°、长卡顿预算、无源复位、相机显示航向，
以及固定 30 / 60 / 90 / 120 / 144 / 240 FPS、波动帧时间、按键操纵、座舱、地面与模型重载。
帧率测试使用受控游戏帧时间和变换采样，不把数字检查等同于实际 GPU 帧耗或主观画面验收。

## 源码

```text
Plugins/JSBSimFlightDynamicsModel/.../Public/JSBSimSimulationState.h
Plugins/JSBSimFlightDynamicsModel/.../JSBSimMovementComponent.h / .cpp
Source/AirCombat/Presentation/FighterPresentationComponent.h / .cpp
Source/AirCombat/Presentation/Tests/FighterPresentationTests.cpp
```

旧 simDtime / remainder / simloops 和重复的帧末位姿换算已移除，没有添加第二套飞控或第二套物理求解。


### 本次结果

完整原生编译成功；27 项自动化全部通过，0 失败、0 警告；7 个相关蓝图在新进程中编译及参数校验通过。
13 组受控帧时间 PIE 场景通过；模型重载另行确认在完整游戏 Tick 后清空显示差值并保留标定。

| 帧率 / 场景 | 物理步数 / 帧 | 结果 |
|---|---|---|
| 30 FPS | 4 | 连续显示、追尾同步；无时间丢弃 |
| 60 FPS | 2 | 连续显示、追尾同步；无时间丢弃 |
| 90 FPS | 1 / 2 | 连续显示、追尾同步；无时间丢弃 |
| 120 FPS | 1 | 连续显示、追尾同步；无时间丢弃 |
| 144 FPS | 0 / 1 | 连续显示、追尾同步；无时间丢弃 |
| 240 FPS | 0 / 1 | 连续显示、追尾同步；无时间丢弃 |
| 60 / 30 / 144 / 90 / 240 波动 | 0 / 1 / 2 / 4 | 连续显示、追尾同步；无时间丢弃 |

144 FPS 的采样包含 48 个未推进物理的帧，这些帧的物理 Actor 不动，但显示位姿每帧仍连续前进。
240 FPS 对应 240 个此类帧，同样连续。144 FPS 的短区间原始物理位置差分速度标准差约 104.85 m/s，
显示位置差分标准差约 0.00105 m/s；前者是采样节奏引起的离散变化，不代表真实飞行速度失控。
修复前约 1.58 m 的追尾前后距离波动，在本次同类受控场景下降至变换采样数值误差级。
按键滚转、俯仰、座舱、地面 60 / 144 FPS 和 0.4 s 长帧预算检查也通过。


## 2026-10-28 结构原点统一

VisualRoot 已删除，Airframe 使用单位变换，模型轴向和尺寸在导入阶段处理。
F16 / J10CE 的结构原点、锚点与出生点已等价迁移。当前值与验证见 [VisualAlignment](../VisualAlignment/README.md)。
此前的验证 JSON 记录执行时的旧坐标约定，本轮验证见 [StructuralOrigin](../VisualAlignment/StructuralOrigin/validation.json)。
