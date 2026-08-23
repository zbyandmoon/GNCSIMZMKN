# 路线 R3–R5｜新内核硬切换、研究证据与纵向工作流

[上一分册：R0–R2](r0-r2-foundations.md) · [返回路线总览](../11-roadmap-overview.md) · [下一分册：R6–R8](r6-r8-platform-and-frontends.md)

**主线定位**：本分册建设“Execution Plan → ModelCommit/RunOutcome → ArtifactRef/Evidence Graph → Research Workflow”。阶段出口是一条通过科学校核、失败注入和报告复现的真实研究闭环。

## 1. 阶段目标

R3 消费 R2 已证明、已链接的 immutable `ExecutionPlanImage`，把其中 regions/obligations、typed entries、state layouts、integration scopes 与 transaction groups 物化为 Model Authority 的封闭 Execution Algebra。R3 创建 PreparedModel/Bound handles、workspace、RuntimeCell、per-session stores 与 scheduler，并首次调用 entry、积分、暂存 candidate 和原子提交；它不从 source/Catalog 重新发现依赖或选择实现。YYZ 6DoF slice 用于证明开放 Model Graph 可以降级到通用算子、Kernel 无领域/RuntimeCellProfile 分支并完成硬切换。R4 建立 Artifact Authority 与 Evidence Firewall。R5 建立 Operation + Artifact 两域的 Workflow 操作闭环，并用真实气动—控制—仿真—报告链证明外部研究能力可以在不改变单次 Session 语义的条件下增长。

阶段顺序严格：

```text
new Session transaction
-> YYZ vertical slice
-> scientific difference approval
-> runner cutover
-> legacy deletion
-> Artifact evidence
-> research workflow
```

G6 前暂停 Python/LLM/GUI 对内部对象的绑定。

## 2. R3：新 Session、YYZ slice 与硬切换

### 2.1 R3.1 Session lifecycle skeleton

建立：

- `CreateSession(image)` 空 Created Session、`InitializeSession(handle, RunBinding)` 首 run 与 `RestoreSession(handle, CheckpointRef)` branch run API；
- SessionState reducer；
- LifecycleCoordinator；
- InstanceStore；
- model-prepare、instance、session-resource、run-open、run-finalize/lease-close、dispose journals；
- Diagnostic channel；
- RunOutcome builder；
- cancel token 与 safe points。

先用 fixture cells 验证：

- create/prepare/init/run/finalize、reset 与 branch restore；
- 每阶段 failure unwind；
- cancel before/during run；
- finalization failure 不覆盖 primary，且 RunFinalizeHook 失败后 lease 仍关闭；
- run commit 前的 open failure 只 rollback/close lease，不调用 RunFinalizeHook；
- 同一 Session 的相邻 run 拥有独立 RunId/RunOutcome，branch restore 使用 checkpoint binding 且拒绝 override；
- 第二个 Session 与失败 Session 隔离。

新 Session 不 include 或持有 Simulator。

### 2.2 R3.2 `CommittedStateStore`、control stores 与 compiled handles

实现：

- typed StateBlockHandle；
- CommittedStateStore；
- CommittedOutputStore；
- 校验并消费 Image 已冻结的 StateSchema/layout identity 与 process-local size/alignment；
- StateCodecEntry 与 per-Session aligned committed boxes；
- 调用 Image 已链接的 initial-state builder；
- full-block StateReplacement 形式的 InstantPatch/IntervalCandidate buffers；
- CommittedCommandLedger、SessionCommandQueue 与 EventQueue control stores；
- checkpoint-ready serialization hooks；
- invariant validation。

测试：

- owner-only write；
- stale/wrong schema handle；
- unknown field failure；
- shared PreparedModel + isolated State；
- RNG cursor reset；
- ParameterState/SourceRuntimeState reset 与 checkpoint；
- EntityLifecycleState/topology revision reset、checkpoint 与 commit；
- command ledger sequence、未消费 due command 与 application receipt rollback；
- reset 后 ledger sequence/queue/event 清零，旧 command 不跨 run；branch restore 从 checkpoint ledger/queue 基线继续；
- clone/validate 全部位于 commit 前，ModelCommit 只执行 noexcept swap；
- instant predecessor 与 interval candidate chain 校验；
- atomic buffer swap。

### 2.3 R3.3 CycleFrame 与 compiled region executor

