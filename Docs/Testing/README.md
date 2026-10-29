# 代码与测试清理（2026-10-29）

当前保留 **32 项**有实际回归价值的自动化测试。测试不会在 PIE 或游戏启动时自行执行；只有通过 Automation 窗口或命令主动运行才会创建测试 World、加载测试模型。

本次统一为 `WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR` 编译条件：编辑器开发构建保留测试，非编辑器 Game/Server 和 Shipping 构建不包含这些测试实现。`EditorContext` 决定测试运行环境，预处理条件决定实现是否编译，两者职责不同。

## 保留的覆盖

| 分类 | 数量 | 保留原因 |
| --- | ---: | --- |
| Input | 6 | 虚拟杆推出、回中、换机复位；输入资产引用、正负键合并和稳定重绑名称 |
| FlightControls | 3 | 减速板按住/松开、初始配平交接、模型独占方向舵反馈 |
| FlightDynamics | 5 | 地面查询回退；收轮/离地清除悬挂残留；遥测只读性；模型和发动机类型切换；完整状态复位 |
| Camera | 5 | 鼠标增量只消费一次、视角隔离、机体姿态、回正、碰撞、显示插值航向 |
| Presentation | 5 | 120 Hz 时钟、异常 Delta/hitch、最后两个子步与四元数、跨帧率显示连续性、失效后复位 |
| Animation | 4 | 舵面单位/缺失值、真实悬挂、轮速积分、预览/重载清理、F16 机型绑定 |
| FlightDebug | 3 | N/A 与零值、120 Hz 历史窗口/峰值/复位、单位与机型实际数据来源 |
| Visual | 1 | Mesh、座舱和追尾锚点共享固定空间；CG 变化不能移动视觉几何 |

保留这些测试是为了防止已修复的问题再次出现。它们不替代真实 PIE 飞行验证：例如固定速度的插值测试不能证明任意机动的飞行品质，资产映射校验不能证明所有实际按键事件都已穿过 Slate 焦点与输入系统。

遥测测试中的 `c172p` 用于覆盖活塞发动机与固定起落架，验证切换机型后不会残留涡轮/喷口数据；它是兼容性夹具，不是新增玩家机型。

## 删除与整理

- 删除 `AirCombat.Input.KeyboardVirtualStick.OppositeKeysCancel`。它手动创建零目标，没有实际合并按键，只重复了回中行为。正负键的 `Cumulative`/`Negate` 配置仍由 `ImcOppositeKeyMapping` 校验，实际输入链仍使用 PIE 检查。
- 删除 `AirCombat.Visual.F16ReferenceGeometry`。它遍历导入顶点并写出 CSV，没有与标定基线比较，属于一次性测量导出，不是持续回归断言。历史测量结果保存在 [F16VisualGeometry.csv](../VisualAlignment/F16VisualGeometry.csv)；需要重新标定时应显式开展测量，不应每次回归自动生成测量文件。
- 删除 JSBSim 桥接中未接入的 `LStream`/`Stream` 标准输出重定向器、没有调用的 `LogInitialization()`、只写不读的 `TickTime` 及每帧计时取样。原有绿色 `DrawDebugMessage()`/`DrawDebugObjects()` 和蓝图开关保留。
- 删除飞行监测快照中仅赋值、没有消费者的 `DetailTitle`；标题继续由 Widget 的当前页面生成。扫描当前 221 个 `.uasset`/`.umap` 未发现此字段引用。
- 去掉起落架分支已确认 `IsBogey` 后的重复条件、废弃注释代码，以及几何导出留下的编辑器渲染/文件写入 include。标准库头文件改为直接包含所需的 `<memory>`。
- 为此前没有编译边界的输入测试补上条件，并统一其余测试及测试专用 friend 的边界。

## 明确保留

`Source/AirCombat/FlightTest/` 是测试关卡的实际 GameMode/出生点功能，不是自动化临时脚本。地面和空中出生点依赖它。

`AircraftCalibrationTools` 是 Editor 模块，仍承担 J10CE 标定动画的数据模型支持、动画编辑完成通知和蒙皮几何导出。删除插件可能影响现有标定资产，不能仅因为 C++ 没有直接调用其 Blueprint/Python 接口就判断为无用。

动画、相机和状态查询中的 `UFUNCTION`/`UPROPERTY` 是蓝图反射接口；引擎回调也由引擎调用。本次没有按照“没有 C++ 调用点”批量删除这些接口。`DetailTitle` 的删除同时核对了原生消费者和当前资产引用。

源码里的测试与 Docs 中的历史验收记录职责不同。旧 JSON/截图/测量记录保留原来的日期、测试数量和坐标基准，不改写成今天的结果。当前测试清单及编译结果以本页和 [validation.json](validation.json) 为准。

## 运行

编辑器控制台运行：

```text
Automation RunTests AirCombat.
```

也可在 **Tools → Test Automation** 中按 `AirCombat.` 筛选。命令行可使用本机引擎的 `UnrealEditor-Cmd.exe`：

```text
UnrealEditor-Cmd.exe AirCombat.uproject /Game/Levels/Tests/L_InputTest -nullrhi -unattended -Multiprocess -ExecCmds="Automation RunTests AirCombat." -TestExit="Automation Test Queue Empty" -ReportExportPath=<报告目录>
```

无需为单次清理新增测试模块或另一套测试框架；现有按功能分布的 `Tests/*.cpp` 保持原位。运行飞行监测与输入面板，仍使用各自的 `AirCombat.Flight.*` 和 `AirCombat.Input.*` 控制台入口。

## 本次验证

`AirCombatEditor Win64 Development` 完整编译成功；32 项自动化全部通过，0 失败、0 测试警告、0 跳过。

644 个 Content、Config 和 JSBSim 模型资源文件的哈希与清理前一致。旧绿色调试输出实现、Fighter/相机/动画运行代码、输入调试面板和 AircraftCalibrationTools 保持原样。本次没有重跑飞行品质/机动包线测试，也未执行非编辑器 Game/Server/Shipping 构建；非编辑器测试隔离通过预处理条件核对。
