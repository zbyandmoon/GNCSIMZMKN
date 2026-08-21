# contracts

职责：领域量、frame、time、quality、diagnostic、outcome、artifact 和跨域 DTO。

允许依赖：foundation。

禁止依赖：具体模型、Compiler、Kernel、Workflow、前端和文件编码。

当前已实现：`sample_context.hpp` 提供真实 YYZ/CAVH consumer 所需的 provisional frame identity、clock domain、simulation instant、half-open validity 和 data quality。`outcome.hpp` 提供 Observation、Outcome 共用的 `EvidenceValidity` 值语义。`execution_semantics.hpp` 拥有跨 package/compiler 共用的 temporal、execution-obligation 与 closure-strategy 语义；`execution_plan_image.hpp` 拥有 Kernel 后续可消费的 process-local immutable Image/handle/layout primitives。`packages/yyz-rigid-step` 直接使用这些类型校验一步输入的 sample/effective time；长期 wire format、跨进程 ABI、serializer、兼容矩阵和迁移链仍未承诺。
