# J10CE 起落架接地点标定

2026-10-28：按完全展开、未压缩、前轮回正姿态的 LOD0 胎面测量更新 XML。
前双轮仍使用一个等效 NOSE_GEAR；三个 location 均为未压缩接触点，不是轮心，不再额外减半径。

| 接触单元 | XML Structural in | Actor = Mesh cm |
|---|---|---|
| NOSE_GEAR | (-138.2766, 0.0000, -74.9303) | (331.2226, 0.0000, -230.3230) |
| LEFT_GEAR | (50.0283, -47.5616, -75.1002) | (-147.0719, -120.8065, -230.7545) |
| RIGHT_GEAR | (50.0283, 47.5616, -75.1002) | (-147.0719, 120.8065, -230.7545) |

保持 StructuralFrameOrigin=(-20,0,-40) cm、Airframe 单位变换。镜像取整相对原测量的最大误差 0.00027346 cm。
EYEPOINT 候选尚未确认，不写入；质量、惯性、燃油、载荷点、气动、飞控、推力和支柱参数均未修改。
本次只证明接触几何和所测初始工况可用，不等同于完整驾驶特性或全部载荷包线验收。

## 结构原点与 CG

JSBSim 的结构原点可以任意选择，Body 轴系才以当前总 CG 为原点，见 [官方坐标文档](https://jsbsim-team.github.io/jsbsim-reference-manual/user/concepts/frames-of-reference/)。
本项目自制 J10CE 模型继续约定固定结构原点位于其**假定空机 CG**，mass_balance/CG=(0,0,0) IN；它不是从 Mesh 推算出的实测 CG。
在当前 Actor / Mesh 坐标中，这个假定空机 CG 位于 (-20,0,-40) cm，而不是 Actor 原点。
空机质量、飞行员点质量及燃油共同决定当前总 CG；燃油变化不移动结构原点，也不每帧改 StructuralFrameOrigin。
本次默认测试载荷下的初始当前 CG 约为 (-3.8698,0,0.0578) IN，已不等于零。
F16 等现成数据继续使用它们自己的结构基准，无需为了统一习惯重写全部坐标。

## 验证

- XML 结构解析通过，实际 UE/JSBSim 新进程加载成功，三个参考点映射正确。
- J10CE 地面配平和 400 节空中初始化通过；各约 3 秒、60 FPS 短时运行，无仿真时间丢弃。
- 地面三个 WOW 均为 true，前腿压缩约 13.22 cm、两主腿约 7.42 cm；空中三个 WOW 均为 false、压缩为零。
- D3D12 离屏尝试发生设备移除，后续使用 NullRHI 完成上述物理验证，没有宣称图形渲染通过。
- 受保护的 Content / Config / Source / JSBSim 文件只改变 j10ce.xml，其余模型、动画、蓝图与配置未改动。
- 无 C++ 或反射类型变化，不需要重新编译原生模块。

另记录已有的起落架收起时 StrutForce 遥测异常：ResetToIC 未初始化该变量，未放下时又跳过无接触清零分支，UE 当前无条件读取。
本次看到空中右腿读数约 1.3e258 N。该历史读数未纳入当时的力遥测验收，也未据此修改弹簧或阻尼。现已在 UE 适配层修复，见 [遥测生命周期修复](../../../FlightDynamics/Telemetry/README.md)。
当时仅接触位置、WOW、压缩量及短时仿真检查通过；新的回归已确认公开遥测在收起/离地时归零。JSBSim 库内原始 getter 的未初始化路径仍由适配层隔离。

完整记录见 [validation.json](validation.json)、[runtime.json](runtime.json)。
