# ADR-0015: R2 stateless sampled RuntimeComponent Catalog boundary

- Status: Accepted
- Decision Date: 2026-08-20
- Owner: Repository owner
- Related tasks: R2-CAT-001、R2-PLAN-001
- Architecture references: 00A §3.3、05 §5～§7、05 §12～§15、10 §4、12 §4、12 §9.3、12 §11～§14、13、14、15 §4

## Context

R1 已交付 `AltitudePitchGuidanceKernel`。它从 committed rigid observation 独立计算限幅 pitch command，正式 `AltitudePitchGuidanceOutput` 被 `PitchMomentControllerKernel` 消费；两段 YYZ 产品组合在每个 committed boundary 重新求值该链。kernel 没有跨调用 mutable state、reset、checkpoint、termination 或 decision authority。

当前产品 wrapper `ControlledPropelledRigidMassStepKernel` 在调用内部构造 observation，并内联执行 guidance、controller、actuator、propulsion 与 rigid/mass candidate。Catalog 目前没有独立 RigidBody StateOwner/`PublishProjection` provider，也没有 package-owned environment PureQuery；`RunBinding` 只容纳初态、time origin、初始 ParameterState 与外部 replay/input ArtifactRef，不能替代每步 sampled provider。把单个 guidance occurrence 降为计划会留下 required input；把 wrapper 宣称为单一 state owner 又会绕过 rigid/mass 原子边界和未来 R3 transaction ownership。

## Decision

1. `ModelExecutionForm` 增加封闭的 `RuntimeComponent` tag。只有该 form 可以携带 `StaticRuntimeComponentDescriptor`；PureQuery/Closure 继续要求 preparation identity，并拒绝 runtime-only facts。
2. 首个 package-owned descriptor 选择 YYZ `AltitudePitchGuidance`，profile 为 stateless `SampledTransform`，唯一 obligation 为 `BoundaryEvaluation`。placement 固定为 `vehicle.process`。
3. required input 为 current-cycle committed rigid observation，正式 output 为 current-cycle guidance output。两端使用 `SampledSignal`；输入 cardinality 为 exactly-one，输出 cardinality 为 one-or-more。
4. schedule 把现有 R1 两区间产品的“每个 committed boundary 求值一次”冻结为 process phase、整数 step interval 1、offset 0、zero-order hold 和 input age 0。该事实只属于 `gnc.package.yyz.guidance.altitude-pitch.experimental@1`，不声称覆盖 00A/Reference A 的 10/20 Hz 目标 profile；后者需要独立 definition/source 并重新编译。该无状态 descriptor 不开放 state schema；lifecycle 只声明 instantiate/dispose；entry 锁定现有 guidance kernel identity。
5. package 提供 Catalog-only exact config schema 与 builder，确定性保留 frame、clock、configuration revision、guidance gains/limit 和 quaternion policy。该 builder 只服务 Catalog/package descriptor，不构成新的 canonical source schema。现有 qualification IR 继续使用 ADR-0013 的范围；完整 RuntimeComponent graph 使用后续明确版本的 canonical source/IR。科学公式、控制符号、frame、时间边界与 R1 oracle 不变。
6. Catalog 只接受已有产品支撑的组合，并拒绝 invalid form/profile/recipe/obligation/schedule/port/lifecycle。`GNC-PLAN-RUNTIME-COMPONENT-UNAVAILABLE` 在真实图尚未闭合时保护旧的局部编译路径。真实 provider、consumer、schedule 与 temporal edge 闭合后，合法 canonical YYZ source 必须继续生成静态计划；该合法路径不再触发临时 firewall。
7. 既有 `CanonicalMissionIr` revision 2 与 `semantic-bytes@2` 继续显式拒绝 RuntimeComponent execution form，encoding identity 与 qualification vector 保持不变。完整 RuntimeComponent 图使用 additive `CompleteCanonicalMissionIr` revision 3 与 `semantic-bytes@3`；两条 API 不互相转换或改写。
8. 残缺图、缺 provider、provider/StateOwner/writer 不唯一、所有权冲突、非法 cycle、时间关系或调用授权不成立时继续 fail closed。图闭合不得使用 dummy provider、虚构 StateOwner、wrapper 伪装 owner、mini runtime 或临时 runner。

## Consequences

