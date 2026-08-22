# AGENTS.md

本仓库是 GNCZMKN 目标架构的全新实现。G0/G1/G2/G3 已通过，当前处于 R3；产品代码已完成 R1 Foundation/Contracts/YYZ/CAVH 模型切片、R2 静态 Compiler，以及 Image-backed formal REF-YYZ run、Completed-run reset、plan-derived cancellation 和 explicit dispose。蓝图描述目标语义，已交付能力只以当前源码、fixture、oracle 和自动测试为准。

## 每次开始

按顺序读取：

1. `README.md`
2. `docs/handoff/r0-execution-state.md`
3. `docs/tasks/backlog.json`
4. 当前任务直接引用的 ADR、架构分册和测试

历史审计、旧分支状态和聊天记录不属于默认上下文。确需追溯时再查询 Git 历史。

## 工作方式

- 仓库所有者保留产品范围、优先级、科学口径、阶段门和发布决定。
- AI 智能体承担分析、实现、测试、文档同步和风险提示；不得充当最终批准人、阶段门签署人或多个虚构的独立责任主体。
- 默认只运行一个实现智能体。只有用户明确要求并行或子智能体时才可以委派。
- 交付优先级依次为：可执行科学或产品切片、直接回归测试、完成该切片所需的最小契约与文档。
- 每个切片必须产生可执行产物，或关闭一个阻碍可执行产物的真实问题。纯治理扩张需要用户明确要求。
- 没有当前 consumer 或已复现回归时，不新增治理 schema、验收回执、角色授权链、报告镜像、commit/hash 锁或大批量 mutation。
- 不为未来阶段预建 manager、callback、plugin runtime、兼容层或抽象扩展点。
- 发现蓝图矛盾时，记录最小问题与影响范围；只有公共语义确需选择时才新增或修订 ADR。

## 当前阶段

- 当前 gate 为 R3。
- R2 已通过 G3；当前已交付冻结 Image 到 Session 的 InitializationCommit、两次 RK4 Continue、Terminal commit、冻结 RunOutcome、Completed-only ResetCommit、plan-derived in-process cancellation、稳定历史 outcome 查询与一次性 dispose。CommandLedger/event queue、checkpoint、branch restore、active-run truncating reset、完整 RunResource hooks 和完整诊断 pipeline 仍待后续 R3 切片。
- 仅 backlog 中进入 `ready` 的 R3 工作已解锁；R4～R8 保持锁定，直到对应 gate 通过。
- 文档中的 `V1` 表示目标范围，不代表当前实现状态。

## 核心边界

- `reference/legacy/` 只读且不参与构建。
- 生产代码不得 include、链接、运行或复制 legacy runtime API。
- 迁入的科学公式、参数和资产需要 provenance、适用域、独立 oracle 和新 model identity。
- Compiler 不依赖具体 package 实现。
- Kernel 不依赖 Compiler、领域 package、文件格式、Workflow 或前端。
- Workflow 不读取 Session 内部 state、CycleFrame 或 Runtime Cell。
- Adapter 只通过 Application API、Artifact 或已批准的稳定 contract 接入。
- 每份 mutable state 只有一个 owner；跨 owner 数据具有 typed contract 和明确时间关系。

## 实现与验证

- C++ 标准为 C++17；构建入口为 CMake presets。
- 新三方依赖、新公共 schema、新 hash/codec、新线程模型和跨进程边界需要窄 ADR。
- 一个变更聚焦一个可运行的纵向切片；测试与实现放在同一交付中。
- C++、CMake、科学计算或执行路径变更运行配置、构建、相关 CTest 和仓库检查。
- Markdown 或任务状态变更运行直接相关检查；共享或合并前运行完整检查。
- repository guard 只保护会影响构建、运行、科学结果、架构依赖或 legacy 隔离的回归。报告措辞、actor 身份、历史 commit 和验收收据不得成为 guard 对象。
- 新中文文档与注释使用 UTF-8，并禁用“先否定前项、再转折肯定后项”的对照句式。

## 任务状态

- `docs/tasks/backlog.json` 是唯一当前任务源。
- `assignee` 表示正在执行的工作主体；任务没有活动执行时保持 `null`。
- `in_progress` 需要 assignee 和可定位的工作产物。
- `done` 需要 deliverables、acceptance 和可执行 evidence 闭合；AI 自评或机器互签不构成完成证据。
- 架构、科学、许可、阶段门和发布选择由对应 owner 提交决定。实现智能体应把问题收敛成最小可选项，避免扩写治理材料。
