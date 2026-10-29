# JSBSim 遥测初始化、接触状态与重载清理

更新：2026-10-28。修改范围是 UE 遥测适配层；没有改 JSBSim 二进制库或 XML，气动、飞控、质量与发动机参数均保持原值。

## 根因

当前 JSBSim 的 FGLGear::ResetToIC 没有初始化 StrutForce；起落架未完全放下时跳过接触分支，
也可能留下之前的 WOW、压缩、速度和力。冷启动收起时的约 1.3e258 N 是不可靠读数，不是应当据此调整模型的真实支柱载荷。
UE 原来的初始与逐帧采集都会无条件读取 GetCompForce。

原采集还调用了虚函数 FGLGear::GetBodyForces，它会重新查询地面、执行接触判断、轮速衰减与接触报告；
额外调用频率跟随显示帧率，不能把它当作纯 getter。
FGear.Force 同时误用 FEET_TO_CENTIMETER，再经 TransformPosition 加入 CG / 结构原点位置，单位与方向都有误。

## 当前契约

- 模型未完成初始化时只提供可读的接触元数据，不读取尚未生成的动态支柱数据。
- WOW 必须同时满足模型就绪、起落架完全放下和原始 WOW。部分收放或收起时强制为 false。
- 未触地时 CompressionMeters、CompressionVelocityMetersPerSec、StrutForceNewtons、Force 都严格为零；不会先读未初始化值再做有限性过滤。
- 只有有效承重的 BOGEY 读取支柱压缩和轴向力；STRUCTURE 接触不冒充有悬挂的轮腿。
- WheelRollLinearVelocityMetersPerSec 保留真正的离地空转；不因 WOW=false 就将轮速清零，也不在每次读取时推进衰减。
- 未放下时转向角回零。固定式起落架始终发布 extension=1、down=true，不受全局收放输入误导。
- StrutForceNewtons 保留 JSBSim 轴向符号，压缩为负值；需要显示载荷大小的 UI 可取绝对值。
- Force 是 Actor 轴系中的牛顿向量。换算为 lbf×4.4482216152605；Body→Actor 包含 Z 轴反射，但绝不加位置偏移。
- 采集通过已求解的轴向力访问器刷新力缓存，不执行完整接触计算。
- 调试力箭头先将 Actor 向量转至世界方向，再用显式 0.001 cm/N 比例显示。

Gears 的初始/逐帧复制统一为一处。每次生成干净快照，保留作者可编辑的前/后腿标记。
这些标签在 LoadAircraft(false) 时按接触名称保留；LoadAircraft(true) 恢复默认。

## 相邻清理

- FAircraftState.Reset 现在清除完整状态，包括速度、导航、姿态、失速告警和 Crashed，原来仅清操纵面。
- 释放模型时立即清除 Gears、EngineStates、CG/EP/VRP、显示历史和属性节点缓存；标记 AircraftLoaded/Trimmed=false。
- 释放组件持有的模型和属性树引用后再销毁 Exec，PropertyManager 不再残留旧树。
- 每帧重新生成发动机快照，避免喷口、N1/N2、加力、磁电机等类型专属字段遗留；没有扩展尚未实现的发动机类型功能。
- LoadAircraft(false) 保留可编辑燃油密度和油量；油箱数量及只读容量、温度、百分比随新模型重建。
- 油箱输入在未就绪时仍是配置值，不能当作正在飞行的实时测量；C++ 消费者检查 IsSimulationReady，蓝图检查 Trimmed。
- 属性查询在无模型、无节点或路径无效时清空输出；批量查询短输入列表安全地作为只读请求，不再越界或复用旧字符串。
- 编辑器预览按模型名称不匹配时重载，重载失败不标记为准备完成。
- 删除空构造函数、AddZeroed 初始化路径、重复 getter/查询代码和已注释的旧调试循环。没有新增蓝图参数，也不需要资产迁移。

## 自动化与实际运行

完整 AirCombatEditor Win64 Development 编译成功。31 项自动化通过，0 失败、0 警告、0 跳过。
新增测试用实际 JSBSim 模型和地面回调计数，覆盖地面载荷、单位和轴系、坐标平移不变性、离地真实轮速、
收放残留、冷启动、重复读取、无效模型、类型切换、配置保留和属性查询复用。
重复 25 次采样不增加地面查询、不改变仿真时间，也不重复衰减轮速。

| 模型 | 工况 | FPS | 帧数 | 结果 |
|---|---|---:|---:|---|
| f16 | ground | 60 | 157 | 通过 |
| f16 | cold_up | 60 | 150 | 通过 |
| f16 | air_down | 144 | 361 | 通过 |
| f16 | cycle | 240 | 2521 | 通过 |
| j10ce | ground | 60 | 150 | 通过 |
| j10ce | cold_up | 60 | 150 | 通过 |
| j10ce | air_down | 144 | 1040 | 通过 |
| j10ce | cycle | 240 | 2281 | 通过 |

8 组 PIE 使用 NullRHI，只检查真实 JSBSim 运行数据，不把它等同于图形画面验收。
F16 直接放下空中初始化通过；J10CE 的 air_down 工况先在收起时配平，再放下并保持，cycle 覆盖完整放下/收起。
冷启动收起的两架飞机还检查了运行中重载：旧空速、导航、发动机输出和接触载荷立即清除。
最后的固定式起落架元数据修正由 c172p 原生回归覆盖；两架战斗机运行工况走的是相同的可收放分支。

## 独立记录的模型限制

J10CE 在 400 节、起落架已经放下时的直接空中配平未通过；本轮没有以调气动或飞控参数绕过它。
先收起配平后再放下可以完成本轮遥测与收放验证。该限制应在后续模型初始条件/配平专项中处理，
不能据此宣称该机型所有构型和速度都已具备稳定配平能力。

详见 [validation.json](validation.json) 与 [runtime.json](runtime.json)。