实现：

- compiled typed slots；
- Image 编译的 SlotHeader/SlotCodecEntry/offset/alignment/writer token；
- per-StepTransaction bounded aligned frame arena；
- held sample injection；
- sample/effective time、sequence、quality/freshness；
- InputBundleView；
- Publish/Boundary DAG/Integration/Commit/PostCommit region executor；
- multi-rate interval/offset；
- event/command due sets；
- stable independent-node order。

测试：

- current-cycle chain；
- previous/held relation；
- skipped producer；
- max-age violation；
- priority changes only independent order；
- display name/registration independence；
- no runtime string slot lookup；
- unbounded dynamic payload compile failure；
- frame span 在 transaction 结束后不可访问，held output 必须深拷贝。

### 2.4 R3.4 StepTransaction

实现 14 的时间线：

```text
open state_epoch e / tick k
-> publish projection
-> inject commands/events/held samples
-> component delta evaluation
-> build/validate t_k observation draft
-> termination
-> terminal: finalize draft, reserve critical buffer, instant ModelCommit + ObservationSeal
   or continue: interval/continuous candidates, finalize/reserve, state_epoch e+1 / tick k+1 ModelCommit + ObservationSeal
-> post-commit publication
```

StepOutcome 记录 base/committed epoch、phase reached、delta set、termination、integration、events、diagnostics 与 observation ref。

必须覆盖的 rollback：

- component kernel returns failure；
- illegal delta owner/field；
- transition reducer failure；
- termination evaluator failure；
- closure query failure；
- RK stage failure；
- candidate invariant failure；
- cancellation at each safe point；分别断言 ModelCommit 前 rollback、ModelCommit 后保留 committed step、terminal 后剩余 queued command 通过 CommandLedgerCommit supersede。

### 2.5 R3.5 Command/Event/Behavior/`DecisionAuthority` integration

- command queue/target `DecisionAuthority`/effective time/expiry/idempotency；
- submission schema/target/`PermissionGrant`/capacity 形成 CommandLedgerCommit 与 CommandSubmissionOutcome，只推进 ledger sequence；
- safe point expiry/supersession 形成 CommandLedgerCommit 与 CommandLedgerMaintenanceReceipt；
- owner 在 due tick staged CommandApplicationReceipt，并只随 ModelCommit 提交；
- failed step 保留 due command 未消费并丢弃 staged application receipt；
- event delivery plan；
- local guidance mode mechanism fixture，确认无独立 RuntimeInstanceId；
- FlightPhaseDecisionCell fixture；
- VehicleConfigurationDecisionCell fixture；
- perturbation/fault typed command、owner acceptance/rejection 与 application receipt；
- predeclared entity activation command 与 topology revision；
- transition event/journal/observation；
- terminal transition branch。

验证 embedded mechanism state 随宿主 owner replacement 提交、`DecisionAuthority` 唯一、behavior transition 与新 output 同步回滚、configuration/topology revision 一致，以及 submission receipt 无法冒充 model application evidence。模拟故障 activation 属于 owner 的模型提交；非法 payload 才形成 command rejection。Kernel call table 只包含 compiled obligations，不出现 mechanism/RuntimeCellProfile/fault/entity-type dispatch。

### 2.6 R3.6 Continuous Coordinator 与 Frozen closure

首条 slice 使用明确 `FrozenIntervalClosure`：

- truth/physical response 在 t_k 求值；
- FormInput 对 `[t_k,t_{k+1}]` hold；
- all continuous scopes 从同一 committed epoch 产生 candidate；
- candidate shape/finite/invariant checks；
- integration stats；
- continuous candidates 与 discrete interval state 一起 commit。

目标 integrator API 消费 pure derivative/closure kernel，不调用 RuntimeComponent provider。

验证：RK order、independent systems sync、group membership、NaN、state layout、terminal no advance。

### 2.7 R3.7 YYZ slice A：contracts 与 state/form

先物化并执行 R2 已冻结的最小 YYZ RigidBody/Mass schema、initial builder、committed projection、frozen-form 与 derivative entries；随后沿同一边界扩展目标 YYZ slice：

