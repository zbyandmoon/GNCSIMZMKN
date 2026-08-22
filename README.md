# GNCZMKN Next

这是 GNCZMKN 大型目标架构的 greenfield 实现仓库。仓库版本仍为 `0.0.0-bootstrap`；R0 科学基线、R1 独立模型生态与 R2 静态编译已经闭合，R3 已形成 caller-bound InitializationCommit、两次 Continue、一次 Terminal、Completed-run ResetCommit、plan-derived cancellation、稳定历史 RunOutcome、显式 dispose，以及 route-free REF-YYZ process-local checkpoint/branch restore 串联的 formal run，并补充 qualification-only ModeOwner command/event 原子事务和真实 YYZ guidance→controller source-authored 双速率 `HeldLatest` 切片。

## 当前交付状态

- 当前 gate：`R3`；G0/G1/G2/G3 已由仓库所有者判定 `Passed`。
- 已闭合：R1 Foundation、窄范围 in-process Contracts、YYZ/CAVH 产品切片，以及 R2 从 canonical REF-YYZ source 到 proven/linked `ExecutionPlanImage` 的完整静态链。
- R2 Image 保存 exact package/build lock、按 storage class 划分的确定性 extent、slot size/alignment/offset/codec/writer/reader、两个 state codec、Query `CallerLocal` route、Closure `HeldInterval` route、fixed-step RK4 policy、完整 `TransactionPlan` 分支语义、reset lifecycle capability、plan-derived cancellation safe points、preparation lifecycle，以及七个 typed `RuntimeCellFactory` 的数字依赖闭包。R3 additive lowering 还可从 programmatic source extension 冻结 exact occurrence schedule 与 binding temporal override，并派生显式 `HeldLatest` binding 的 committed-output slot、producer/coordinator writer、reader set 与最大 age；这些事实进入 source semantic hash、proof、descriptor hash 和 Image fingerprint，空 extension 的 interval-1 编码与 fingerprint 保持原值。
- 当前 R3 切片只消费冻结 Image 与显式 package/generated adapter。Created Session 没有 active RunId；初始化请求携带调用方拥有的 opaque RunId 和精确 Image/plan binding，全部结构预检、typed materialization、initial state 与 evaluator history store 成功后才发布 `InitializationCommit{run_sequence=0, epoch=0, tick=0}`。提交前失败统一进入 `Failed`，不发布 active run，并继续保持强异常安全逆序清理。
- Session 通过统一 `execute_step()` 从 Image `TransactionPlan` 选择分支：两次 Continue 用固定步长 RK4、transaction-local FrozenInterval held form 与质量区间演化把 `(epoch=0,tick=0)` 推进到 `(1,1)` 与 `(2,2)`，第三步在 tick 2 执行完整 boundary DAG 和真实 committed-history evaluator，再以 Terminal instant ModelCommit 推进到 `(3,2)`。`run_to_terminal()` 只循环调用该入口。Terminal 保持刚体与质量状态字节，先 seal tick 2 observation，再 seal 与 `ORACLE-YYZ-MISSION-COMPOSITION-001` 对齐的 mission result，完成当前空 finalization set 并冻结 `Completed + Valid` RunOutcome。
- 每次 step 形成 `Committed | Terminated | Cancelled | Failed` StepOutcome，包含 Run/transaction/epoch/tick、最后到达位置、candidate/history/seal 摘要和可选 primary diagnostic。`request_cancel()` 是唯一允许与执行线程并发的 Session mutation；Kernel 在 Image 声明的 transaction start、boundary/candidate 间、最终预检后和 commit 后主动采样。commit 前取消清理全部 staging 并冻结 `Cancelled + Valid` RunOutcome；Continue commit 后取消保留该 StepOutcome 与新证据。取消不产生 error diagnostic，`run_to_terminal()` 直接返回 `Completed | Cancelled | Failed` drive status。Terminal 或执行失败先冻结时保持权威。
- `Completed` Session 接受携带新 Session-local unique RunId 与 exact RunBinding 的 reset。Kernel 在任何 staging 前确认七个 Runtime Cell 都声明 `Resettable`；缺失 capability 时保留旧 run 全部证据并形成 `Reset + run_start_committed=false + Failed + Unknown` attempt。成功的 `ResetCommit` 令 sequence 和 epoch 各增加一、tick 回到 Image initial tick，重建 mutable state，并清空 committed held outputs、history、seal、terminal result、journal 与本 run step count；Image、provider、PreparedModel 和 Runtime Cell 保持复用。InitializationCommit/ResetCommit 前失败的 finalization 为 `NotStarted`，run-start commit 后执行失败与正常完成为 `Succeeded`。
- `dispose()` 允许 `Created`、`Completed`、`Cancelled` 与 `Failed` 进入 `Disposed`，逆序释放 PreparedModel、Runtime Cell、state/candidate/frame storage、committed held outputs、history 与 seal，且清理只执行一次。释放后 materialized counts 为零，Image、最后 committed RunId 和冻结 outcome 继续可查询；析构不重复清理。`Initialized` 拒绝 dispose，活动 run 不会被隐式截断。
- route-free REF-YYZ 可在 `Initialized` committed boundary 发布 immutable process-local checkpoint。checkpoint 深拷贝 committed state、evaluator history、plan-declared held samples 与当前 sealed boundary，并保存 exact Image/RunBinding、parent run identity、epoch/tick/step count 和 continuation frame coordinates。新的 `Created` Session 以新 child RunId、同一 Image 和匹配的 typed implementation 完成普通 preparation/Runtime Cell 物化，随后用一次 `RestoreCommit` 发布私有 state/history/held/seal 和 lineage；child 从 `run_sequence=0`、checkpoint boundary 与干净 cancellation state 继续执行。取消请求始终绑定提出请求时的精确 RunId，捕获后取消 parent 不影响 checkpoint 或 child。同一 checkpoint 可恢复多份互相隔离的分支。
- programmatic `CompleteStaticCompositionSource` extension 沿普通 product definition→Compiler→link→Session 链把 package-owned guidance occurrence 设为 interval 2/offset 0，并把 exact guidance→controller binding 改为 `HeldLatest`；稳定 YYZ package descriptor 保持单一 interval-1 定义。tick 0 与 tick 2 使用 fresh guidance，tick 1 从 Session-local `CommittedOutputStore` 注入保留原 sequence/timing/quality 的 tick 0 样本，controller 读取 age 1，同时以当前执行 tick 发布 output context。仓库所有者已选择 00A target-conformance 速率形状：`0.01 s` base，navigation/guidance/controller/actuator/observation 分别为 interval `1/5/2/1/4`，guidance→controller 与 controller→actuator 最大 age 分别为 `4/1`；目标产品链与科学判定继续由 `R3-YYZ-001` 推进。
- qualification-only ModeOwner 通过可选 Compiler lowering 冻结一条数字 command route、一个 transaction、`InstantPatch` owner candidate 与同 transaction later-phase event delivery。`submit_command()` 只在 `Initialized` committed boundary 由单一执行 owner 调用；per-Session ledger/queue 形成 transaction-start cutoff，`Applied` 的完整 state replacement、application receipt、typed event 与 consumer output 随 ModelCommit 同时可见。reducer/consumer/seal/command-precommit 故障允许在同一 committed boundary 重试，普通 projection/boundary/history/integration/scientific candidate 故障继续冻结 Failed RunOutcome。已消费 queue 项在提交前压缩，未知 reducer decision、第二条 route、跨 transaction consumer 与未知 transaction 均 fail closed。reset 清空本 run control stores，失败 reset 保留旧 evidence，两份 Session 完全隔离。
- 暂缓开展：完整 00A 科学输入、真实资产/geodetic mapping、difference report 与 terminal science verdict、first-order hold/interpolation/extrapolation、异步 clock domain、生产 command consumer、通用多 route/多 transaction scheduler 与 event bus、command/event checkpoint participation、pending-command branch identity、active-run truncating reset、完整 RunResource hooks、durable checkpoint serialization/Artifact 与 compatibility migration、完整 DiagnosticRecord/PolicyDecision/renderer/store/artifact 链、YAML/INI、多端 adapter、runtime registry、StateFragment，以及 R4 Field/Artifact/Dataset/sink 与 R4～R8 其余能力。
- 旧 GNCZMKN 只作为只读行为与科学参照，不进入任何生产 target、include path 或运行依赖。

