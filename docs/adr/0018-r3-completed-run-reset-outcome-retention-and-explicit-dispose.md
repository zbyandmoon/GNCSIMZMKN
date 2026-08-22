# ADR-0018: R3 completed-run reset, outcome retention and explicit dispose

- Status: Accepted
- Date: 2026-08-21
- Owner: Repository owner
- Related tasks: R3-LIF-001, R3-DIA-001
- Architecture references: 06 §3, §4, §7, §14, §19; 07 §3, §6, §7, §14; 14 §5, §7
- Extended by: ADR-0019 plan-derived in-process cancellation and safe-point sampling

## Context

ADR-0017 已闭合单条 REF-YYZ run 的调用方 RunId、InitializationCommit、权威 step 路径和冻结 RunOutcome。当前真实 consumer 需要在同一 Session 内从已完成 run 启动下一条 run，并在资源生命周期结束时显式释放 Session-owned materialization。该切片继续复用冻结 Image、既有 initial-state builders 和同一 `execute_step()` 路径。

## Decision

1. `reset(ResetRequest)` 当前只接受 `Completed` Session。`Created`、`Initialized`、`Failed` 与 `Disposed` 返回 lifecycle rejection；活动 run 截断不在本切片内。
2. `ResetRequest` 携带调用方提供的新非空 opaque `RunId` 与当前 Image 的 exact `RunBinding`。RunId 在单个 Session 的既有 run attempt 和 committed run 范围内唯一；Kernel 不维护跨 Session identity registry。
3. 成功的 `ResetCommit` 将 `run_sequence` 增加一、将 committed state epoch 增加一，并把 tick 恢复为 Image initial tick。全部 committed/candidate mutable state 由既有 Image initial-state builders 重建，所有 state-block epoch 与新 epoch 对齐。
4. Runtime Cell 只有在 Image 中恰好声明一次 `Resettable` lifecycle capability 时才允许跨 ResetCommit 复用。该 capability 表示 cell 不持有跨 run 变化的模型状态；需要重建的 run-local mutable state 全部位于 Image 声明的 state/history/seal/control stores。Kernel 在任何 reset staging 或 committed mutation 前逐组件验证，缺失时以 `ResetCapabilityMissing` 进入 `Failed`，并完整保留旧 run 证据。
5. `ResetCommit` 清空 evaluator history、sealed boundary、terminal result、step journal 和本 run committed-step count。immutable Image、provider、PreparedModel 与通过 capability 验证的 Runtime Cell 保留，新 RunId/RunBinding 成为 active run identity。
6. 每条 RunOutcome 由 Session 独立拥有。按 sequence 返回的只读指针在 Session 对象销毁前保持稳定；新 reset run 尚未冻结时，`run_outcome()` 返回空。通用 run-start 字段使用 `Initialize | Reset | RestoreBranch` 与 `run_start_committed`。
7. reset 所需 outcome storage、历史容器和新 mutable state 在 commit 前完成 staging。任何 request、capability、重建、验证、分配或最终预检失败都会进入 `Failed`，保留既有 committed sequence/state/epoch/tick/history/seal 和历史 outcome，并冻结 `Reset + run_start_committed=false + Failed + Unknown` attempt outcome。InitializationCommit 或 ResetCommit 前失败的 finalization status 固定为 `NotStarted`；run-start commit 后的执行失败与正常 Terminal completion 在当前空 finalization set 下固定为 `Succeeded`。失败冻结路径不分配。
8. `dispose()` 接受 `Created`、`Completed` 与 `Failed`，进入 `Disposed` 并逆序清理一次。ADR-0019 进一步允许 `Cancelled` 进入同一路径。`Initialized` 返回 lifecycle rejection。清理释放 PreparedModel、Runtime Cell、state/candidate/frame storage、history 与 seal，同时保留 Image identity、最后 committed RunId 和冻结 outcome 查询，直至 Session 对象销毁。
9. checkpoint、branch restore 与 active-run truncating reset 继续开放。

## Consequences

- 同一 REF-YYZ Session 可以形成 sequence 0、1、2 及后续 run，所有 run 继续经过权威 Continue、Continue、Terminal 路径。
- reset commit 具有单一 no-fail publication 区；提交前故障不会改动已有模型状态或证据。
- 缺少 `Resettable` 的合法 Image 会在 reset publication 前 fail closed；capability 经 package descriptor、Compiler 和 Image fingerprint 保真传递。
- historical outcome ownership 与 materialized resource ownership 分离，显式 dispose 可以释放运行资源并继续提供只读身份与 outcome。

## Executable evidence

- `r3.kernel-step-transaction.probe` 覆盖三条完整 run、exact scientific/sealed payload/result replay、稳定 outcome 指针、真实缺失 `Resettable` Image、request/rebuild/copy/replace/validation/final-precheck 故障、commit 前 `NotStarted`、commit 后 `Succeeded` 及 Created/Completed/Cancelled/execution-Failed dispose。
- `r3.kernel-session-materialization.probe` 覆盖 outcome/history staging 分配故障、失败冻结后的零分配、initialization-Failed dispose 和析构不重复清理。

## Supersession rule

checkpoint、branch restore 或活动 run 截断需要改变 reset commit、outcome ownership 或 dispose 可见性时，新增后继 ADR。