- entity-scoped truth projector 与 plan-local selector view；
- R3 实际 Prepared Environment/Aero/Closure 与 Bound invocation handles；
- 超出当前 uniform-environment qualification slice 的 Earth/Atmosphere/Gravity/Wind prepared query models；
- static perturbation Definition；
- minimal Observation projector；
- no-force/no-control fixture mission。

目标：编译并运行 free/known-force rigid-body reference，无旧 provider interface。

### 2.8 R3.8 YYZ slice B：input 与 navigation

- IMU/SatNav/AirData algorithms；
- explicit bias/noise/fault State 与 RNG cursor；
- Navigation Definition/State/Kernel/Telemetry；
- sample/held/freshness semantics；
- no OnboardState copy component。

目标：多速率 measurement/navigation 输出与 independent sample-time oracle 一致。

### 2.9 R3.9 YYZ slice C：behavior composition、guidance、control

- FlightPhaseDecisionCell + transition protocol mechanism；
- Guidance six-piece、RuntimeCellRecipe 与 tagged command；
- local guidance phase mechanism/StateFragment；
- Controller six-piece、anti-windup/limiter/law-selection mechanisms；
- ControlAllocator；
- typed Guidance/Control/Actuator command edges。

目标：process Boundary DAG 无手工 priority dependency；新增局部 mechanism 只改 package recipe；transition、hold 和 command age 可观察；Kernel 与 Compiler core 不出现 guidance/controller 专用分支。

### 2.10 R3.10 YYZ slice D：configuration 与 physical models

- VehicleConfigurationDecisionCell；
- Actuator State/Kernel；
- Actuator `FaultStateFragment` 与 stuck/degraded typed command reducer；
- Propulsion State/Kernel/MassFlowInterval；
- Mass State/projection/evolution/jump reducer；
- PreparedAeroModel/PureQuery；
- configuration coverage matrix。

目标：configuration、mass、propulsion、actuator/aero 使用同一 `configuration_revision` 和 committed `state_epoch`；ActuatorCommand 携带 `basis_configuration_revision`，每条可达 transition 都有 RejectOldCommand、Neutralize 或 TypedRemap 策略并产生 `ActuatorCommandDisposition`；旧 `enabled_phase_name` 与 output provider path 消失。

### 2.11 R3.11 YYZ slice E：closure 与 evaluation

- pure ForceMomentClosureKernel；
- Frozen closure sample/telemetry；
- `TerminationDecision` / `Evaluator` contracts；
- terminal observation；
- key metrics；
- complete RunOutcome。

目标：全链闭环运行，query call count 不改变 physical result/observation。

### 2.12 R3.12 扩展压力运行 fixture

先为 `PublishCommitted/InvokeCompiled/AdvanceCandidate/Stage/Validate/Commit/SealEvidence/EffectAfterCommit` 建立算子级 conformance matrix，覆盖成功、拒绝、失败、取消与 commit 前后边界。随后在 YYZ 物理链之外建立三个小型组合 fixture：

1. 两实体 fixture：A/B 各自发布 entity-scoped truth；relative geometry、模拟 sensor 和 link message 通过三条独立 plan route 运行；
2. 故障因果 fixture：scheduled stuck command 经 Actuator `FaultStateFragment`、surface output、简化 aero/rigid-body/contact 与 `Evaluator` 形成 impact termination；
3. 已知分离 fixture：inactive child 通过完整 parent-to-child mapping 激活，parent change、child initial state、relationship 和 topology revision 原子提交，child 从下一 Publish Region 可见。

这些 fixture 用来证明现有 obligations、StepTransaction、CommittedStateStore 和 Observation 语义能够承载多实体、故障和已知拓扑变化。它们不要求在 R3 完成完整空战、起降或星座产品模型。

### 2.13 R3.13 scientific difference report

按 R0 policy 对比：

- initial truth；
- measurements/navigation；
- guidance/control commands；
- actuator/propulsion/mass/aero responses；
- closure forces/moments；
- state trajectory/energy/mass；
- phase/configuration events；
- termination time/metrics；
- dt convergence。

每项差异分类、引用 oracle/ADR/model id。无法解释项阻断切换。

