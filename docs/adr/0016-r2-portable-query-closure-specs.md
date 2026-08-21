# ADR-0016: R2 portable query/closure and static plan-link boundary

- Status: Accepted
- Decision Date: 2026-08-20
- Owner: Repository owner
- Related tasks: R2-BIND-001、R2-PLAN-001、R2-PRF-001、R2-LINK-001
- Architecture references: 04 §15.1、05 Pass 9、05 §10.1～§10.2、12 §5.2、12 §9.1～§9.2、14 §24

## Context

R2 静态编译已经从真实 CAVH/YYZ package descriptors 形成 canonical Mission IR、BindingPlan、TemporalBindingPlan、binding proof 和 query/closure obligations。此前的窄 `ExecutionPlanDescriptor` 只保存 prepare identity 和 obligation endpoint，缺少 query/closure kernel identity、workspace fact 与完整 preparation inputs；本决定补齐这些事实，并把完整静态 planning/linking 明确在 R2。

owner 已决定在 R2 补齐 package-owned RigidBody/Mass StateOwner 静态合同、initial mapping、`PublishProjection`、DerivativeEvaluation、完整 plan/proof/link/image。R3 保留实际对象物化和执行。本决定同时冻结 portable execution inputs 与其后续 R2 planning/linking 边界。

## Decision

1. Contracts 成为 `ClosureStrategy` 的唯一语义 owner，并声明 `FrozenInterval | CandidateState | AlgebraicSolve` 的完整共享枚举。枚举存在不等于当前 Compiler 支持对应执行能力。
2. Model SDK 为 PreparedModel-backed execution forms 增加封闭、互斥的 `StaticPureQueryDescriptor` 与 `StaticClosureDescriptor`。两者保存 exact stable entry id/version 和显式 workspace requirement；Closure 另保存 `ClosureStrategy`。这些字段不保存函数地址、ABI 或 PreparedModel 实例。
3. 当前三个真实 kernel 均无 caller-visible workspace，因此 CAVH GlideEnvelope query、YYZ AerodynamicTable query 和 ForceMomentClosure 都精确声明 `workspace=None`。不为未来可能的 workspace 预留虚构 size/alignment/layout。
4. Catalog 通用验证 execution form 与 query/closure/runtime payload 恰好匹配，并要求 exact prepare/entry identity。当前 Closure slice 只接受产品已验证的 `FrozenInterval + IntervalModel`；CandidateState 与 AlgebraicSolve fail closed。验证不按 CAVH/YYZ model id 写特殊分支。
5. `ExecutionPlanDescriptor` revision 3 保存：
   - 每个 occurrence 的 `PreparedModelPreparationInputs`：stable input id、package/model/form、exact prepare identity、完整 canonical config、已解析 asset-binding refs 和稳定去重的 source refs；
   - PureQuery 的 `QueryExecutionSpecInputs`：preparation-input ref、exact query entry、显式 workspace，以及从已解析 BindingPlan 派生的 consumer-binding facts；
   - Closure 的 `ClosureExecutionSpecInputs`：同级 stable facts，加 exact strategy 与 consumer temporal relation；
   - 每项 consumer-binding fact 的完整 provider/consumer endpoint、contract、scope resolution 和 binding source；
   - 每个 compiled obligation 到对应 query/closure execution input 的稳定引用。
6. consumer-binding facts 只说明 query/closure response 的结果流。普通 AlgorithmKernel consumer不获得 `BoundQuerySet`；closure invocation caller也不从 provider→consumer edge 推导。正式 caller authorization由 R2 的 `QueryPlan`、`ClosurePlan`/`IntegrationScopePlan` 与 link pass根据 package invocation requirement和canonical source binding共同产生。
7. Source refs 聚合 occurrence、subject/scope/placement、configuration root、逐字段配置、asset binding、Catalog execution descriptor 与 consumer binding provenance，并按 URI/path 去重排序。source relocation 改变 provenance，但不改变 semantic hash 或 source-independent dry-run explain。
8. Canonical Mission IR revision 与 `semantic-bytes@2` 保持不变。Entry identity 是 package implementation/plan fact，不进入当前 canonical source semantics。
9. 完整 `QueryPlan`、`ClosurePlan`、静态 invocation authorization、`IntegrationScopePlan`、`TransactionPlan`、`PlanProofIndex` 和 `ExecutionPlanImage` 都属于 R2。R2 可以抽取、公开并链接现有纯函数 entry，前提是 R1 科学结果、浮点调用顺序和 oracle 保持。
10. R3 创建实际 `PreparedModel`、BoundQuery/BoundClosure handles、workspace、RuntimeCell、Session-owned stores 与其他 mutable objects；query、closure、projection、derivative、component entry 和 integrator 的实际调用也从 R3 开始。
11. 完整 RuntimeComponent 图采用 additive programmatic source revision 3、`CompleteCanonicalMissionIr` revision 3、`semantic-bytes@3` 与完整 descriptor revision 6。source semantic hash 覆盖 model/config/asset/port/state/obligation/schedule/temporal/invocation composition facts；prepare/query/closure/runtime entry identity、recipe、workspace、state layout、build fingerprint、函数地址与 source location 均不进入 source semantic hash。既有 `semantic-bytes@2` API 与 reference vector 保持原样。
12. process-local `ExecutionPlanImage` 保存 exact package lock、type-preserving callable reference、entry signature、workspace/layout identity、当前进程 state `sizeof/alignof`、numeric handles、regions、DAG、invocation、integration scope、transaction 与 proof conformance。typed callable 与地址不进入稳定 fingerprint；link 阶段不调用任何 entry。跨进程 ABI、动态 package protocol 与 serializer 不在本决定范围内。

