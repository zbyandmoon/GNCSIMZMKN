# kernel

职责：Session lifecycle、compiled regions、state/output/control stores、CycleFrame、StepTransaction、integration scope、commit 和 backend execution。

允许依赖：foundation、contracts，以及后续抽出的 plan-runtime contract。

禁止依赖：Compiler 实现、Mission Source、Catalog lookup、具体领域 package、Workflow、文件 sink 和前端。

G3 已通过。当前 R3 Kernel 只消费 immutable、process-local `ExecutionPlanImage` 与显式 materialization provider：初始化先验证 Image 结构和 typed materializer identity，再按 lifecycle handle 创建 prepared artifact、Runtime Cell、committed/candidate state store 与 evaluator history。bounded `CycleFrame` 按 callsite input 和 writer token 限制读写，frame close 会逆序销毁暂存值并令旧 view 失效；统一 `execute_step()` 已沿 Image region/DAG、IntegrationScope 与 TransactionPlan 执行两次 fixed-step RK4 Continue 和一次 Terminal instant commit，提交 committed history、ObservationSeal、terminal result 与冻结 RunOutcome。

`request_cancel()` 是唯一允许与单一执行 owner 并发的 mutation。它只更新 mutex 保护的 Session-local 单调请求镜像；执行线程在 Image 的 transaction start、boundary/candidate producer 间、最终预检后和 ModelCommit 后采样。commit 前取消销毁 frame/candidate/history/seal staging，保留最近一次 committed evidence，并冻结 `Cancelled + Valid` RunOutcome；Continue commit 后取消保留刚提交的 StepOutcome、epoch/tick/history/seal。Terminal 或执行失败先冻结时保持权威，取消本身不形成 RuntimeDiagnostic。`run_to_terminal()` 用 `Completed | Cancelled | Failed` 直接表达 drive 结果。

`Completed` Session 可用调用方提供的新 Session-local unique RunId 和 exact RunBinding 形成 `ResetCommit`。Kernel 在任何 reset staging 前逐个确认 Runtime Cell 的 `Resettable` capability；缺失时保留旧 committed evidence 并冻结独立的 `Reset + run_start_committed=false + Failed + Unknown` attempt。成功提交令 sequence 与 epoch 各增加一，tick 回到 Image initial tick，mutable state 由既有 initial-state builders 重建，history、seal、result、journal 和 step count 清空；Image、provider、PreparedModel 与 Runtime Cell 继续复用。InitializationCommit/ResetCommit 前失败的 finalization 为 `NotStarted`，run-start commit 后执行失败与正常完成为 `Succeeded`。历史 RunOutcome 可按 sequence 查询且只读地址稳定；`dispose()` 从 `Created`、`Completed`、`Cancelled` 或 `Failed` 逆序清理一次，同时保留 Image、最后 committed RunId 与冻结 outcome 查询。

route-free `Initialized` Session 已支持 process-local immutable checkpoint 与 branch `RestoreCommit`。checkpoint 深拷贝 committed state、evaluator history、plan-declared `HeldLatest` sample 和当前 seal；多个 child Session 可从同一 committed boundary 独立继续。evaluator history 以 Image 声明的固定 depth 保存最近 committed samples，满 depth 后滑动替换最早样本。seal 所需 producer 在 observation tick 跳过执行时，Kernel 只可从 Image 授权的 `HeldLatest` store 注入通过 age/quality/codec 检查的样本。

`r3.kernel-yyz-target-rate.probe` 已消费 source-authored interval `1/5/2/1/4`、全零 offset 和 age `4/1`，完成 tick-31 短跑、双 Session 隔离及两次 3000-tick 确定性运行。该证据标记 `target_conformance/science_verdict_pending`。完整 YYZ 科学输入、真实资产/geodetic mapping、difference report 与 terminal science verdict 继续由 `R3-YYZ-001` 推进。当前保持延后的 Kernel 能力包括 command/event checkpoint participation、pending-command branch identity、active-run truncating reset、完整 RunResource hooks、durable serialization/compatibility 和完整诊断 pipeline。

Kernel 不恢复 package 类型、不调用 `std::any_cast`、不读取 source、不查询 Catalog，也不选择 implementation；package/generated adapter 负责把 exact typed Image entry 组装为固定 materializer 和 invocation table。
