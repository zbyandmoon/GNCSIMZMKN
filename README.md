# GNCZMKN Next

这是 GNCZMKN 大型目标架构的 greenfield 实现仓库。仓库版本仍为 `0.0.0-bootstrap`；R0 科学基线与 R1 独立模型生态已经闭合，R2 开始交付静态编译能力，尚未形成仿真运行能力。

## 当前交付状态

- 当前 gate：`R2`；G0/G1/G2 已由仓库所有者判定 `Passed`。
- 已闭合：R1 Foundation、窄范围 in-process Contracts、真实 GlideEnvelope PureQuery、真实 ForceMomentClosure、YYZ 四个产品切片和 CAVH 公式产品切片。
- 当前静态编译入口保留既有 `TypedStaticCompositionSource`/`semantic-bytes@2` qualification 路径，并增加最小 REF-YYZ 的 programmatic `CompleteStaticCompositionSource` revision 3。后者经同一个 package-owned Catalog 形成 `CompleteCanonicalMissionIr`、typed ports/bindings、RigidBody/Mass state blocks 与 initial/projection/evolution obligations、Query/Closure/RuntimeComponent plans、Boundary DAG、IntegrationScope、Transaction、派生 `PlanProofIndex`，再由 exact linker 生成 process-local `ExecutionPlanImage` review artifact。Image 已保存 exact package/build lock、按 storage class 划分的确定性 extent、slot size/alignment/offset/codec/writer/reader/lifetime、两个 state codec、Query `CallerLocal` route、Closure `HeldInterval` route、fixed-step RK4/numerical policy、三类 transaction outcome、Session-owned preparation policy，以及七个 package-owned typed `RuntimeCellFactory` 的完整数字依赖闭包；link 阶段不调用任何 entry，也不创建 R3 Session 或 RuntimeCell。
- 暂缓开展：YAML/INI、多端 adapter、Session、mini runtime、runtime registry、serializer、StateFragment 与 R3～R8 能力。
- 旧 GNCZMKN 只作为只读行为与科学参照，不进入任何生产 target、include path 或运行依赖。

阶段顺序已经固定：R1 以 Definition、PreparedModel 和 Kernel 的无 Session 独立求值闭合；R2 实现 MissionSource 到已证明、已链接 ExecutionPlan 的静态编译；R3 再由正式 Session、StepTransaction 和 IntegrationScopePlan 形成首个正常 YYZ run。R2 不建设临时 runner、mini Session 或影子 runtime。

## 新成员从这里开始

1. 阅读 [当前执行状态](docs/handoff/r0-execution-state.md)。
2. 查看 [任务台账](docs/tasks/backlog.json)。
3. 阅读当前任务直接引用的 ADR、架构分册和测试。
4. 需要项目边界背景时再阅读 [交接总览](docs/handoff/README.md)。

## 构建与验证

```powershell
./tools/install-eigen.ps1 -DownloadIfMissing
cmake --preset dev "-DEigen3_DIR=build/dependencies/eigen-3.4.0/install/share/eigen3/cmake"
cmake --build --preset dev
ctest --preset dev
powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify-repository.ps1
```

