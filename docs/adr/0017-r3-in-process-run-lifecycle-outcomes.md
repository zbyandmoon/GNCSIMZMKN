# ADR-0017: R3 in-process run identity, initialization commit and outcomes

- Status: Accepted
- Decision Date: 2026-08-21
- Owner: Repository owner
- Related tasks: R3-LIF-001、R3-DIA-001、R3-TXN-001、R3-YYZ-001
- Architecture references: 06 §3、§7、§19，07 §3、§6～§7，14 §5、§7
- Extended by: ADR-0018 completed-run reset, outcome retention and explicit dispose; ADR-0019 plan-derived cancellation; ADR-0026 retry classification

## Context

R3 已有 Image-backed Session、完整 Continue/Terminal StepTransaction、committed history 和 ObservationSeal。此前 Session 没有正式 run identity，初始化成功与失败只由 lifecycle state 和低成本 `SessionResult` 表达，Terminal 也没有冻结的 RunOutcome。当前 REF-YYZ consumer 需要一条可直接运行、可失败并可拒绝重入的进程内主线。

本切片不引入 reset、restore、跨进程协议或持久化结果格式。当前初态仍由冻结 Image 提供，因此 RunBinding 只需精确绑定已有 Image/plan identity。

## Decision

1. `RunId` 是调用方提供的 opaque owned value。Kernel 不生成 UUID，不维护全局计数器、registry 或数据库。
2. 当前 `RunBinding` 精确携带 Image fingerprint、plan id、mission id、source semantic hash 和 descriptor semantic hash。任一字段不匹配或 RunId 为空时，初始化在 active run 发布前失败。
3. 初始化请求先形成 pending attempt。Image 预检、prepared/runtime object 物化、initial state 和 evaluator history store 全部成功后，`InitializationCommit` 才公开 RunId、binding、`run_sequence=0`、`epoch=0` 和初始 tick。提交前失败会逆序清理资源、保留初始 epoch/tick、进入统一 `Failed`，并通过 `InitializationOutcome` 与最小 `RunOutcome` 标明失败阶段。
4. `execute_step()` 是唯一 branch-aware 物理执行入口。它从同一 `SessionResult` 失败事实派生 `StepOutcome` 和 `RuntimeDiagnostic`；Continue、Terminal 与提交前失败分别形成 `Committed`、`Terminated` 与 `Failed`。
5. 当前 `RuntimeDiagnostic` 保存稳定 code、stage、public operation、source kind/field/numeric conformance handle、typed subject kind/reference、RunId 与显式 presence、tick/base epoch 与显式 simulation-context presence、typed cause code/ref、validity effect、disposition、message key 和静态 detail。`create_session`、initialize/reset/checkpoint/restore、`execute_step`、`run_to_terminal`、`submit_command` 与 `dispose` 的失败值都携带同一 envelope；execution failure 的 validity 为 `Invalid`，run-start commit 前失败为 `Unknown`。
6. `run_to_terminal()` 只循环调用 `execute_step()`。Terminal commit 完成当前空 finalization set 后冻结 `Completed + Valid` RunOutcome；执行失败冻结 `Failed + Invalid` RunOutcome。空 finalization set 的完成状态为 `Succeeded`。
7. RunOutcome 保存 Run/Image/plan identity、最终状态与 validity、初末 tick/epoch、committed step count、terminal/result availability、primary/related diagnostics 和 finalization status。形成后保持 immutable；后续非法调用只返回 lifecycle rejection。
8. `EvidenceValidity` 是 Contracts 拥有的共享值枚举。本 ADR 只承诺进程内 C++ value semantics，不定义 serializer、wire ABI、Artifact 或 Manifest 格式。

## Consequences

- REF-YYZ 现在具有 `Created → InitializationCommit → Continue → Continue → Terminal → Completed RunOutcome` 的单一正式路径。
- 初始化和执行失败均保留第一原因；cleanup 或重入拒绝不会覆盖已冻结 outcome。
- 两个 Session 可以共享 immutable Image/provider，同时各自拥有 RunId、state、history、seal、diagnostic 和 outcome。
- Public failure attribution is allocation-safe and uses only runtime API fields or numeric Image conformance handles. A caller can distinguish the operation, source and typed subject without parsing detail text or inspecting package identities.
- Cancellation remains a normal control outcome with a stable `CancellationReason` and no `RuntimeDiagnostic`, as required by ADR-0019.
- Completed-run reset、历史 outcome ownership 与 explicit dispose 已由 ADR-0018 补充；cancellation、command/event、checkpoint/branch restore 和 fixture-specific activation 也已有后续窄切片。active-run truncating reset、完整 RunResource hooks、持久化 evidence 和完整 DiagnosticPolicy pipeline 继续由后续 R3 切片交付。
- Generic rendering, a diagnostic store, durable diagnostic artifacts and configurable policy remain deferred until a direct consumer exists.

## Alternatives considered

- Kernel 自动生成 RunId：会引入全局身份服务或进程局部计数语义，当前 consumer 无此需要。
- 保留无参 initialize：调用方无法在 attempt 前提供正式 identity，也会留下两条初始化语义。
- 另建 YYZ runner：会复制 StepTransaction 和积分循环，导致 state/history/seal 与 outcome 可能漂移。
- 通过序列化快照形成 outcome：当前只有进程内 consumer，引入格式和 hash 缺少直接需求。

## Verification

- `r3.kernel-session-materialization.probe`
- `r3.kernel-opening-boundary.probe`
- `r3.kernel-step-transaction.probe`
- `r3.kernel-command-event.probe`
- `r3.kernel-stuck-actuator.probe`
- `r3.kernel-two-entity-causal.probe`
- `r3.kernel-inactive-child-activation.probe`
- `ORACLE-YYZ-MISSION-COMPOSITION-001`

## Supersession rule

Completed-run multi-run Session 已按 ADR-0018 扩展。branch restore、active-run truncation、跨进程 transport 或持久化 Artifact 的真实 consumer 需要改变 identity、commit visibility 或 outcome ownership 时，再评审对应决定。
