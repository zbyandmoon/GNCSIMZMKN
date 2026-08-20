# model_sdk

职责：ModelDefinition、Algorithm six-piece、RuntimeCellRecipe、State/Port/Telemetry schema、execution obligations 和 behavior composition。

当前已交付的 descriptor 覆盖真实 `PureQuery`、`Closure`、stateless `RuntimeComponent`、RigidBody/Mass state owner 与 AlgorithmKernel consumer：package 提供 static model/algorithm/port/preparation、state schema、initial builder、projection/evolution、obligation entry、schedule 与 lifecycle 描述，R2 Compiler 以只读方式消费。`RuntimeComponent` 是封闭 execution-form tag，只有该 form 可以携带 runtime recipe/profile/state/obligation facts；PureQuery/Closure 继续只携带 preparation、request/result contract、workspace 与 exact entry facts。

允许依赖：foundation、contracts。

禁止依赖：Compiler、Kernel、Session、Artifact Store、adapter 和具体项目。

当前已交付的双 consumer 最小能力：

- `model_metadata.hpp`：immutable definition/prepared metadata、封闭 execution form 和 typed prepare failure；现有 PreparedModel 产品使用 PureQuery/Closure，RuntimeComponent tag 由静态 Catalog descriptor 消费。
- `algorithm_evaluation.hpp`：call-local formal output 与 telemetry 分离；YYZ/CAVH 的无状态 kernel 均直接返回该类型。
- `static_descriptor.hpp`：冻结 execution form、typed ports、state ownership、obligation、phase、schedule、temporal relation 与 lifecycle 的 package-owned 静态语义。
- `static_implementation.hpp`：package 向 composition root 提供 exact process-local entry identity/version/signature、type-preserving callable reference和 state layout `sizeof/alignof`；Compiler 只消费该通用表，不依赖具体 package。

最小 REF-YYZ 图的 RuntimeComponent/state descriptor 已能进入 R2 planning/linking，Mass 的既有数值策略也由 canonical Definition/config提供。公共 `RuntimeCellFactoryContext` 冻结 `RuntimeInstanceId` 与 immutable resource-plan view；七个 package-specific typed RuntimeCellFactory、两个 state codec 与真实 stored-value codec 分别提供 descriptor identity、exact typed implementation entry、call shape/type witness 和 Image handle。RuntimeComponent Image 直接保存 provider preparation/plan、state、input/output/writer、invocation/callsite、interval model、integration/transaction 与 evaluator history handle。Query 的 caller-local return没有writer/binder，Closure只有一个held-interval writer。R2只链接这些静态接缝，R3由package composition恢复并调用。实际PreparedModel/Bound objects、workspace instance、Session-local RuntimeCell、per-session state、registry、serializer与StateFragment runtime仍未实现。R1的当前独立求值路径不依赖Mission、Session、文件系统或logger。
