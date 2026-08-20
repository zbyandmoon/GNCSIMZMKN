# R0-ARCH-002 架构 Fitness 覆盖与故障设计

- 文档状态：Implemented R0 source-boundary slice
- 实现成熟度：executable；当前 source/CMake 边界可作为直接 fitness evidence
- 任务：`R0-ARCH-002`
- 日期：2026-08-15
- 权威 owner role：Architecture Lead

## 1. 目标与证据边界

本计划把目标蓝图中的 architecture fitness functions 分成三类证据：当前 R0 可由仓库结构证明的规则、需要 R0 治理 artifact 后启用的规则、需要 R1–R5 产品 artifact 后启用的规则。每条规则必须通过真实 source/descriptor/transaction artifact 或与正向 evaluator 相同的 mutation 证明检测能力。

当前产品已具备 R1 PureQuery、Closure 与 stateless AlgorithmKernel；R2 既保留窄静态 `ExecutionPlanDescriptor` qualification 路径，也为最小 REF-YYZ 图形成 RuntimeComponent/state plans、`PlanProofIndex` 和 exact-entry-linked `ExecutionPlanImage` review artifact。package-owned typed RuntimeCellFactory、state/value codec、caller-local/held result route、确定性 storage extent、唯一 writer/reader、fixed-step RK4 policy、transaction branches、preparation lifecycle 与 factory direct handles 已进入 Descriptor/Image，poison tests 证明 link 阶段零调用、地址不进入 fingerprint。R2-PLAN/PRF/LINK 仍等待 owner review，G3 尚未判定；R3 的实际 entry 调用、Session、stores、runtime scheduler、integration execution 与 `StepTransaction` staging/commit仍未形成。对应运行 artifact 交付前，coverage report 使用 `not-applicable-awaiting-artifact` 或 `deferred-by-gate` 表达该状态。

本计划不定义 runtime schema、不增加生产依赖、不改变 module DAG，也不允许扫描器根据类名推断 state owner、temporal relation 或 commit semantics。

## 2. 当前可验证覆盖

| Surface | 当前证据 | 覆盖强度 | 已知缺口 |
| --- | --- | --- | --- |
| 术语与 CapabilityStatus | glossary parser、authority registry、派生 baseline | 强：唯一性、状态、alias、enum authority、hash | 未检查普通 production identifier 与退出词 |
| module DAG | ADR-0003 + CMake target parser + source-boundary evaluator | 强：9 modules、22 physical edges、closure、cycle、production include direction | public/private header seam 随首个真实 consumer 收窄 |
| shared symbol owner | authority registry + authority text | 强：27 个 enum/key/owner identity | 尚无 production definition，可执行 duplicate-definition scan 缺失 |
| Legacy ownership | glossary + 22 条 ownership mapping | 强：唯一 primary owner、consumer、disposition | ownership evidence 不等于 production deletion guard |
| Legacy production dependency | source/CMake inventory + exact path/API evaluator | 强：路径、archive、include、代码 identifier、CMake bracket argument | R3/G6 的全量删除 token 仍按 gate 激活 |
| Kernel/Compiler boundary | CMake closure + normalized C/C++ include graph | 强：直接、相对与 dot-segment include 均受同一 DAG 判定 | 首个 private source root 出现时补 root registration |
| semantic/evolution guards | 无目标 artifact | 未启用 | 需要 maturity、prerequisite 与 mutation 证据 |
| orchestration | CTest + repository verification + Ubuntu/Windows workflow | 强：`r0.source-boundaries`、仓库入口和 Windows PowerShell 5.1 入口闭合 | 无 |

architecture baseline 保持 15 项直接 mutation，覆盖 glossary、term/alias/authority、ownership、DAG/CMake、source hash 和派生 bytes。source-boundary evaluator 另有一个正向矩阵和八个定向反例，直接覆盖 source owner、依赖方向、project/package 边界与 Legacy 禁入。两者共同使用现有 ADR 和 registry，没有复制 terminology/DAG authority。

### 2.1 Candidate responsibility reconciliation

`origin/codex/r0-first-wave@7d7c0a8` 的 27 个名称 / 33 项职责是 comparison evidence，不是新的 authority：

- 22 项 target module 已落在当前 ownership registry 的 primary owner 或 secondary consumer 集合；
- `execution-phase-plan`、`onboard-state-input-view`、`simulation-builder-plan` 共 3 项提出新的物理 owner 细分；
- `guidance-process-recipe` 与 `node-factory-contribution` 共 2 项使用 `packages_user` 逻辑贡献边界；
- `IContinuousGroup`、`IIntegrator`、`ISummaryObserver`、`math_types.hpp`、`SimulationSummary` 共 5 个当前未登记名称承载 6 项职责；
- 33 项职责引用的 target terms 全部已经存在于当前 glossary，因此差异集中在名称注册、owner 粒度和逻辑边界表示，而不是缺少 canonical vocabulary。

