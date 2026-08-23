# YYZ rigid step and mass boundary

本 package 目前包含两个连续的 R1 YYZ 产品切片，以及 R3 target-conformance/canonical 00A 所需的导航、初始映射与 committed mission evaluation 组件：单段刚体 candidate、两段刚体/标量燃耗的 typed atomic-boundary composition、`TruthPassthroughNavigation`、package-owned `Canonical00AInitialMappingQuery`、constant-space `CommittedMissionAccumulator` 和 runwide terminal evaluator。对应稳定产品 identity 由 package descriptor、mapping identity 与 exact implementation table 共同冻结。

单段刚体调用链：

```text
typed committed rigid state
+ typed environment and mass properties
+ prepared AerodynamicTable PureQuery and real table asset
+ supplied body wrench
→ formal strict trilinear query output
→ aerodynamic dimensionalization
→ pure ForceMomentClosure output and RigidFormInput
→ full-inertia rigid derivative consumes the held form input
→ fixed RK4 interval candidate
```

`PreparedAerodynamicTableModel` 是 YYZ 当前真实 PureQuery：独立 definition 保存气动几何与 table asset identity，独立 asset 保存 Mach/alpha/beta axes 和 coefficient rows，prepare 形成 immutable trilinear view。`AerodynamicTableQueryKernel` 的正式 output 只提供六个 coefficients，domain status 与 weights 留在 telemetry；`RigidStepKernel` 直接消费该 output 并保持既有 dimensionalization 数值。

`PreparedForceMomentClosureModel` 是当前唯一使用 `Closure` execution form 的 YYZ 模型。`ForceMomentClosureKernel` 规范化 contribution 顺序，拒绝重复 source 与 frame/clock/revision/interval 不一致，逐项完成 CoM 力矩搬移，并以正式 output 提供 total force/moment 和 `RigidFormInput`。`RigidStepModelDefinition` 在准备边界校验固定步长、数值策略、frame/clock identity、气动几何和真实表格 asset；`PreparedRigidStepModel` 持有不可变定义、AerodynamicTable query model 与 Closure model。`RigidStepKernel` 只接收显式 typed 输入并返回 candidate，不读取 Session、Mission、文件系统、logger 或全局 registry。

package descriptor 将 AerodynamicTable 声明为 `vehicle.output + PureQuery`，ForceMomentClosure 声明为 `interaction/closure + ContinuousClosureLink + IntervalModel`，并提供各自 canonical config schema。RigidStep 的两个 required input 分别要求 exact PureQuery 与 closure contract。对应 builder 从稳定 block 重建 typed definition；aero asset slot 以 exactly-one `AssetBinding` 接受 `gnc.asset.yyz.aerodynamic-table.multiaffine@1`。这些字段由通用 Compiler 读取，package id 不进入 Compiler 分支。

同一 package contribution 现在把 `TruthPassthroughNavigationKernel` 与真实 `AltitudePitchGuidanceKernel` 描述为 stateless RuntimeComponent Catalog 候选。navigation 对 frame、clock、revision、quality 与 finite rigid state 做 exact 检查，随后逐位保留 committed truth observation，并用独立的 `navigation-estimate` output port 与 codec entry 供 guidance 消费；值 contract/layout 继续复用 committed rigid observation。guidance 对每份 navigation estimate 做独立求值，正式 output 被 `PitchMomentControllerKernel` 消费。它从被动姿态矩阵的 body-forward 轴计算一般 heading 下的 signed pitch，并为既有 pure-`B-y` fixture 保持原运算顺序与冻结数值；canonical level-east attitude 的 measured pitch 为零。两个 descriptor 均冻结 `SampledTransform + BoundaryEvaluation`、process phase、package-owned interval-1/current-cycle sampled ports、zero-order hold、lifecycle 和 exact kernel identity，且不开放 state schema。canonical config builder 分别保留 navigation 的 frame/clock/revision，以及 guidance 的 frame/clock/revision、三项参数与完整 quaternion policy。

R2 package contribution 进一步冻结了真实 uniform-environment PureQuery、RigidBody/Mass 两个 StateOwner、initial/projection/derivative/evolution entries，以及 guidance/controller/actuator/fixed supplied-propulsion/terminal evaluator 的静态合同。Mass 的既有 `NumericalPolicy` 已进入 canonical Definition/config，runtime-facing initial/evolution entries 不要求 Session 另行生成策略。`ControlledPropelledRigidMassStepKernel` 等 R1 wrapper 仍只作为科学/oracle compatibility composition，不被登记为 StateOwner 或 RuntimeComponent。

package 现在为十个 RuntimeComponent 提供各自 package-specific typed `RuntimeCellFactory`，并为 RigidBody、Mass、CommittedMissionAccumulator state 与真实 stored value 提供窄进程内 codec。R2/R3 exact-link 这些 entry、call shape、独立 C++ type witness 及 numeric handle，factory binding 压缩为稳定的 state/input/output/writer/invocation/provider/interval/integration/transaction/history handle。uniform environment 与 aero 的正式 query output 走授权 caller 的局部 typed return，不分配 CycleFrame result slot；ForceMomentClosure 的正式 output 由唯一 Closure Coordinator writer 写入 held interval slot。显式 composition adapter 负责恢复 package 类型并调用已链接 entry；Kernel 不按 model id/type switch 重建 invocation set，telemetry 也不成为 environment/aero/closure 的权威 result flow。

