# 飞机模型与 JSBSim 结构坐标对齐

更新：2026-10-28。当前统一使用 `StructuralFrameOrigin` 做平移标定，删除 VisualRoot 与额外 Mesh 偏移。
此前保持 StructuralFrameOrigin 不变、移动模型的约定已被本方案替代。当时 F16 / J10CE XML 均未修改；随后 J10CE 三个接地点已按测量更新，见 [接地点标定](J10CE_Geometry/ContactCalibration/README.md)。

## 当前结构

```text
AircraftRoot                         JSBSim 最新物理 Actor 位姿，原点等于 Mesh 原点
├─ PresentationRoot                  共享插值显示位姿，无静态标定
│  ├─ Airframe                       单位变换，模型轴向和尺寸在导入时统一
│  ├─ CockpitAnchor → CockpitCamera   Mesh 空间驾驶员视点
│  └─ ChaseFocus → ChaseArm → ChaseCamera
└─ AircraftCG                        动态 CG 标记，不作为模型或相机的父节点
```

Actor / Airframe 的固定局部基准都是导入 Mesh 原点。轴向 X 前、Y 右、Z 上，厘米。
模型尺寸、旋转和缩放必须在 DCC / 导入阶段处理；Airframe 在构造和初始化时保持单位变换，不提供第二套标定参数。
Actor 实例也应保持单位缩放。PresentationRoot 只保存显示插值差值，不能填写静态标定。

JSBSim XML 结构坐标为 X 后、Y 右、Z 上，当前模型位置单位为英寸：

```text
Actor_cm = Mesh_cm
Actor_cm = (-Structural.X, Structural.Y, Structural.Z) * 2.54 + StructuralFrameOrigin
Structural_in = (-(Actor.X-Origin.X), Actor.Y-Origin.Y, Actor.Z-Origin.Z) / 2.54
```

StructuralFrameOrigin 表示结构参考原点在 Actor / Mesh 空间中的厘米位置，初始化前配置。
它不改变 XML 的质量、惯性、载荷或 CG。Actor 世界位置仍由插件按 CG 世界位置减去旋转后的 CG 局部位置计算。
CG 变化不会在模型内部移动 Mesh 或相机锚点，也不能把 CG 额外叠加到模型变换。

## 当前蓝图参数

| 配置 | F16 | J10CE |
|---|---|---|
| StructuralFrameOrigin cm | (-550.521082, 0, 182.883548) | (-20, 0, -40) |
| Airframe 相对 PresentationRoot | 单位变换 | 单位变换 |
| CockpitAnchor 相对 PresentationRoot cm | (320, 0, 260) | (10, 0, 0) |
| ChaseFocus 相对 PresentationRoot cm | (-200, 0, 190) | (10, 0, 0) |
| SpringArm 距离 / SocketOffset.Z | 2500 / 250 cm | 保留原配置 |

相机锚点直接使用 Mesh 坐标；FOV、后处理、观察限制、灵敏度、回正、碰撞探针和镜头距离均保持迁移前值。
XML EYEPOINT 不会每帧覆盖 CockpitAnchor。J10CE 仍保留原相机位置，**本次没有采用尚待确认的眼点候选**。

## 坐标迁移规则

旧 Mesh 相对 Actor 的有效平移为 M，旧结构原点为 O：

```text
新 StructuralFrameOrigin = O - M
新 Airframe 相对变换 = Identity
新相机锚点 = 旧锚点 Actor 坐标 - M
新 Actor 世界位置 = 旧 Actor 世界变换.TransformPosition(M)
```

相同规则应用于出生点与已有实例，保持视觉模型和真实 CG 的世界位置。
本次两个关卡没有预放置 Fighter，迁移了两份 Pawn 蓝图及三个 PlayerStart。
F16 的模型内眼点 / 追尾焦点保持不变；J10CE 消除了旧 Airframe 的 -10 cm X 偏移。
移除原生 VisualRoot 属性、组件及旧层级代码，没有保留运行时兼容偏移或按机型追加出生补偿。

## F16 保留的标定和几何差异

视觉资产 `SK_West_Fighter_F16C`，物理模型仍为 `f16`，XML、接触点、弹簧阻尼、质量、惯性及气动参数未改动。
前轮未压缩物理接地点换算至当前 Mesh / Actor 约为 (210.462918, 0, 0.003548) cm，
对应实际 LOD0 胎面最低点。原轮胎几何测量不变，只重表达 Actor 坐标。

| 部位 | 视觉接地点减物理参考点 cm，保留残差 |
|---|---|
| 前轮 | 约 (0,0,0) |
| 左主轮 | (-39.2445, +9.7878, -1.0141) |
| 右主轮 | (-39.2449, -9.7880, -1.0141) |

视觉轴距约 397.38 cm、物理轴距 358.14 cm；单个原点平移不能消除轴距或轮距差异。
轮胎滚动半径仍为前轮约 22.40354 cm、主轮约 30.03159 cm；悬挂资产与行程保持原配置。
当前基准见 [calibration.json](calibration.json)，迁移前测量快照见 [原始记录](F16_measurement_before_origin_rebase.json)。

## J10CE

固定展开、零压缩、前轮回正姿态见 [标定姿态](J10CE_CalibrationPose/README.md)。
最新 Mesh = Actor 坐标表和 JSBSim 对照见 [J10CE 几何测量](J10CE_Geometry/README.md)。
结构原点迁移后，J10CE 接地点已按测量值写入 XML；眼点、CG、气动、飞控及暂停的动画改造未展开。

## 验证

F16 的一次性几何导出测试已于 2026-10-29 清理；[原始轮胎测量 CSV](F16VisualGeometry.csv) 保留。固定视觉层级、CG 与相机锚点的回归仍保留，当前清单见 [测试维护说明](../Testing/README.md)。

完整编译、保存后复开、自动化、PIE 和物理资源哈希结果见 [StructuralOrigin/validation.json](StructuralOrigin/validation.json)。
相机结构见 [Camera](../Camera/README.md)，120 Hz 固定步进和统一显示插值见 [Presentation](../Presentation/README.md)。
历史验证文件保留各自执行时的坐标约定；当前配置以本文和本次迁移验证为准。
