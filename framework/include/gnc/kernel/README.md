# kernel

职责：Session lifecycle、compiled regions、state/output/control stores、CycleFrame、StepTransaction、integration scope、commit 和 backend execution。

允许依赖：foundation、contracts，以及后续抽出的 plan-runtime contract。

禁止依赖：Compiler 实现、Mission Source、Catalog lookup、具体领域 package、Workflow、文件 sink 和前端。

G3 已通过。当前 R3 Kernel 只消费 immutable、process-local `ExecutionPlanImage` 与显式 materialization provider：初始化先验证 Image 结构和 typed materializer identity，再按 lifecycle handle 创建 prepared artifact、Runtime Cell、committed/candidate state store 与 evaluator history。bounded `CycleFrame` 按 callsite input 和 writer token 限制读写，frame close 会逆序销毁暂存值并令旧 view 失效；统一 `execute_step()` 已沿 Image region/DAG、IntegrationScope 与 TransactionPlan 执行两次 fixed-step RK4 Continue 和一次 Terminal instant commit，提交 committed history、ObservationSeal、terminal result 与冻结 RunOutcome。

`Completed` Session 可用调用方提供的新 Session-local unique RunId 和 exact RunBinding 形成 `ResetCommit`：sequence 与 epoch 各增加一，tick 回到 Image initial tick，mutable state 由既有 initial-state builders 重建，history、seal、result、journal 和 step count 清空；Image、provider、PreparedModel 与 Runtime Cell 继续复用。历史 RunOutcome 可按 sequence 查询且只读地址稳定；reset 提交前失败保留全部既有 committed evidence，并冻结独立的 `Reset + Failed + Unknown` attempt outcome。`dispose()` 从 `Created`、`Completed` 或 `Failed` 逆序清理一次，释放 materialized resources，同时保留 Image、最后 committed RunId 与冻结 outcome 查询。checkpoint、branch restore、active-run truncating reset 与完整 RunResource hooks 仍待后续切片。

Kernel 不恢复 package 类型、不调用 `std::any_cast`、不读取 source、不查询 Catalog，也不选择 implementation；package/generated adapter 负责把 exact typed Image entry 组装为固定 materializer 和 invocation table。