scheduled stuck-actuator qualification 现在把 actuator owner 收窄为唯一权威的 `FaultStateFragment`：健康位置按当前 demand 在每个 boundary 直接求值，只有 stuck 模式保存锁定位置。contact evaluator 在每个 committed boundary 同时发布 domain result 与通用 typed `TransactionBranch`；Session 只读取 Image 冻结的 branch slot，并在首次 ground-impact boundary（当前 fixture 为 tick 10）走 Terminal、跳过后续 interval candidate。命令 reducer、event consumer 与 ObservationSeal 的明确失败可原边界重试；普通 evaluation、candidate validation 与 final precommit 失败固定为 `Failed + Invalid`，不会因队列中存在命令而降级为可重试。

00A target-rate composition 通过 programmatic source override 选择 `0.01 s` base、navigation/guidance/controller/actuator interval `1/5/2/1`、observation interval `4`、全零 offset 与 HeldLatest age `4/1`。目标组合增加 per-Session `CommittedMissionAccumulator` state owner：每次普通 interval candidate 只合入当 tick 已发布的 committed rigid observation 和 mass properties，ModelCommit 后更新 opening/latest boundary、sample count、全程 extrema 与 earliest terminal decision。终端时，depth-one history 将累计到 tick 2999 的状态与 tick 3000 committed rigid/mass boundary 合并，输出覆盖 tick 0～3000、`3001` 个样本和 `30.0 s` 的 `duration-complete` runwide result。frozen terminal state 在幂等返回前仍执行完整 committed-state invariant；opening/latest tick、sample count、latest 可重算 metrics、terminal result/boundary/count 与 extrema 合法范围均以常量空间验证。既有 depth-three evaluator 继续输出带 `window` 命名的 tick 2998～3000 诊断。`r3.kernel-yyz-target-rate.probe` 对 tick-31 短跑验证完整调用、source tick、age、transaction/lifecycle 语义，并以两次 3000-tick 运行验证 bit determinism；证据保持 `target_conformance/science_verdict_pending`。

`Canonical00AInitialMappingQuery` 把精确 author facts 映射到 `frame.yyz.00a.launch-local-enu@1`：ENU position `[0,0,1000] m`、velocity `[220,0,0] m/s`、body forward/right/down 对应 East/South/Down，初始 `q_I_B` 的 double-precision representative 为 `[0,1,3.0615158845559431e-17,0]`，angular rate 为零。query 只接受纬经 `31.2304/121.4737` 的 launch anchor，并同时验证 duration、mass、command 与整数 cadence；Earth rotation、curvature、ellipsoid conversion 和 changing tangent plane 都在适用域外。canonical source 从该输出建立独立 mission/plan/subject/clock 和 Image fingerprint `e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c`，旧 interval-1 fingerprint 保持 `7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14`。真实 Session 的第一步在 opening Mach `0.6176470588235294` 超出 exact aero asset 闭区间 `[0.2,0.6]` 时 fail closed，committed tick/epoch 均保持 0。独立 Decimal oracle 对全部可得 opening 字段与 domain status 达成一致，后续 trajectory 与 terminal verdict 等待 owner 选择科学域。

两段边界调用链：

```text
opening committed rigid state + MassState
+ held environment and supplied body wrench
+ typed MassFlowInterval
→ RigidStepKernel reads opening committed mass
→ ScalarBurnMassKernel produces a constant-geometry mass candidate
→ both candidates form one AtomicRigidMassCandidate
→ complete candidate becomes the next committed boundary value
→ interval 1 consumes that rigid state and mass
```

`FrozenRigidMassStepKernel` 只在刚体与质量两项 candidate 全部成功后返回值。`TwoIntervalMassCommitKernel` 把完整 candidate 提升为下一段 opening boundary；调用者无法向第二段单独注入陈旧质量或陈旧刚体状态。产品实现保持独立 identity，并由 `ORACLE-YYZ-TWO-INTERVAL-MASS-COMMIT-001` 的 80 位 Decimal 分段解析轨迹直接比较。

当前 atomic boundary 是 package 内的纯值组合，不拥有 mutable state store、epoch 或 Session rollback。运行期 `ModelCommit`、失败后保留上一成功边界、推进查询、控制面/速率导数、fuel geometry、depletion event、Compiler 与调度继续留在后续阶段。

直接验证入口：

```powershell
ctest --preset dev -R '^r1\.yyz-rigid-step\.(probe|oracle)$'
ctest --preset dev -R '^r1\.yyz-two-interval-mass-commit\.(probe|oracle)$'
ctest --preset dev -R '^r2\.compiler-runtime-component-catalog\.probe$'
ctest --preset dev -R '^r2\.yyz-static-product-contracts\.probe$'
ctest --preset dev -R '^r2\.compiler-complete-yyz-plan\.probe$'
ctest --preset dev -R '^r3\.kernel-yyz-target-rate\.probe$'
ctest --preset dev -R '^r3\.yyz-00a-initial-mapping\.probe$'
ctest --preset dev -R '^r3\.kernel-yyz-00a-canonical\.probe$'
ctest --preset dev -R '^r3\.yyz-00a-canonical\.oracle$'
```

首条 oracle 检查直接读取 `REF-YYZ-FROZEN-INTERVAL-001`；第二条读取 `REF-YYZ-TWO-INTERVAL-MASS-COMMIT-001`。两者都使用原 fixture 声明的容差，reference 与产品 model identity 保持分离。
