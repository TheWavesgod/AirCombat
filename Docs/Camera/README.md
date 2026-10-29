# 飞机相机（UE 5.8.3）

更新：2026-10-28。

## 职责与结构

`AFighter` 装配原生组件；`UFighterCameraControllerComponent` 管理观察输入、模式、限制和自动回正。
UE 默认 PlayerCameraManager 从活动的 CameraComponent 取得最终视图。
视觉模型与相机都使用固定机体参考系，动态重心仅作为参考标记。

```text
AircraftRoot                         JSBSim 最新物理 Actor 位姿，原点等于 Mesh 原点
├─ PresentationRoot                  共享插值显示位姿，无静态标定
│  ├─ Airframe                       单位变换，模型轴向和尺寸在导入时统一
│  ├─ CockpitAnchor → CockpitCamera   Mesh 空间驾驶员视点
│  └─ ChaseFocus → ChaseArm → ChaseCamera
└─ AircraftCG                        动态 CG 标记，不作为模型或相机的父节点
```

`ChaseCamera`、`CockpitCamera`、`ChaseArm` 原生对象名称保留，延续蓝图中的镜头和 SpringArm 配置。
模型平移标定只修改 FlightDynamics.StructuralFrameOrigin，模型轴向和尺寸在导入时统一。

## 位置在哪里调

| 参数 | 编辑位置 | F16 当前值 |
|---|---|---|
| 结构原点对齐 | FlightDynamics.StructuralFrameOrigin | `(-550.521082,0,182.883548)` cm |
| 驾驶员眼睛位置 / 基础方向 | CockpitAnchor，相对 PresentationRoot | `(320,0,260)` cm / 零旋转 |
| 追尾观察焦点 | ChaseFocus，相对 PresentationRoot | `(-200,0,190)` cm |
| 追尾距离 | ChaseArm → TargetArmLength | 2500 cm |
| 追尾高度偏移 | ChaseArm → SocketOffset | `(0,0,250)` cm |
| 位置延迟 | ChaseArm → CameraLagSpeed / EnableCameraLag | 保留 8 / **false** |
| 旋转延迟 / 碰撞探针 / 最大延迟距离 | ChaseArm 对应原生参数 | 保留迁移前值 |
| 模式 / 灵敏度 / 观察限制 / 回正 | CameraController | 保留迁移前值 |
| FOV / 宽高比 / 后处理 | 各自 CameraComponent | 保留迁移前值 |

座舱视点随视觉模型刚性运动，不再每帧从 JSBSim EYEPOINT 覆盖。
物理 `EPLocalPosition` 仍可查询，视觉驾驶员视点按实际座舱标定，两者不强制重合。
追尾焦点也不随动态 CG 在机体内滑动；焦点变化与镜头距离、高度是独立参数。

## 两种模式

- Chase：方向是插值显示航向 + 环视偏航，俯仰独立限幅、滚转为零，继续保持地平线。
- Cockpit：继承 CockpitAnchor 的基础姿态和飞机完整姿态，扭头角在其局部空间叠加。
- 两种模式分别保留观察角；初始化或切换模式时只有一个相机活动。
- 追尾偏航在停止鼠标输入一段时间后按恒定角速度回正，俯仰保持。

SpringArm 负责镜头距离、偏移与碰撞回缩，当前关闭追尾平移 Lag。场景物体需阻挡 ECC_Camera 才会影响回缩。

## 输入与帧序

```text
Enhanced Input → PlayerController
  ├─ 飞行操作 → FFighterControlCommand → JSBSim 仿真前回调
  └─ 鼠标观察 / 相机切换 → CameraController
FDM → Fighter（仅 CG 参考标记）→ Presentation → Airframe / AnimInstance
    → CameraController → SpringArm → UE 相机更新
```

观察增量是像素 / 帧，乘灵敏度得到角度，不乘 DeltaTime；每次相机 Tick 消费一次。
失去控制权、重置输入或切换模式时清空待处理增量。
FDM、Pawn 和 Presentation 在 PrePhysics 按依赖更新；CameraController 在 PostPhysics 并依赖 Presentation，SpringArm 依赖 CameraController；专用服务器关闭显示和相机 Tick。
详见 [输入帧序](../Input/README.md#5-输入动力学和相机帧序)。

## 迁移与验证

此前拆分相机行为时，旧 Pawn 上的距离 / 高度 / 延迟参数已迁入 SpringArm，观察参数迁入 CameraController。
当前进一步统一结构原点映射，保留已有 FOV、宽高比、后处理、观察参数和 SpringArm 全部配置。
原 AircraftEyePoint、MeshOffset 及 VisualRoot 已删除；CockpitAnchor / ChaseFocus 直接使用 Mesh 坐标，Airframe 保持单位变换。
迁移范围为两份 Pawn 蓝图；本次完整编辑器检查确认两个测试关卡均无预放置 Fighter。

原有 `AirCombat.Camera.*` 四项测试继续覆盖输入消费与模式、姿态、回正和 SpringArm 碰撞。
新增视觉测试验证动态 CG 不会移动驾驶员视点与追尾焦点；保存后校验及 F16 PIE 结果见
[最新显示插值验证](../Presentation/validation.json)。
完整坐标说明、F16 基准和保留的轴距差异见 [F16 视觉对齐](../VisualAlignment/README.md)。


## 高速追尾抖动修复

追尾平移 Lag 已关闭，统一整机显示插值位于 PresentationRoot。旧距离、焦点、座舱位置和镜头参数保留；
追尾位置与航向使用同一显示时间。详细时钟、过载策略和不同帧率结果见 [统一显示插值](../Presentation/README.md)。


## 2026-10-28 结构原点统一

VisualRoot 已删除，Airframe 使用单位变换，模型轴向和尺寸在导入阶段处理。
F16 / J10CE 的结构原点、锚点与出生点已等价迁移。当前值与验证见 [VisualAlignment](../VisualAlignment/README.md)。
此前的验证 JSON 记录执行时的旧坐标约定，本轮验证见 [StructuralOrigin](../VisualAlignment/StructuralOrigin/validation.json)。