阶段顺序已经固定：R1 以 Definition、PreparedModel 和 Kernel 的无 Session 独立求值闭合；R2 实现 MissionSource 到已证明、已链接 ExecutionPlan 的静态编译；R3 已完成 formal REF-YYZ run、Completed-run reset、plan-derived cancellation、显式 dispose、route-free process-local checkpoint/branch restore、Session-local store/frame/IntegrationScope、窄 in-process outcome、首条 qualification command/event 原子事务与真实 YYZ edge 的双速率 held-output qualification，后续补齐生产调度 consumer、command/event checkpoint participation、active-run truncating reset、完整 RunResource 和运行诊断能力。

## 新成员从这里开始

1. 阅读 [当前执行状态](docs/handoff/r0-execution-state.md)。
2. 查看 [任务台账](docs/tasks/backlog.json)。
3. 阅读当前任务直接引用的 ADR、架构分册和测试。
4. 需要项目边界背景时再阅读 [交接总览](docs/handoff/README.md)。

## 构建与验证

```powershell
./tools/install-eigen.ps1 -DownloadIfMissing
cmake --preset dev "-DEigen3_DIR=build/dependencies/eigen-3.4.0/install/share/eigen3/cmake"
cmake --build --preset dev
ctest --preset dev
powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify-repository.ps1
```

