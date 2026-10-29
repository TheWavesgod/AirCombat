# 起落架悬挂动画制作与接入

## 已有的数据与职责

JSBSim 是地面受力和支柱弹簧阻尼的唯一权威。
插件导出 `GetCompLen()` / `GetCompVel()` / `GetCompForce()` 的结果，并换算为米、米每秒、牛顿。
动画快照进一步提供每条腿的 `CompressionCm`、`CompressionSpeedCmPerSec`、`StrutForceNewtons`、`bWeightOnWheel`。

收放与悬挂是两个不同维度：

- `Extension01`：0 完全收起，1 完全放下；用于整套起落架收放。
- `CompressionCm`：支柱实际压缩长度；用于承重、着陆时的减震机构形变。
- `SuspensionAlpha`：`clamp(CompressionCm / MaximumCompressionCm, 0, 1)`，仅在行程已配置时可用。

目前两份绑定资产的 `MaximumCompressionCm` 都保持 0，悬挂最大行程等待动画资产标定。
F16 的 `WheelRadiusCm` 已按实际轮胎几何标定：前轮约 22.40354 cm，左右主轮约 30.03159 cm；J10CE 仍为 0。
固定模型对齐与未压缩轮胎底部基准见 [F16 视觉对齐](../VisualAlignment/README.md)。
原始压缩厘米数始终可用；未标定时 `bHasSuspensionRange=false`、归一化采样为 0。
地面实测的静态压缩约 6–10 cm 是受力结果，不是动画最大行程，不能直接当作最大值。

## 建议怎样准备资产

每条腿准备独立悬挂动画，例如：

```text
AN_<Rig>_Suspension_Nose
AN_<Rig>_Suspension_LeftMain
AN_<Rig>_Suspension_RightMain
```

1. 以“起落架已放下、支柱完全卸载伸长”的姿态作为第 0 帧。
2. 最后一帧为该支柱允许的最大压缩姿态，量出相对于第 0 帧的实际压缩行程（厘米）。
3. 可以包含缓冲支柱、剪式连杆、轮叉和关联机械件的多骨骼联动；复杂几何关系直接烘焙在片段里。
4. 不要在悬挂片段中动画机体 Root / 重心，不要重复起落架整套收放，也不要夹带轮胎持续滚动或前轮转向。
5. 骨架、骨骼名必须与目标 SkeletalMesh 一致；保留各自的 F16C / J10CE 骨架，不用重定向去共享不同机构。
6. 优先做相对于“未压缩姿态”的 Local Space Additive 片段；如果保留绝对姿态，则使用清晰的腿部骨骼蒙版隔离混合。

时间长度可以自由选择。片段作为“压缩程度到姿态”的曲线使用，不按真实时间自行播放。

## AnimBP 如何连接

推荐顺序：

```text
起落架实际收放姿态
  → 每条腿的悬挂片段（局部加法 / 分骨骼混合）
  → 前轮转向
  → 轮胎滚动
```

在该外观自己的 `DA_*_AnimationBindings` 的 `Gears` 中设置对应腿的 `MaximumCompressionCm`。
然后：

```text
GetSuspensionAnimationTime("Nose", 悬挂片段长度)
  → Sequence Evaluator.ExplicitTime
  → Apply Additive / Layered Blend per Bone
```

`GetSuspensionAnimationTime` 使用实际压缩比例采样；没有配置行程或数据不可用时返回 0。
Sequence Evaluator 设置 `Teleport to Explicit Time=true`、禁用循环。
动画已经按比例采样时，不要再把混合权重也设为同一个比例；通常采样后的机械姿态以权重 1 应用，避免重复缩放幅度。

只需单根支柱直线压缩、还没有片段时，也可以使用：

```text
GetSuspensionTranslation("Nose", 该骨骼空间的压缩轴)
  → Transform (Modify) Bone.Translation（Additive）
```

函数返回归一化轴乘真实 `CompressionCm`，不要求配置最大动画行程。必须在 AnimBP 中选择正确骨骼、轴向和空间，不能把三个腿统一猜成世界 Z 轴。
多连杆机构更适合动画片段，不建议靠一个位移节点硬推全部连杆。

## 轮胎半径与当前限制

设置各腿的 `WheelRadiusCm` 后，快照按 `轮速 / 半径` 积分出轮胎角度，已有 AnimBP 滚动节点会消费它。
F16 已配置实测半径并启用视觉滚动；J10CE 未配置半径时不会虚构转速，原始轮速仍然提供。轮胎旋转轴已按两套骨架分别连接。

当前已经完成真实悬挂数据与采样接口接入，但最终悬挂片段尚未提供，因此没有声称已完成最终支柱形变美术效果。
准备好资产后，补齐行程、片段和腿部蒙版即可，不需要改 Fighter 或再增加物理弹簧。
