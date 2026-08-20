# kernel

职责：Session lifecycle、compiled regions、state/output/control stores、CycleFrame、StepTransaction、integration scope、commit 和 backend execution。

允许依赖：foundation、contracts，以及后续抽出的 plan-runtime contract。

禁止依赖：Compiler 实现、Mission Source、Catalog lookup、具体领域 package、Workflow、文件 sink 和前端。

G3 已通过。当前 R3 Kernel 只消费 immutable、process-local `ExecutionPlanImage` 与显式 materialization provider：`Created` 不持有 mutable runtime object；`Initialized` 按 lifecycle handle 创建 prepared artifact 与 Runtime Cell，按 storage extent placement-construct slot/state object，并在失败或析构时逆序释放。Kernel 不恢复 package 类型、不调用 `std::any_cast`、不读取 source、不查询 Catalog，也不选择 implementation；package/generated adapter 负责把 exact typed Image entry 组装为固定 materializer。cycle executor、RK4 stage、transaction commit、terminal publication、reset/checkpoint/restore 仍未实现。