2026-08-23 的 `R3-YYZ-001` canonical 00A 切片已对 initial truth、cadence/held timing、opening air-data/三线性 coefficient lookup、严格气动域、双 3000-interval product trajectory、terminal outcome 与 determinism 形成产品/独立 reference comparison。冻结的 R0 气动资产在 opening Mach `0.6176470588235294` 继续 fail closed；owner 授权的 synthetic multiaffine successor 保留旧域结果，经真实 tick 9 查询触发唯一一次有限域调整后覆盖完整实际查询包线。两次真实 Session 均以 `Completed / duration-complete` 到达 tick 3000，difference report 的 `unresolved_count=0`，verdict 为 `abstract_engineering_target_conformance`。独立 reference 只重算 opening 与资产关系，未复制 3000-step Session/RK4；真实飞行器气动、地理/大气变化、稳定性和独立收敛仍是科学差距。本切片不授权 runner cutover 或阶段门决定。

### 2.14 R3.14 runner/active project 切换

- CLI build/validate/run/list/explain 使用 new Application/Compiler/Session；
- active project Mission 改为新 schema；
- examples/tests 改为 new plan fixtures；
- output 先使用 minimal typed in-memory/CSV adapter；
- current `doc/` 同步新事实。

切换提交后禁止旧路径新增修改。

### 2.15 R3.15 legacy deletion

删除：

- Simulator/ExecutionPhaseManager；
- SimulationNode/DiscreteNode/IDiscreteTask；
- NodeFactory/NodeRegistry/AssemblyContext；
- MissionAssembler/old builder runtime chain；
- old provider interfaces and giant project header；
- IObservable/observable helpers；
- registration macros/bootstrap chain；
- generic callback StateMachine；
- old Mission parser/schema/examples；
- direct Simulator tests。

运行 deletion guards、full necessary build/tests、YYZ evidence comparison。G6 完成后才能进入 R4 正式建设。

### 2.16 R3.16 Candidate closure（硬切换后）

基于新内核增加一个 high-fidelity 6DoF reference：

- candidate rigid-body/actuator/fuel state；
- pure air-data/aero/propulsion/mass query；
- IntegrationScopePlan；
- RK stage telemetry aggregation；
- Frozen/Candidate comparison；
- convergence/performance evidence。

该工作可以在 R4 并行，但不能改写已经确定的 StepTransaction ownership。

### 2.17 R3 退出条件

1. Session normal/terminal/failure/cancel/finalize 全部一致。
2. YYZ complete vertical slice 只使用新 objects/contracts。
3. scientific difference report 无未解释项。
4. runner、tests、active project 只使用新 Compiler/Session。
5. legacy runtime/schema/provider/observation path 删除。
6. deletion/architecture guards 生效。
7. Frozen closure 有时间语义和 convergence evidence。
8. two Sessions share immutable models and isolate mutable state；
9. RuntimeCellProfile/mechanisms 均已降级为 obligations/callsites，Kernel 无相关类型 switch；
10. YYZ slice 的修改范围与 02 压力表声明的 stable untouched areas 一致。
11. 两实体 selector、舵机卡死因果链与 inactive child activation fixture 在同一 generic Kernel 上通过。
12. 每个 Image callsite 都能追到 Model Graph grammar element、PlanProofRecord 与 Execution Algebra operator，Kernel 中无产品专用执行入口。

## 3. R4：Typed Observation、Artifact 与研究证据

### 3.1 R4.1 ObservationBatch

Batch 来源：

- committed state projector；
- CycleFrame output；
- invocation telemetry；
- StepJournal event/diagnostic/decision。

字段有 FieldId、schema/version、dtype/shape、unit/frame、sample/effective time、quality/source、EntityId 和 topology revision。Batch 关联 StepOutcome/committed epoch。

### 3.2 R4.2 Observation pipeline

- compiled projectors；
- preallocated batch buffers；
- rate/decimation；
- critical/noncritical field；
- backpressure；
- sink outcomes；
- sink-independent dataset schema 与 per-sink EncodingPlan；
- no model re-query。

CSV 与 MATLAB `.mat` 实现为首对 Dataset Sink。两者消费相同 ObservationBatch/schema，并分别记录 FieldId 到 column/variable 的确定映射、codec version、append/chunk 能力和 close outcome。HDF5、Parquet、数据库与实时流沿同一接口追加。新列结构按 typed schema 设计，无旧列兼容目标。

### 3.3 R4.3 Artifact identity/store

建立本地优先：

- ArtifactDescriptor/ArtifactRef；
- schema/hash/size/content URI；
- payload encoding/codec version/schema mapping ref；
- staging/atomic commit/partial marker；
- LineageEdge；
- local directory store + JSON index；
- integrity verification。