Accepted `RECON-DEC-006` / `RECON-DEC-007` 已固定当前九模块 DAG、22 条 Legacy ownership 和 registry v1 schema。准备表可以使用 `packages/`、`user/`、`apps/`、composition root 等 source-rule label；物理 module 提升、responsibility overlay 或新 Legacy mapping 仍需满足已接受的 superseding/migration 规则。

## 3. 当前守卫层次

```text
authoritative ADR / registry / gate vocabulary
               |
               v
deterministic policy projection
               |
      +--------+---------+
      |                  |
      v                  v
CMake/source inventory   artifact inventory by maturity
      |                  |
      +--------+---------+
               v
findings + coverage states + exact exceptions
               |
               v
positive repository scan + same-evaluator mutations
```

### 3.1 L0｜权威与 maturity

- 继续由 ADR-0003、ADR-0005、glossary 和 authority registry 提供唯一语义来源；
- policy 是确定性投影，不复制术语定义或 module graph；
- policy enforcement state 限定为 `enforced`、`not-applicable-awaiting-artifact`、`deferred-by-gate`，evaluation result 独立记录 `passed`、`failed`、`not-run`；
- rule 从 deferred/awaiting 升为 enforced 时必须同时增加正向目标、负向 mutation、owner task 和 evidence ref；
- public schema、module graph 或 firewall 变化先走 ADR。

### 3.2 L1｜物理依赖与禁入规则

- CMake edge 继续使用当前 parser；production CMake 由 source-boundary evaluator 额外检查 Legacy tree/archive 禁入；
- C/C++ include inventory 只解析预处理 include directive，并将 `gnc/<module>/...` 与可解析相对路径映射到 source owner；
- identifier scan 只扫描 production C/C++/CMake，排除 docs、fixtures、oracles、reports、tests 的 expected violation text 和冻结 Legacy；
- include path 在比较前统一 separator、`.`/`..`、repository root 与 Windows case；越出 repository 的 unresolved relative include 记录为诊断；
- comments、普通字符串和 raw string 中的历史词不形成 identifier finding；include string 与 runtime Legacy path 单独解析；
- 当前切片没有 exception/allowlist。真实例外需要先形成窄 ADR，并明确 exact rule/path/token-or-edge。

### 3.3 L2｜artifact-aware semantic rules

- StateOwner/DecisionAuthority、TemporalRelation、RuntimeInstanceId、obligation、commit/effect、proof/lineage 等规则读取其权威 descriptor 或可执行 fixture；
- artifact 不存在时不使用 source token 猜测语义；
- descriptor 出现后，rule activation 是交付的一部分，不能无限保持 awaiting-artifact；
- 每个 semantic evaluator 同时接受正向 fixture 与最小反例 mutation。

## 4. Fitness family 覆盖计划