也可以运行一键检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap.ps1
```

离线环境可以向 `install-eigen.ps1 -ArchivePath` 或 `bootstrap.ps1 -EigenArchive` 提供已下载的 Eigen 3.4.0 官方 ZIP；脚本会核对固定字节数与 SHA-256。

当前 CLI 只证明 composition root、编译器和测试工具链可工作：

```powershell
build/dev/gnc_sim --version
build/dev/gnc_sim --self-check
```

首个 YYZ 产品入口已能独立计算一步 candidate。`AerodynamicTableDefinition`、真实 multiaffine table asset、`PreparedAerodynamicTableModel` 与 `AerodynamicTableQueryKernel` 形成独立 PureQuery；`RigidStepKernel` 直接消费正式 coefficient output，再完成 dimensionalization。独立 `ForceMomentClosureKernel` 对显式 body-wrench contributions 做 CoM 力矩搬移与求和，正式 Closure output 生成 `RigidFormInput`，该输入固定用于全部 RK4 stages。`RigidStepOutput` 只携带下游消费的 candidate；air-data、气动 query telemetry、Closure evaluation 和初始导数位于 `RigidStepTelemetry`：

```powershell
build/dev/gnc_yyz_rigid_step_product_probe --self-check
```

第二个 YYZ 产品入口现已覆盖 committed observation、限幅 altitude/pitch guidance、pitch-moment controller、当前周期理想力矩执行、typed propulsion、连续两次 rigid/mass atomic commit，以及从三份 committed samples 生成的最小 mission result。推进力与控制纯力偶经同一个 supplied-wrench consumer 闭合，tick 1 提交后会重新读取刚体状态和 `99.95 kg` 质量；最终 tick 2 结果匹配 `REF-YYZ-MISSION-COMPOSITION-001`，同时保留 `REF-YYZ-PROPULSION-RESPONSE-001` 和既有 atomic-boundary 回归：

```powershell
build/dev/gnc_yyz_two_interval_mass_commit_product_probe --self-check
```

首个 CAVH 产品入口由独立 `GlideEnvelopeQueryKernel` 从 prepared parabolic envelope 生成正式 query output；Eq17/Eq18 组合显式消费该 output，再把 typed gamma reference 与正式 `alpha*` 交给 TDCT。limited alpha 属于正式 output；包络结果在公式组合层作为 telemetry 留存，公式中间量、TDCT 修正和饱和信息同样位于 telemetry。它复用 `ORACLE-CAVH-FORMULA-001` 比较三组方程与四组 TDCT 结果，并拒绝包络域错误、公式奇异、Eq17 导数退化、非法 TDCT 和上下文不一致：

```powershell
build/dev/gnc_cavh_formula_product_probe --self-check
```

当前 CAVH 输出止于 TDCT 公式阶段的限幅 alpha；产品级 guidance command 的 frame、时间与 ownership 映射等待真实 vehicle/controller consumer。

YYZ 与 CAVH 共同消费最小 `ModelDefinitionMetadata`、`PreparedModelMetadata` 和 `AlgorithmEvaluation<Output, Telemetry>`。公共 execution-form tag 现包含 `PureQuery | Closure | RuntimeComponent`；当前 PreparedModel 产品路径仍只使用前两类，clock/configuration expectation 由 package definition 保持。GlideEnvelope、AerodynamicTable 与 ForceMomentClosure prepare 对各自 model id、model version 与 execution form 做 exact 检查。产品 probe 直接验证真实 query/closure output 消费、错误 model version 拒绝与 C++ output 类型边界，并保留既有 R0 oracle。

既有 R2 static composition 继续从 package-owned descriptor 精确解析 CAVH `GlideEnvelope` 与 YYZ `AerodynamicTable` PureQuery、`ForceMomentClosure` Closure 和对应 algorithm consumer；其 `semantic-bytes@2` qualification 路径保持不变。完整 REF-YYZ 路径在 additive source/IR revision 3 中选择 uniform environment、aero、FrozenInterval closure、RigidBody、Mass、guidance、controller、ideal actuator、supplied propulsion 和 terminal evaluator 的真实产品定义。它冻结 current-cycle/interval temporal edges、两个唯一 StateOwner、initial state、PublishProjection、query/closure authorization、held form、rigid derivative、mass candidate、terminal committed history 和 rigid/mass atomic candidate set，不把 wrapper、dummy provider 或 RunBinding 伪装成 runtime node。

```powershell
build/dev/gnc_compiler_static_plan_probe --explain
build/dev/gnc_compiler_static_plan_probe --semantic-hash
build/dev/gnc_compiler_complete_yyz_plan_probe --self-check
```

YYZ package 还贡献 `AltitudePitchGuidance` 的 stateless `SampledTransform` descriptor：`vehicle.process` placement、每个 committed boundary 的 `process` phase、`BoundaryEvaluation`、current-cycle sampled input/output、zero-order hold、默认 instantiate/dispose 边界和 exact guidance kernel identity。该 schedule 只描述当前 R1 两区间产品 identity，不覆盖 00A/Reference A 的目标多速率 profile；该窄 descriptor 也不开放 state-schema 字段或重新解释现有混合 output。它的 canonical config 可确定性重建 definition，Catalog 对 form/profile/recipe/obligation/schedule/port/lifecycle 组合做封闭校验：

```powershell
build/dev/gnc_compiler_runtime_component_catalog_probe --self-check
```

`GNC-PLAN-RUNTIME-COMPONENT-UNAVAILABLE` 继续保护既有窄编译入口和残缺 RuntimeComponent 图；REF-YYZ revision 3 已闭合 provider、consumer、owner、schedule、temporal、invocation、typed factory、result route、stored slot/state codec、integration、transaction 与 preparation facts，并可生成供 owner review 的静态 Image。缺 provider/授权、owner/writer 不唯一、非法 reader、错误 alignment、layout overflow、非法 phase/cycle/time relation、scope/transaction/lifecycle 不完整、factory/codec/build-lock/result-route cross-reference 错配和 unresolved implementation 均 fail closed，失败路径不发布部分 Image。该结果把 `R2-PLAN-001`、`R2-PRF-001` 与 `R2-LINK-001` 推进到 review，G3 仍由仓库所有者决定。

`hash_canonical_mission_ir` 使用 `gnc.canonical-mission-ir.semantic-bytes@2` 的显式 tagged/length-prefixed big-endian encoding 与 SHA-256。source URI/path、输入顺序和 plan id 被排除；C++ 与 Python reference 继续固定 YYZ qualification vector `b29dc67f2a9e0bb36cb18a5e54a8c4830bdb0cae718fbf856646ba903892511b`。RuntimeComponent 仍不会进入该 API。完整图改用 additive `gnc.canonical-mission-ir.semantic-bytes@3`：source semantics覆盖 model/config/asset/port/state/obligation/schedule/temporal/invocation composition，package entry identity、recipe、workspace、state layout、build fingerprint、函数地址与 source location均排除。Descriptor revision 6 与 Image revision 3 另保存 exact implementation、allocation、access 和 lifecycle facts；registration order 与地址不影响稳定 fingerprint，任何影响 R3 分配或访问的已链接字段都进入 Image fingerprint。当前仍未提供 syntax-neutral `SourceTree`/`SourceMap`、通用 `PreparedModelKey`/共享 cache、持久化 serializer、Session、stores、scheduler、integrator execution、candidate staging/commit、实际 PreparedModel/Bound handles、workspace instance 或 Session-local RuntimeCell；asset proof仍只证明 source-selected identity preservation。`R2-PLAN-001`、`R2-PRF-001` 与 `R2-LINK-001` 均为 owner review。G3 未通过，factory、codec 与 science entry 的实际恢复和调用、RuntimeCell 物化及 R3 后续阶段保持锁定。

## 仓库地图

```text
framework/include/gnc/
  foundation/      数学、数值和值工具
  contracts/       领域、时间、诊断和产物契约
  model_sdk/       definition metadata 与 algorithm evaluation
  compiler/        source、catalog、IR、proof 与 lowering
  kernel/          session、region、state、transaction 与 backend
  evidence/        observation、artifact 与 lineage
  workflow/        experiment、task graph 与 tool port
  application/     use case、control 与 DTO

packages/          可复用领域、模型和工作流贡献
adapters/          CLI、Python、工具、存储、IPC 与前端适配
user/              项目私有研究代码、配置和资产
design-notes/      目标架构蓝图
fixtures/          可执行 reference fixture
oracles/           独立科学与行为判据
reference/legacy/  只读旧仓库快照
docs/tasks/        工作包、依赖和阶段门
```

## 权威顺序

发生冲突时按以下顺序处理：

1. 已接受 ADR 对本仓库的窄实现决策；
2. `design-notes/gnczmkn-architecture-roadmap/` 的目标语义与架构不变量；
3. `specs/` 中已标记 stable 的机器契约；
4. 已通过的 executable fixture、oracle 和自动测试；
5. `docs/handoff/` 的协作与交付规则；
6. `reference/legacy/` 中的旧行为证据。

旧实现出现差异时，需要按缺陷修复、约定统一、显式模型变化、时间语义澄清、浮点差异或无法解释进行分类。无法解释的差异会阻断阶段门。