也可以运行一键检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap.ps1
```

离线环境可以向 `install-eigen.ps1 -ArchivePath` 或 `bootstrap.ps1 -EigenArchive` 提供已下载的 Eigen 3.4.0 官方 ZIP；脚本会核对固定字节数与 SHA-256。

当前 CLI 只证明 composition root、编译器和测试工具链可工作：

```powershell
build/dev/gnc_sim --version
build/dev/gnc_sim --self-check
```

首个 YYZ 产品入口已能独立计算一步 candidate。`AerodynamicTableDefinition`、真实 multiaffine table asset、`PreparedAerodynamicTableModel` 与 `AerodynamicTableQueryKernel` 形成独立 PureQuery；`RigidStepKernel` 直接消费正式 coefficient output，再完成 dimensionalization。独立 `ForceMomentClosureKernel` 对显式 body-wrench contributions 做 CoM 力矩搬移与求和，正式 Closure output 生成 `RigidFormInput`，该输入固定用于全部 RK4 stages。`RigidStepOutput` 只携带下游消费的 candidate；air-data、气动 query telemetry、Closure evaluation 和初始导数位于 `RigidStepTelemetry`：

```powershell
build/dev/gnc_yyz_rigid_step_product_probe --self-check
```

第二个 YYZ 产品入口现已覆盖 committed observation、限幅 altitude/pitch guidance、pitch-moment controller、当前周期理想力矩执行、typed propulsion、连续两次 rigid/mass atomic commit，以及从三份 committed samples 生成的最小 mission result。推进力与控制纯力偶经同一个 supplied-wrench consumer 闭合，tick 1 提交后会重新读取刚体状态和 `99.95 kg` 质量；最终 tick 2 结果匹配 `REF-YYZ-MISSION-COMPOSITION-001`，同时保留 `REF-YYZ-PROPULSION-RESPONSE-001` 和既有 atomic-boundary 回归：

```powershell
build/dev/gnc_yyz_two_interval_mass_commit_product_probe --self-check
```

首个 CAVH 产品入口由独立 `GlideEnvelopeQueryKernel` 从 prepared parabolic envelope 生成正式 query output；Eq17/Eq18 组合显式消费该 output，再把 typed gamma reference 与正式 `alpha*` 交给 TDCT。limited alpha 属于正式 output；包络结果在公式组合层作为 telemetry 留存，公式中间量、TDCT 修正和饱和信息同样位于 telemetry。它复用 `ORACLE-CAVH-FORMULA-001` 比较三组方程与四组 TDCT 结果，并拒绝包络域错误、公式奇异、Eq17 导数退化、非法 TDCT 和上下文不一致：

```powershell
build/dev/gnc_cavh_formula_product_probe --self-check
```

当前 CAVH 输出止于 TDCT 公式阶段的限幅 alpha；产品级 guidance command 的 frame、时间与 ownership 映射等待真实 vehicle/controller consumer。

YYZ 与 CAVH 共同消费最小 `ModelDefinitionMetadata`、`PreparedModelMetadata` 和 `AlgorithmEvaluation<Output, Telemetry>`。公共 execution-form tag 现包含 `PureQuery | Closure | RuntimeComponent`；当前 PreparedModel 产品路径仍只使用前两类，clock/configuration expectation 由 package definition 保持。GlideEnvelope、AerodynamicTable 与 ForceMomentClosure prepare 对各自 model id、model version 与 execution form 做 exact 检查。产品 probe 直接验证真实 query/closure output 消费、错误 model version 拒绝与 C++ output 类型边界，并保留既有 R0 oracle。

既有 R2 static composition 继续从 package-owned descriptor 精确解析 CAVH `GlideEnvelope` 与 YYZ `AerodynamicTable` PureQuery、`ForceMomentClosure` Closure 和对应 algorithm consumer；其 `semantic-bytes@2` qualification 路径保持不变。完整 REF-YYZ 路径在 additive source/IR revision 3 中选择 uniform environment、aero、FrozenInterval closure、RigidBody、Mass、guidance、controller、ideal actuator、supplied propulsion 和 terminal evaluator 的真实产品定义。它冻结 current-cycle/interval temporal edges、两个唯一 StateOwner、initial state、PublishProjection、query/closure authorization、held form、rigid derivative、mass candidate、terminal committed history 和 rigid/mass atomic candidate set，不把 wrapper、dummy provider 或 RunBinding 伪装成 runtime node。

```powershell
build/dev/gnc_compiler_static_plan_probe --explain
build/dev/gnc_compiler_static_plan_probe --semantic-hash
build/dev/gnc_compiler_complete_yyz_plan_probe --self-check
build/dev/gnc_kernel_session_materialization_probe --self-check
build/dev/gnc_kernel_opening_boundary_probe --self-check
build/dev/gnc_kernel_step_transaction_probe --self-check
build/dev/gnc_kernel_multirate_held_output_probe --self-check
build/dev/gnc_kernel_command_event_probe --self-check
```

最后五个探针通过 fixture adapter 恢复 Image 中的 exact typed entries。Session 初始化只沿数字 handle 建立三个 prepared artifact、七个 Runtime Cell 和两组 committed/candidate state object；frame value 延迟到首次写入时构造，并在 frame 关闭时逆序销毁。`IntegrationHeld` 值始终留在当前 transaction 的 CycleFrame，RK4 与 candidate producer 完成消费后随 frame 一并销毁；显式 `HeldLatest` sampled output 另由 Image 授权进入 Session-local committed store。revision、handle 集合、extent membership、bounds/alignment/overlap、state/lifecycle、held-output/cancellation/command-event authority、materializer identity、精确 preparation 依赖与 type witness 均在 placement 前验证。多速率探针覆盖 fresh/held/fresh、age、missing/expired、clone/validation/precommit rollback、pre/postcommit cancellation、共享 provider 双 Session 隔离、reset/dispose、checkpoint corruption、双 child suffix 与 parent-cancel/child-continue。完整 interval-1 REF-YYZ 运行仍形成三个 committed history samples、tick 0/1/2 ObservationSeal 和 tick 2 terminal result seal，并保持既有 fingerprint、oracle、terminal result、reset、cancellation 与 branch restore 回归。

YYZ package 还贡献 `AltitudePitchGuidance` 的 stateless `SampledTransform` descriptor：`vehicle.process` placement、`process` phase、`BoundaryEvaluation`、zero-order hold、`Instantiate | Resettable | Dispose` lifecycle 和 exact guidance kernel identity。稳定 package surface 只有 package-owned interval-1/current-cycle descriptor。独立 2:1 qualification 由 programmatic source extension 精确引用真实 occurrence 与 binding，覆盖 guidance interval、controller age 和 `HeldLatest` relation；Compiler 在 canonical lowering 前核对 endpoint、port、contract、offset 与可达 age，link 仍以原始 package implementation signature 做 exact conformance。canonical config 可确定性重建 definition，Catalog 对 form/profile/recipe/obligation/schedule/port/lifecycle 组合做封闭校验：

```powershell
build/dev/gnc_compiler_runtime_component_catalog_probe --self-check
```

`GNC-PLAN-RUNTIME-COMPONENT-UNAVAILABLE` 继续保护既有窄编译入口和残缺 RuntimeComponent 图；REF-YYZ revision 3 已闭合 provider、consumer、owner、schedule、temporal、invocation、typed factory、result route、stored slot/state codec、integration、transaction 与 preparation facts。缺 provider/授权、owner/writer 不唯一、非法 reader、错误 alignment、layout overflow、非法 phase/cycle/time relation、scope/transaction/lifecycle 不完整、factory/codec/build-lock/result-route cross-reference 错配和 unresolved implementation 均 fail closed，失败路径不发布部分 Image。`R2-PLAN-001`、`R2-PRF-001`、`R2-LINK-001` 与 `R2-GATE-001` 已完成，G3 结果为 `Passed`。

`hash_canonical_mission_ir` 使用 `gnc.canonical-mission-ir.semantic-bytes@2` 的显式 tagged/length-prefixed big-endian encoding 与 SHA-256。source URI/path、输入顺序和 plan id 被排除；C++ 与 Python reference 继续固定 YYZ qualification vector `b29dc67f2a9e0bb36cb18a5e54a8c4830bdb0cae718fbf856646ba903892511b`。RuntimeComponent 仍不会进入该 API。完整图改用 additive `gnc.canonical-mission-ir.semantic-bytes@3`：source semantics覆盖 model/config/asset/port/state/obligation/schedule/temporal/invocation composition，package entry identity、recipe、workspace、state layout、build fingerprint、函数地址与 source location均排除。Optional occurrence/binding override 使用独立 conditional domain；只有 extension 存在时才编码 source-authored schedule/temporal facts。Descriptor revision 6 与 Image revision 3 另保存 exact implementation、allocation、access、lifecycle 与 cancellation facts；optional command/event 与 held-output extension分别把自身运行字段纳入 descriptor/Image fingerprint，无对应 route/edge 的 REF-YYZ hash 保持原值。R3 已提供 Session-local committed/candidate/runtime/held-output stores、有界 CycleFrame、transaction-local `IntegrationHeld`、受限 IntegrationScope view、Image-backed fixed-step RK4、rigid/mass candidate 原子提交、committed history、ObservationSeal、terminal result seal，以及 caller-bound RunId/RunBinding、InitializationCommit、Completed-run ResetCommit、plan-derived in-process cancellation、stable historical RunOutcome、explicit dispose、route-free process-local checkpoint/branch restore、qualification-only command/event transaction 和真实 sampled edge 的多速率 held injection。当前最小 in-process sealed storage 只保留最新 boundary 与完整 terminal result。syntax-neutral `SourceTree`/`SourceMap`、通用 `PreparedModelKey`/共享 cache、生产 command/event consumer、通用多 transaction scheduler、command/event checkpoint、pending-command branch identity、active-run truncating reset、完整 RunResource hooks、durable checkpoint/diagnostic/artifact 与 compatibility migration 仍待后续任务；asset proof仍只证明 source-selected identity preservation。R4 及后续阶段保持锁定。

## 仓库地图

```text
framework/include/gnc/
  foundation/      数学、数值和值工具
  contracts/       领域、时间、诊断和产物契约
  model_sdk/       definition metadata 与 algorithm evaluation
  compiler/        source、catalog、IR、proof 与 lowering
  kernel/          session、region、state、transaction 与 backend
  evidence/        observation、artifact 与 lineage
  workflow/        experiment、task graph 与 tool port
  application/     use case、control 与 DTO

packages/          可复用领域、模型和工作流贡献
adapters/          CLI、Python、工具、存储、IPC 与前端适配
user/              项目私有研究代码、配置和资产
design-notes/      目标架构蓝图
fixtures/          可执行 reference fixture
oracles/           独立科学与行为判据
reference/legacy/  只读旧仓库快照
docs/tasks/        工作包、依赖和阶段门
```

## 权威顺序

发生冲突时按以下顺序处理：

1. 已接受 ADR 对本仓库的窄实现决策；
2. `design-notes/gnczmkn-architecture-roadmap/` 的目标语义与架构不变量；
3. `specs/` 中已标记 stable 的机器契约；
4. 已通过的 executable fixture、oracle 和自动测试；
5. `docs/handoff/` 的协作与交付规则；
6. `reference/legacy/` 中的旧行为证据。

旧实现出现差异时，需要按缺陷修复、约定统一、显式模型变化、时间语义澄清、浮点差异或无法解释进行分类。无法解释的差异会阻断阶段门。