| Authority IDs | R0-ARCH-002 计划状态 | 检测依据 | 启用前置 |
| --- | --- | --- | --- |
| FF-ARCH-01 | `enforced` | package/model SDK source owner 与 include graph | 已实现 |
| FF-ARCH-05、06 | source-direction 子守卫 `enforced`；运行时语义 `not-applicable-awaiting-artifact` | Workflow/Evidence/Adapter include graph | 首个 state/DTO consumer 激活语义检查 |
| FF-ARCH-02 | `not-applicable-awaiting-artifact` | Kernel dispatch/control-flow inventory + reviewed domain vocabulary | 首个真实 Kernel dispatch 进入工作树 |
| FF-ARCH-11 | `not-applicable-awaiting-artifact` | Kernel/Workflow/Control/Artifact dispatch inventory + domain/product vocabulary | 对应 dispatch 首次进入工作树 |
| FF-ARCH-03、04、15 | `not-applicable-awaiting-artifact` | Compiler-consumed recipe/obligation descriptors 与 publish fixture | R2-CAT/R2-PLAN |
| FF-ARCH-07–10、12、16 | `not-applicable-awaiting-artifact` | CapabilitySlice/ChangeCard、AuthorityDomain、ChangeVector、proof/operator/commit/evidence refs | governance contract/ADR 与真实 change fixture |
| FF-ARCH-13 | `deferred-by-gate` | Workflow Graph、proof、TaskOutcome、ArtifactCommit | R5-WFP/EXE/ART |
| FF-ARCH-14 | R0 gate evidence | withheld scenario record + reviewed diff/untouched areas | R0-GATE-001 |
| FF-DEP-04、06、07、08 | `enforced` | source root/include categories + CMake DAG + negative inventory | 已实现 |
| FF-DEP-01、02、03、05、09 | module-direction 子守卫 `enforced`；format/library/private seam 子守卫 `not-applicable-awaiting-artifact` | source root/include graph | 对应真实 source/header 首次进入工作树 |
| FF-OBJ-01–07、09、15 | `not-applicable-awaiting-artifact` | Compiled RuntimeComponent/StateSchema/recipe conformance fixtures | R2-CAT/R2-PLAN/R3-FIX |
| FF-OBJ-08、12–14 | `not-applicable-awaiting-artifact` | StateBlockPlan、codec、CycleFrame、replacement/commit fixture | R2-BIND/PLAN、R3-STR/TXN |
| FF-OBJ-10、11 | `deferred-by-gate` | Session lifecycle/resource failure suite | R3-LIF |
| FF-BEH-01–10 | `not-applicable-awaiting-artifact` | mechanism/DecisionAuthority/entity/fault descriptors 与 fixtures | R1-BEH/REC、R2-BIND |
| FF-PLAN-01–13 | `not-applicable-awaiting-artifact` | Source/IR/Binding/Plan/Proof compiler positive/negative suite | R2-SRC through R2-PRF |
| FF-RUN-01–10 | `deferred-by-gate` | executable Session transaction/lifecycle/evidence suite | R3-SCH/TXN/CON/LIF |
| FF-DIA-01–04 | `not-applicable-awaiting-artifact` | stable diagnostic registry + failure injection | R1 contracts、R2/R3 diagnostics |
| FF-ART-01–03 | `deferred-by-gate` | RunManifest、lineage、dataset round-trip | R4-OBS/SNK/ART/MAN |
| FF-CFG-01–03 | 分阶段启用 | source/config schema negative suite；runtime image scan | R2-SRC/IR/PLAN 与 R3 |
| §9.8 deletion guard | R0 Legacy isolation 子集 `enforced`；完整 deletion guard `deferred-by-gate` | exact Legacy identifier/include/path inventory | 当前 AGENTS production 禁入；R3/G6 收紧为全量零引用 |

### 4.1 Authority ID inventory

下列显式 inventory 只用于证明本计划逐 ID 覆盖治理分册 §9，不复制各 rule 的语义定义：

- Architecture：`FF-ARCH-01`、`FF-ARCH-02`、`FF-ARCH-03`、`FF-ARCH-04`、`FF-ARCH-05`、`FF-ARCH-06`、`FF-ARCH-07`、`FF-ARCH-08`、`FF-ARCH-09`、`FF-ARCH-10`、`FF-ARCH-11`、`FF-ARCH-12`、`FF-ARCH-13`、`FF-ARCH-14`、`FF-ARCH-15`、`FF-ARCH-16`；
- Dependency：`FF-DEP-01`、`FF-DEP-02`、`FF-DEP-03`、`FF-DEP-04`、`FF-DEP-05`、`FF-DEP-06`、`FF-DEP-07`、`FF-DEP-08`、`FF-DEP-09`；
- Object/state：`FF-OBJ-01`、`FF-OBJ-02`、`FF-OBJ-03`、`FF-OBJ-04`、`FF-OBJ-05`、`FF-OBJ-06`、`FF-OBJ-07`、`FF-OBJ-08`、`FF-OBJ-09`、`FF-OBJ-10`、`FF-OBJ-11`、`FF-OBJ-12`、`FF-OBJ-13`、`FF-OBJ-14`、`FF-OBJ-15`；
- Behavior：`FF-BEH-01`、`FF-BEH-02`、`FF-BEH-03`、`FF-BEH-04`、`FF-BEH-05`、`FF-BEH-06`、`FF-BEH-07`、`FF-BEH-08`、`FF-BEH-09`、`FF-BEH-10`；
- Plan：`FF-PLAN-01`、`FF-PLAN-02`、`FF-PLAN-03`、`FF-PLAN-04`、`FF-PLAN-05`、`FF-PLAN-06`、`FF-PLAN-07`、`FF-PLAN-08`、`FF-PLAN-09`、`FF-PLAN-10`、`FF-PLAN-11`、`FF-PLAN-12`、`FF-PLAN-13`；
- Runtime：`FF-RUN-01`、`FF-RUN-02`、`FF-RUN-03`、`FF-RUN-04`、`FF-RUN-05`、`FF-RUN-06`、`FF-RUN-07`、`FF-RUN-08`、`FF-RUN-09`、`FF-RUN-10`；
- Diagnostic：`FF-DIA-01`、`FF-DIA-02`、`FF-DIA-03`、`FF-DIA-04`；
- Artifact：`FF-ART-01`、`FF-ART-02`、`FF-ART-03`；
- Configuration：`FF-CFG-01`、`FF-CFG-02`、`FF-CFG-03`；
- Deletion：`§9.8 deletion guard`。

## 5. Source-root 物理规则