首版无需数据库和远程服务。

### 3.4 R4.4 Run Manifest

必须包含：

- source set/source map；
- Plan hash 与 explain summary；
- model graph、execution core、observation plan、encoding plan 与 descriptor 分层 hash；
- package/model/algorithm/contract lock；
- asset hash；
- math/quaternion/numerical/closure policy；
- seed/RNG streams；
- Session/Step/Run outcome；
- diagnostics/waivers；
- observation/sink durability；
- artifacts/lineage；
- code/toolchain/platform identity。

### 3.5 R4.5 structured metrics

MetricDefinition/MetricResult：

- algorithm/version；
- inputs/units；
- validity/threshold；
- evidence refs；
- diagnostic refs。

自由文本 summary 由 renderer 消费 metrics/RunOutcome 生成。

### 3.6 R4.6 failure evidence

验证：

- initialization failure still manifests；
- model step failure keeps committed epoch；
- evidence sink failure after model commit；
- disk full/close failure；
- partial artifact quarantine；
- primary failure plus cleanup diagnostics。

### 3.7 R4.7 checkpoint/replay

基于 StateSchema 保存：

- plan hash/epoch/tick；
- all state blocks/modes/configuration/RNG；
- held outputs；
- command ledger sequence、未消费 command queue、application receipts 与 event queue；
- canonical RunBinding values/hash/provenance refs；
- external source cursors、dedup watermarks 与 committed input receipts；
- observation sequence；
- manifest refs。

同 plan restore 在新 Created Session 上使用 checkpoint canonical binding 打开 branch run，拒绝 binding override；校验完成后进入 Paused。新 Session/RunId 与 local run_sequence 从零开始，物理 state_epoch/tick/time origin 保留。Replay 使用 recorded commands/external samples。

restore fixture 还要验证：ledger sequence 从 checkpoint 基线继续；未消费 queue entry 保留 origin identity 并绑定新 RunId；已终结 command 不再入队；RestoreCommit 前 command submission 被拒绝。

### 3.8 R4 退出条件

1. 每次 run 有 RunOutcome/Manifest。
2. observation 无 IObservable/query side effect。
3. state/output/telemetry/event 可追到 descriptor/source。
4. critical sink failure 改变 evidence validity。
5. Artifact atomic/partial/integrity tests 通过。
6. checkpoint/replay 恢复 behavior StateFragments/configuration/RNG/held outputs。
7. CSV/MAT round-trip fixture 产生语义等价 dataset，包含 entity/time/unit/frame/quality 元数据。
8. 增加或切换 Dataset Sink 不改变 `execution_core_hash`、model state 或 Kernel call table。

## 4. R5：首条真实研究工作流

### 4.1 选择原则

首条工作流要覆盖真实研究价值与多种边界。推荐：

```text
raw/normalized aerodynamic data
-> aerodynamic envelope/derivatives
-> trim
-> characteristic quantities c1/c2/b1/b2
-> linear model and loop margins
-> controller parameters
-> closed-loop mission
-> figures/tables/report
```

DATCOM 可作为 raw aero adapter；若工具环境暂缺，先用已存在 normalized aero Artifact 验证后半链。

### 4.2 R5.1 WorkflowDefinition/Plan

- typed TaskDefinition；
- input/output Artifact contracts；
- parameter schema；
- DAG/dependency；
- cache key；
- resource/timeout/cancel/retry；
- approval gate；
- deterministic declaration。

Workflow Compiler 必须把每个 task 降级为 `Admit/Authorize/Reserve/Invoke/Observe/Cancel/Finalize` Operation 操作与 `BeginStage/ProduceOrEncode/ValidateArtifact/CommitWithLineage/PublishRef` Artifact 操作。Workflow Engine 只执行这两组已编译操作；task type、工具名称和报告类型不能形成 scheduler 分支。

### 4.3 R5.2 ToolAdapter

首个 adapter 完整实现：

- materialize typed inputs；
- isolated working directory；
- executable/tool version/license info；
- safe argv/env；
- timeout/cancel；
- stdout/stderr Artifact；
- output validate/ingest；
- Diagnostic/TaskOutcome。

真实 tool qualification 与 CI fake fixture 分开。

