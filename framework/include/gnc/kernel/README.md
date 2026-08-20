# kernel

职责：Session lifecycle、compiled regions、state/output/control stores、CycleFrame、StepTransaction、integration scope、commit 和 backend execution。

允许依赖：foundation、contracts，以及后续抽出的 plan-runtime contract。

禁止依赖：Compiler 实现、Mission Source、Catalog lookup、具体领域 package、Workflow、文件 sink 和前端。

R2 只在 Contracts 中形成 immutable、process-local `ExecutionPlanImage`，不创建 Kernel object。当前 review Image 已 exact-link package-owned typed RuntimeCellFactory、state/slot codec 与全部直接数字依赖 handle，并区分 Query caller-local return、Closure held-interval writer及RuntimeComponent output writer。按storage class划分的extent、fixed-step RK4 policy、transaction branches和preparation lifecycle也已冻结。目标 R3 在 G3 通过后由package/generated composition沿这些handle物化PreparedModel/Bound objects、workspace instance、RuntimeCell、state/output stores与compiled region executor，并在Session内执行projection/query/closure/component/derivative、暂存candidate和原子commit；Kernel不重新读取source、查询Catalog、选择package implementation，或用领域/model/type switch补齐静态选择。