| Source owner/root | 允许的 framework 依赖 | 额外限制 |
| --- | --- | --- |
| `foundation` | `foundation` 内部 | 禁止 runtime/config/logger/filesystem 与 Legacy |
| `contracts` | `foundation`、`contracts` | 禁止 JSON/CSV/CLI 与具体序列化依赖 |
| `model_sdk` | `foundation`、`contracts`、`model_sdk` | 禁止 compiler/kernel/evidence/workflow/application/adapters |
| `compiler` | `foundation`、`contracts`、`model_sdk`、`compiler` | core 禁止具体 JSON/YAML/INI parser 分支；frontend adapter 需独立 root |
| `kernel` | `foundation`、`contracts`、`kernel` | 禁止 compiler/package/domain/format/workflow/frontend |
| `evidence` | `foundation`、`contracts`、`evidence` | sink adapter 不能读取 Runtime Cell/CommittedStateStore |
| `workflow` | `foundation`、`contracts`、`evidence`、`workflow` | 禁止 Session/CycleFrame/state store internals |
| `application` | ADR-0003 closure | 跨域只使用公开 contract/ref/receipt/Outcome |
| `adapters` | foundation/contracts/Application public seam 与已批准 Artifact/Control DTO | 不得依赖 compiler/kernel/workflow internals；composition exception 必须精确 |
| `packages/` | foundation/contracts/model SDK public seam | 禁止 compiler/kernel/workflow/application/adapters；包私有 header 不跨包 |
| `user/` | project composition policy（待真实 consumer 冻结） | framework 永不反向 include；不得迁入通用 runtime |
| `apps/` | foundation/contracts/Application/Adapter seam | 禁止直接依赖 Compiler、Kernel、Workflow、package 或 user source |

表中的“允许依赖”只表达方向上限，不自动授权尚未定义的公共 header。具体 public/private seam 需要在真实 consumer 出现时收窄。

## 6. Legacy 与延期能力策略

### 6.1 R0 production error

- include/link/runtime path 指向 `reference/legacy/`；
- production 使用 `SimulationNode`、`DiscreteNode`、`NodeFactory`、`NodeRegistry`、`AssemblyContext`、`MissionAssembler`、`ConfigNode`、`IObservable`、`IDiscreteTask`；
- production 使用 `GNC_REGISTER_NODE_TYPE`、`GNC_REGISTER_BUILTIN_NODE`、`requireByName`、`bindIfPresent`；
- 新 Session/Compiler/Application 调用 Legacy archive 或 extracted binary。

### 6.2 延期能力

`SegmentTransaction`、`TopologyTransaction`、dynamic package runtime/ABI 和新 `KernelCapability` 当前没有 production consumer。本切片不扫描这些名称，也不建立延期 token policy；对应能力进入已开放阶段并出现真实实现时，由该纵向切片增加直接结构与行为检查。

## 7. 同一 evaluator 的故障矩阵

`validate-source-boundaries.ps1` 先把 repository 转为标准化 source/include/CMake inventory，正向仓库检查与合成反例都调用 `Test-SourceBoundaryInventory`。当前八个反例为：

1. 同一 source root 登记两个 owner；
2. Kernel 通过 dot-segment include Compiler；
3. Adapter 直接 include Kernel internal；
4. package 直接 include Compiler；
5. framework 反向 include `user/`；
6. `gnc/<module>` 指向未知内部模块；
7. production source 同时引用 Legacy 路径与 retired API；
8. production CMake 通过 bracket argument 引入 Legacy。

正向矩阵覆盖 Kernel/Compiler/package/Adapter/apps 的允许边、注释中的伪 include、普通字符串中的 Legacy 名称和 CMake bracket comment。architecture baseline 的既有反例继续承担 shared symbol/Legacy ownership、DAG、CMake edge 和派生基线漂移检查。未来 semantic evaluator 与首个 descriptor/state/transaction consumer 同步进入对应产品切片。

## 8. 可执行输出

当前 validator 直接输出 production C/C++ 文件数、include 数、CMake 文件数和已拒绝反例数；失败时输出稳定 rule id、文件、行号与原因。CTest 和 repository verification 以退出码消费结果。当前没有 runtime consumer，也没有需要持久化的 architecture fitness report consumer，因此本切片不新增 schema 或报告镜像。

## 9. 切片退出检查

- 治理分册 §9 的全部 rule family 保持可定位，未来规则没有被标为已实现；
- 当前强、partial、awaiting-artifact 证据与实际脚本一致；
- source/include/CMake 首切片使用真实仓库正向扫描和同 evaluator 反例；
- public schema、module graph 与 runtime firewall 变化继续保留 ADR gate；
- shared symbol、runtime mutable state、descriptor 与 transaction 语义随真实 consumer 激活；
- 完整验证结果记录在任务交付 commit 的可复现测试入口中，不生成 CI 收据副本。
