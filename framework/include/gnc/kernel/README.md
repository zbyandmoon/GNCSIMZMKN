# kernel

职责：Session lifecycle、compiled regions、state/output/control stores、CycleFrame、StepTransaction、integration scope、commit 和 backend execution。

允许依赖：foundation、contracts，以及后续抽出的 plan-runtime contract。

禁止依赖：Compiler 实现、Mission Source、Catalog lookup、具体领域 package、Workflow、文件 sink 和前端。

G3 已通过。当前 R3 Kernel 只消费 immutable、process-local `ExecutionPlanImage` 与显式 materialization provider：初始化先验证 Image 结构和 typed materializer identity，再按 lifecycle handle 创建 prepared artifact、Runtime Cell、committed/candidate state store 与 held-output staging。bounded `CycleFrame` 按 callsite input 和 writer token 限制读写，frame close 会逆序销毁暂存值并令旧 view 失效；成功的 tick 0 opening boundary 把 `IntegrationHeld + HoldInterval` 值深拷贝到 Session-local committed output，同时保持 state epoch 与 tick 不变。Kernel 不恢复 package 类型、不调用 `std::any_cast`、不读取 source、不查询 Catalog，也不选择 implementation；package/generated adapter 负责把 exact typed Image entry 组装为固定 materializer 和 invocation table。区间 executor、RK4 stage、transaction commit、terminal publication、reset/checkpoint/restore 仍未实现。