## Consequences

- Descriptor 能在无需运行时重新发现的条件下说明 PureQuery/Closure 的 stable entry、preparation input、workspace fact、strategy、response consumer binding 与静态授权来源。
- `PreparedModelPreparationInputs` 保持窄范围：当前完整路径冻结三个 provider occurrence 的 definition/prepared-artifact identity、exact config/assets/prepare entry，以及 `SessionOwned + InitializeTime + NoSharedCache + FailSessionInitialization` 策略。共享 cache key 与跨 Session 复用策略仍未提供。
- `QueryExecutionSpecInputs`/`ClosureExecutionSpecInputs` 是正式 `QueryPlan`/`ClosurePlan` 的编译输入；R2 plan/link 增加 authorized invocation caller、稳定 slot/layout identity、linked entry、IntegrationScope 与 transaction facts。
- `PlanProofRecord`/`PlanProofIndex` 与 exact linker 在完整静态图上形成；linker 只解析已冻结 identity/layout/authorization，不执行 entry 或物化 runtime object。
- 未闭合 RuntimeComponent source继续 fail closed。完整合法 RuntimeComponent source通过新的完整计划路径生成 Descriptor/Image。
- `PreparedModel`、Bound handle、workspace allocation、RuntimeInstance物化与 Session均归属 R3。

## Alternatives considered

- 只在 obligation 保存非空 kernel 字符串：无法冻结 workspace、strategy、preparation reference 或 response consumer binding，并迫使后续 pass 重查 Catalog。
- 直接生成虚构的完整 `PreparedModelKey`：现有资产事实只证明 source-selected identity preservation；R2 保留诚实 preparation inputs，资产内容与 policy facts齐备后再形成 cache key。
- 在 Descriptor 保存函数地址或 workspace instance：混淆 portable Descriptor、进程内 Image 和 Session bindings 三层 ownership。
- 同时开放 CandidateState/AlgebraicSolve：现有产品只证明 FrozenInterval；提前接受会声明不存在的 closure executor、solver 与 transaction capability。

## Implementation status

当前 complete API 已形成 deterministic descriptor、派生 proof 与 exact-entry Image review artifact，并保持 link 阶段零调用。每个 RuntimeComponent 的 package-specific typed RuntimeCellFactory、RigidBody/Mass state codec 与真实 stored-value codec 均以独立 identity、signature、call shape、C++ type witness 和 Image handle exact-link。已授权 environment/aero query 使用 `CallerLocal` route，没有 CycleFrame result slot 或 writer；FrozenInterval Closure 使用唯一 `HeldInterval` slot/writer，正式 closure output 与 held model 共用一个权威值。Image 还冻结 fixed-step RK4/step/numerical policy、workspace `None/0`、candidate invariant/projection、Continue/Terminal/Failure transaction sets，以及 fail-closed preparation lifecycle。仓库所有者于 2026-08-20 判定 G3 `Passed`。R3 Session 已沿数字 handle 恢复 typed entry/artifact，物化 Session-local prepared objects、Runtime Cells、committed/candidate stores、bounded CycleFrame、最小 StepJournal、committed history 和 sealed boundary storage。窄 IntegrationScope 读取精确授权的 committed state、frame slots、算法与数值 policy；两次 Continue 各自执行四阶段 RK4与独立 mass evolution，再完成 candidate/history/seal 全量预检和 no-fail 原子提交。第三步从 Image 分支事实选择 Terminal；真实 evaluator 只读取其 callsite 绑定的三份 typed chronological history，并写入正式 result slot。Terminal instant ModelCommit 保持 rigid/mass bytes，以 epoch `+1`、tick `+0` 形成 `(3,2)`，ObservationSeal 先于 result seal。held 值仅由 transaction-local frame 保存。Kernel 没有 model-id switch、signature 解析、YYZ 类型恢复或默认数值策略；package/generated adapter 负责 history type 恢复和产品 evaluator 调用。执行期故障进入 `Failed` 并拒绝同一 Session 重试，已提交 state/history/seal 保持上次成功值。

## Executable evidence

- `framework/include/gnc/contracts/execution_semantics.hpp`
- `framework/include/gnc/contracts/execution_plan_image.hpp`
- `framework/include/gnc/model_sdk/static_descriptor.hpp`
- `framework/include/gnc/model_sdk/static_implementation.hpp`
- `framework/include/gnc/model_sdk/runtime_cell_factory.hpp`
- `framework/include/gnc/model_sdk/in_process_codec.hpp`
- `framework/include/gnc/compiler/static_mission_compiler.hpp`
- `framework/include/gnc/compiler/complete_execution_plan.hpp`
- `packages/cavh-formula/include/cavh/formula.hpp`
- `packages/yyz-rigid-step/include/yyz/rigid_step.hpp`
- `tests/compiler_static_plan.cpp`
- `tests/compiler_complete_yyz_plan.cpp`
- `tests/kernel_session_materialization.cpp`
- `tests/kernel_opening_boundary.cpp`
- `tests/kernel_step_transaction.cpp`
- `r2.compiler-static-plan.probe`
- `r2.compiler-complete-yyz-plan.probe`
- `r3.kernel-session-materialization.probe`
- `r3.kernel-opening-boundary.probe`
- `r3.kernel-step-transaction.probe`