### 4.4 R5.3 Aero Artifact

统一 `AeroTableArtifact`：

- axes/units/conventions；
- configuration/ref geometry；
- coefficients/derivatives；
- interpolation/extrapolation policy；
- source/tool/version；
- valid domain/quality；
- uncertainty/notes。

PreparedAeroModel 直接消费该 Artifact contract。

### 4.5 R5.4 Trim/characteristic/margin tasks

每个 task 分成：

- CompiledModelOccurrence/AlgorithmDefinition；
- pure numerical kernel；
- NumericalOutcome；
- Telemetry/intermediate evidence；
- Artifact renderer。

失败位置可定位到 aero domain、solver convergence、linearization conditioning 或 loop definition。

### 4.6 R5.5 Closed-loop evidence

- analysis outputs 生成 controller parameter Artifact；
- Mission Source 引用 ArtifactRef；
- Compiler 锁定 hash/version；
- run manifest 记录 closure/numerical/model policy；
- metrics 验证 margins 与 time-domain result 的关联。

### 4.7 R5.6 Figures and reports

- FigureSpecification；
- MATLAB/Python/Origin adapters；
- ReportSpecification；
- Word/Excel renderer；
- template/version/data binding；
- visual/render verification；
- figure/table/report lineage。

### 4.8 R5.7 CAVH reproduction package

将 R1/R3 的 CAVH decomposition 完成工程化：

- paper metadata/assumptions；
- GlideEnvelope Artifact；
- Eq17/Eq18 verification；
- guidance component/model descriptor；
- closed-loop mission；
- comparison figures/report；
- maturity/valid domain/limitations。

### 4.9 R5 退出条件

1. 一条真实研究 DAG 从输入 Artifact 到 report 全程可追溯。
2. 修改 aero/controller parameter 只重算受影响 tasks。
3. tool/solver/session/sink failure 都有独立 Outcome。
4. report 数字与图可追到 Metric/Observation/Run Manifest。
5. 目标用户可从文档入口独立复现。
6. CAVH 或同等级论文模型证明 algorithm package 结构有效。
7. 每个 workflow node 都能追到 Workflow Graph element、PlanProofRecord、Operation/Artifact operator、TaskOutcome 和 ArtifactCommit。
8. Workflow 调用 Session 时只提交 PlanRef/RunBinding/Command 并消费 RunOutcome/ArtifactRef，无 CommittedStateStore、CycleFrame 或 Runtime Cell 访问。

## 5. R3–R5 风险

| 风险 | 响应 |
| --- | --- |
| YYZ slice 太大 | 按 A–E 子链提交，每条保持 new-only object |
| Frozen closure 改变结果 | 写明 hold convention，做 dt convergence 与 Candidate follow-up |
| transaction 实现复杂 | 先 fixture cells 穷举 terminal/failure，再迁移物理模型 |
| old runtime deletion 延后 | G6 阻断 R4，单独 deletion commit |
| observation 侵入 model | projector 只读 result/journal，architecture guard |
| 数据编码侵入 runtime | Dataset Sink conformance test + `execution_core_hash` 不变断言 |
| Artifact 变平台 | 本地目录 + JSON index 起步 |
| 工具 license 阻塞 CI | fake/golden fixtures，qualification 独立执行 |
| report 自动化耗时 | 先机器可读 Metric/Artifact，再增加模板 renderer |

## 6. R3–R5 总完成定义

1. 唯一新 Session 内核运行 minimal/YYZ missions。
2. 旧 runtime/schema/provider path 删除。
3. step state、embedded behavior、shared configuration、closure 和 failure 语义有 failure-point evidence。
4. observation/manifest/artifact 完成科学证据闭环。
5. 至少一条 aero-control-simulation-report workflow 可复现。
6. 一个复杂论文算法证明 Definition/State/Input/Output/Telemetry/Kernel 拆分可用。
7. R6 可以只依赖 Application Control、Session Handle、Observation/Artifact contracts，无内部兼容层。
8. 多实体 selector、模拟故障物理链与已知 entity activation 已由最小运行 fixture 证明。
9. CSV/MAT 共享 Observation schema，并能在不改变 Session 语义的条件下继续增加数据编码。
10. Model、Operation 与 Artifact 三类 commit 可以分别查询，跨域只通过 typed intent/ref/receipt/Outcome 交接。