- `R2-CAT-001` 获得 owner 接受、可执行 Catalog snapshot、canonical config round-trip 和非法组合负例，满足完成条件。
- `R2-PLAN-001`、`R2-PRF-001`、`R2-LINK-001` 在完整真实 YYZ graph 上连续推进；局部或未闭合图不发布 Descriptor/Image。
- sampled output contract 原样锁定现有被 controller 消费的 `AltitudePitchGuidanceOutput`；本次闭合图不改写其中 measured/error/saturation 字段的既有产品语义，也不执行新的 command/telemetry contract 拆分。
- state schema、initial builder、projection、derivative 与 interval candidate 只随真实 RigidBody/Mass owner 和完整静态图进入公共 descriptor。
- Runtime scheduler、Runtime Cell 实例、Session、CommittedStateStore、CycleFrame 与 StepTransaction 执行归属 R3；serializer 留待出现真实持久化 consumer。

## Alternatives considered

- 把 `RigidStepKernel` 标为 `ContinuousStateOwner`：现有 kernel 自行执行完整 RK4，且没有独立 publish projection、derivative entry、StateSchema、lifecycle 或 environment/mass/wrench provider descriptor，会提前决定 R3 ownership 与 transaction 语义。
- 为 guidance 创建 dummy observation provider 或 package-local runner：无法对应现有产品边界，并会绕过 Plan Firewall。
- 把完整 controlled rigid/mass wrapper 声明为一个 RuntimeComponent：会隐藏 guidance/controller/actuator/propulsion 的真实连接以及 rigid/mass 多 owner 原子提交边界。

## Implementation status

当前实现已交付两项真实 StateOwner、产品入口，以及 planning/proof/exact-link review。七个 RuntimeComponent 各自冻结并 exact-link package-specific typed RuntimeCellFactory；Image 为 factory 保存 runtime component、provider preparation/plan、state、input/output/writer、invocation/callsite、interval model、integration/transaction policy 与 evaluator history 的直接数字 handle。environment/aero query 使用授权 caller 的 `CallerLocal` typed return，不分配 result storage；FrozenInterval Closure 只生成一个 coordinator-owned held interval slot 和 writer。RigidBody/Mass state codec、stored-value codec、按 storage class 划分的确定性 extent 与 slot size/alignment/offset/reader/lifetime 也已闭合。link 阶段保持零调用。`R2-CAT-001`、`R2-PLAN-001`、`R2-PRF-001` 与 `R2-LINK-001` 已完成，仓库所有者于 2026-08-20 判定 G3 `Passed`。R3 Session 已实际调用 typed factory/codec；每个 Runtime Cell factory 只能查看 Image 精确声明且去重后的 preparation 依赖，未声明依赖在 placement 前 fail closed。统一 step executor 按 Image region/DAG 与 TransactionPlan 执行两次 Continue 和一次 Terminal：前两步在窄 state/frame/candidate authority 下完成 Image-backed RK4、mass evolution、candidate validation、committed-history staging、rigid/mass 原子提交与 ObservationSeal；Terminal evaluator 通过唯一授权的 Image-backed history view 读取三份 chronological sample，写入正式 result slot，随后以 epoch `+1`、tick `+0` 的 instant ModelCommit 保持 state bytes，并依次提交 tick 2 ObservationSeal 与 result seal。`IntegrationHeld + HoldInterval` 值始终属于当前 transaction，完成消费后随 CycleFrame 销毁。执行期提交前故障保留最近一次成功 commit/history/seal并使 Session 进入 `Failed`；成功 Terminal 进入 `Completed`。

## Executable evidence

- `framework/include/gnc/model_sdk/static_descriptor.hpp`
- `framework/include/gnc/model_sdk/runtime_cell_factory.hpp`
- `framework/include/gnc/compiler/static_mission_compiler.hpp`
- `framework/include/gnc/compiler/complete_execution_plan.hpp`
- `packages/yyz-rigid-step/src/mass_commit.cpp`
- `tests/compiler_runtime_component_catalog.cpp`
- `tests/yyz_static_product_contracts.cpp`
- `tests/kernel_session_materialization.cpp`
- `tests/kernel_opening_boundary.cpp`
- `tests/kernel_step_transaction.cpp`
- `r2.compiler-runtime-component-catalog.probe`
- `r2.yyz-static-product-contracts.probe`
- `r3.kernel-session-materialization.probe`
- `r3.kernel-opening-boundary.probe`
- `r3.kernel-step-transaction.probe`
