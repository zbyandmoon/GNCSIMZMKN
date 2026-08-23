# REF-YYZ-001 科学、行为与 target conformance bundle 设计

- 文档状态：R0 qualification implemented；R3 target-rate runtime conformance 与 canonical 00A opening/domain difference 切片均已执行，tick 1～3000 trajectory 和 science verdict 受精确气动域阻断
- 实现成熟度：`REF-YYZ-001` fixture-local R0 qualification bundle 已达到 `executable`；R3 target-rate 工程运行保留 `target_conformance/science_verdict_pending`，canonical profile 已达到 `executable-domain-locked`，当前证据不构成完整 00A 轨迹验收或 R3 gate 结论
- 任务：`R0-SCI-003`（backlog 为 `done`）；R3 executable update：`R3-YYZ-001`（`review`）
- 日期：2026-08-23
- Authority owner role：Scientific Authority
- 协作 owner roles：Validation Lead、Runtime Numerics Lead、Model SDK Lead、Architecture Lead

## 1. 目的与非目标

本设计为 `REF-YYZ-001` 建立可审计的三层 reference：target 架构契约、冻结 Legacy 行为和独立科学真值。完成后的 bundle 要回答四个不同问题：

1. 00A 的 source、Plan、step transaction、Observation、diagnostic 和 evidence 示例是否机器有效；
2. 冻结 Legacy YYZ 模板实际做了什么；
3. 经批准的 YYZ 6DoF 模型、公式、轨迹、终止和指标应当是什么；
4. Legacy、独立 reference 与未来 target 之间的每个差异由谁、依据什么分类。

本设计不把 Legacy output 当作科学真值，不把 00A 的示例数值当作独立 reference，也不在 R0 实现 R1 model package、R2 Compiler 或 R3 Session。它不冻结新的公共 schema、C++ 类型、FieldId、DiagnosticCode 或 artifact wire format；文中的目录名、case id 和 record 字段只是信息需求候选，必须由 `R0-SPEC-001`/ADR 路由后才能成为公共 contract。

## 2. 三条证据 lane

### 2.1 不可互相替代的 authority

| Lane | 回答的问题 | Authority | 可产生的事实 | 不能声称 |
| --- | --- | --- | --- | --- |
| `target-conformance` | 00A/15 的架构对象和关系能否被 schema/validator 表达并由 target runtime 执行 | Design/Plan + Artifact | source arithmetic、plan proof、精确速率与 age、transaction ordering、确定性与 Session 隔离 | 物理轨迹正确；完整 science verdict 已通过 |
| `legacy-behavior` | 冻结 archive 在固定输入/环境下做了什么 | Artifact observation + approved migration disposition | raw trace/dataset、终止、双跑一致性、旧公式/时序观测 | 00A 30 s 结果；旧算法有科学权威 |
| `independent-science` | 批准的模型在声明工况和数值策略下应产生什么 | Model + Artifact | 公式 intermediates、解析/高精度 expected、trajectory、terminal、metric、convergence | 证明 target transaction/encoding 已实现 |

一个 fact 只能在声明的 lane 中获得权威。跨 lane 比较通过 semantic mapping 和 difference report 发生，不能通过复制 expected、复用同一实现、把 Legacy link 进 target 或把短文档 id 冒充内容 hash 实现。

### 2.2 候选 fact validity

以下标签用于本设计说明，不提前定义公共 enum：

- `approved_expected`：有 owner approval、source/input hash、validity 和 tolerance 的可验收事实；
- `observed_legacy`：隔离运行中观察到且 provenance 闭合的旧行为；
- `illustrative`：蓝图为解释对象关系给出的值或短 id，尚无科学来源；
- `target_pending`：schema fixture 可表达，但对应 runtime、source 或证据仍未闭合；
- `needs_decision`：缺少有权 owner 的模型、scenario、classification 或 tolerance 决策；
- `rejected`：来源、完整性、domain 或 policy 已明确不允许成为 expected。

Validator 不得把 `illustrative`、`target_pending` 或 `needs_decision` 计为 pass。一个机器有效 payload 可以在结构上通过，同时其 runtime/scientific assertion 仍为 pending。

## 3. 已审计来源与身份

### 3.1 Target seed

| Source | Bytes | Raw SHA-256 | 当前权威范围 |
| --- | ---: | --- | --- |
| `fixtures/ref-yyz-001/fixture-manifest.json` | 1,865 | `69eb288d48714edcdc565268151e151cbb62a4e1a2f8644a45c000d659f5086f` | fixture id、specification-only 状态、缺口 |
| `00a-yyz-end-to-end-walkthrough.md` | 19,743 | `5e9078a4df7f1bd53b6f83bce2c040f274adcb3de61d54066d6acf58d0552767` | target 端到端对象与示例 |
| `15-reference-vertical-designs-and-object-placement.md` | 58,741 | `7e01ec0c882c97293af15442440caa91d68ade8f5e51972a36c39c981acb576d` | YYZ target 对象、闭合、周期与验证设计 |

R3 canonical addendum 将 owner-authorized 00A facts 与已接受 R0 bytes 固定在独立 profile 中：

| Source | Raw SHA-256 | 当前权威范围 |
| --- | --- | --- |
| `fixtures/ref-yyz-00a-canonical/source.json` | `b4e045237a87d85592b19d62863df95065ecf956146c4a131d7c0d890c41ab98` | canonical author facts、mapping/domain、mission/profile identities、asset refs 与 expected domain outcome |
| `fixtures/ref-yyz-001/asset-index.json` | `570750a2d54d0a2ba67689b3da553ff100dfdf07bae4bddf7897de1f55ae122e` | 七项 R0 qualification asset identity/revision/source index |
| `fixtures/ref-yyz-frozen-interval/cases.json` | `17609b48b02c43bfe9e103218cba04210bcb93938fbfbc332935225461cf0bc6` | environment、mass、aero、propulsion exact payloads |
| `fixtures/ref-yyz-mission-composition/cases.json` | `c506a2e5c3e193de40f0cd3815df077f6a1dcd48e592671949d8eacf453a5a2d` | guidance/control、numerical、termination/observation exact payloads |

Manifest 当前三条 seed fact 的状态是：

- `FACT-YYZ-PLAN-RATE`：ADR-0023 已固定 canonical 00A 的 100 Hz base、20 Hz guidance 与完整 `1/5/2/1/4` cadence；Compiler、proof、Image 和 Session 均已执行；
- `FACT-YYZ-STEP-COMMIT`：pre-commit failure 保持 epoch/tick/state，成功 observation 绑定 ModelCommit；target-rate 3000-tick 工程运行与 canonical tick-0 domain failure 均沿真实 R3 Session 路径形成证据；
- `FACT-YYZ-TRAJECTORY`：canonical source、exact assets、opening expected、per-field tolerance、独立实现与 difference report 已形成；accepted aero domain 在 opening 阶段拒绝 Mach `0.6176470588235294`，因此 tick 1～3000 trajectory 保持 unavailable，report 记录一个 unresolved item。

### 3.2 Frozen Legacy source

冻结来源：

- Git head：`a63621c368aa8e7889547689bcce9c7686b886ac`；
- ZIP SHA-256：`2159a324fd897e4bd508c140a36c9165d744e4e4e61861c5b568201707f988e5`；
- ZIP prefix：`GNCZMKN-legacy-a63621c/`；
- 现有干净复现：[`r0-leg-001-20260810-07`](../../reference/legacy/reproduction/r0-leg-001-20260810-07/evidence-index.json)。

关键输入 identity：

| Archive-relative YYZ input | Bytes | Raw SHA-256 |
| --- | ---: | --- |
| `config/mission.json` | 461 | `9491b622e63b008f3cd99facc6837da5304126966b7736d60f256cd44ca82006` |
| `config/assembly/base_mission.json` | 1,750 | `80eccebcf26d9cb6ceb49486667581600da26099535a832a586ad6d5c17d2a2e` |
| `config/assembly/interceptor.json` | 5,293 | `00b0bddd6f41a5184d34bbdf1dafdcdfe87d8767d65a32dfd07670a446a71214` |
| `config/parameters/initial_state.json` | 116 | `50c8bbdaacde43f9a053320f2cb73e149b187ba1513b331efe50b9fd154c8a8a` |
| `config/parameters/common/standard_trajectory.json` | 300 | `d987bedd19d9ca38252788e7a8ed16df65b9a78277ea95d910221d8773d668e5` |
| `data/trajectory/nominal.csv` | 912 | `3b1a86f9f60fb6e05044f8712331127f0258ee2ba3a24e65ae8a141eea5c68d7` |
| `config/parameters/output/mass_properties.json` | 220 | `d22027587b07ac91033aba887df33b90bc5057e88e15c4e34c65977017be4c82` |
| `config/parameters/output/aerodynamics.json` | 385 | `51cb2a8c752e6861e056626b3fe6adeb425d8f9867179113f9c1439dcdd82f13` |

关键实现 identity：

| Legacy source | Raw SHA-256 |
| --- | --- |
| `interfaces/yyz_c6_types.hpp` | `f65cad847b9ff0cee49b6b4b017b36914f4f88bdb5f684b4b47c0ae87b5d6939` |
| `common/yyz_standard_trajectory_preprocessor.hpp` | `a25b2baad40313b97f833ff0cbbe9c5946abd6180ecb2efd859b66d8875df304` |
| `components/form_rigid_body_6dof.hpp` | `ec65912f4ed7355d243160ed7456ad87cec1a7c95adf71eea8db9ee8ab34a1b9` |
| `components/input_air_data.hpp` | `7e492cdc147982deb3bc78f87fc8cdf99bbe3a8b1167e632a2cf99a2b7e27be0` |
| `components/process_guidance.hpp` | `8568545a35f777bc76c8cc2bfe6412e2007de2e502d7eebb335188302a4f051b` |
| `components/process_flight_control.hpp` | `eefe797f067547e5e073b9245e5aa12e6ed326f984aa8a1bbf9d7244cb5c1691` |
| `components/process_control_allocation.hpp` | `789d3f76e4bf72aa45f2e877a3157786742ac47a7b514296bf311a2f85f13e4e` |
| `components/output_actuator.hpp` | `983e38caabef74062adfab56e631541fec63fcd405bb8b07e12dc8f5ad7d72bf` |
| `components/output_propulsion.hpp` | `ff836c3016e09bbde04144c2a3e6561b875c923e5c35b62b7015dad131c42baa` |
| `components/output_mass_properties.hpp` | `2e4fc0f8c17e5227615e123d70699a4146c40f592c59688a48bd1cb10d788709` |
| `components/output_aerodynamics.hpp` | `e03f21d9f75f745fc832ee1250ae99446d72ac91950ef99bfed662d64e4c8a31` |
| `components/interaction_force_moment_6dof.hpp` | `6be3e3f9aa3f7d655f7b39403738e6ff16b54f69cd33f64aeb51d7356cd81fbb` |
| `components/termination_state_metric.hpp` | `fdaac61a8e7109ec5c8c637345096de21dbb9170f3958e90efdbf6cf89a9f6bd` |
| `components/summary_basic_metrics.hpp` | `c8edc4a0644d9691d418146ac3d39b8626e38db3bc808d5cab1693dcfba09b78` |

这些 hash 固定“旧实现做了什么”的来源，不能把旧公式升级为 target 模型权威。冻结项目自身把多处公式/模型称为 reference slot 或模板；干净运行只证明结构和复现，不证明科学有效性。

### 3.3 已有 Legacy 运行事实

R0-LEG-001 的 YYZ 双跑观察为：

- mission：`user/yyz_cartesian_6dof_framework_9/config/mission.json`；
- `dt = 0.02 s`、duration/max-time `2.0 s`、RK4；
- 101 个含初值的数据 row，terminal 为 tick 100 / `t = 2.0 s` 的 max-time；
- raw CSV 每次 115,477 bytes，SHA-256 `fe8b60dffd65635d9a7d330f1d7a20dda2e0666ecc56f39711d5db1e68eec0e2`；
- summary 每次 9,402 bytes，去除已声明非语义行后的 SHA-256 `8da7f1064d1d9eaf242e65fcd39140f0adde5fdd00bb59fcb626e32af002ebed`；
- CSV 有 201 个旧列，项目装配有 24 个旧节点；二者都是 accidental structure，不是 target expected。

现有 reproduction 只提交了 report/log/hash，没有提交 raw CSV bytes。当前 R0 qualification 未从 hash、日志摘要或 `nominal.csv` 初始/规划输入反推 Legacy output trajectory；未来若开展完整 Legacy-to-target 数值分类，需要从冻结 ZIP 在全新隔离 workspace fresh capture。

### 3.4 Independent science 的当前实现与空缺

`REF-YYZ-6DOF-CORE-001` 已实现并接受 fixture-local 惯性笛卡尔刚体核心：supplied uniform gravity、常正质量、常对称正定体轴惯量、质心处总力、质心总力矩、被动 Hamilton `q_I_B`、固定步长经典 RK4，以及每次导数求值前和提交前归一化。独立 60 位 Decimal reference 与 C++17 probe 覆盖公式 intermediates、解析匀速平移、解析主轴自旋、非主轴无外力矩高精度轨迹、姿态与角速度四阶收敛、转动能与角动量模守恒、ExactGrid 终止、阶段失败整候选丢弃和七类输入域拒绝。

`REF-YYZ-001` 现已在已接受的 fixture-local applicability domain 内提供 canonical R0 qualification source、uniform environment、coefficient lookup 与适用域、propulsion、mass evolution、制导控制、两区间闭环轨迹、terminal 和逐叶 tolerance/difference report。R3 target-conformance 切片已沿产品 navigation/guidance/controller/ideal-actuator 链执行精确多速率短跑与 3000-tick 长跑。R3 canonical profile 进一步固定 owner-authorized mission、launch-local ENU/passive attitude mapping、`680 kg`/`220 m/s` opening state、七项 exact R0 assets、独立 Decimal opening reference 和 scientific difference report。该 profile 在第一份 aerodynamic query 上保持 accepted `[0.2,0.6]` Mach domain 并受控停止；30 秒 trajectory、terminal metrics/verdict 与任何稳定性或 pitch overshoot 声明均保持未形成。

## 4. Scenario 冲突与 authority decision

### 4.1 已确认的非同一性

00A target 示例声明：30 s、100 Hz、guidance 20 Hz、初始经纬高 `31.2304° / 121.4737° / 1000 m`、speed 220 m/s、heading 90°、mass 680 kg、altitude command 1000 m。`YYZ-DEC-002`/ADR-0023 已选择 target-conformance 速率：`0.01 s` base，navigation/guidance/controller/actuator/observation 为 `100/20/50/100/25 Hz`，对应 interval `1/5/2/1/4` 与全零 offset。15 §4 的 guidance 10 Hz 保持 illustrative。冻结 Legacy 声明：2 s、50 Hz、guidance 10 Hz、launch Cartesian 标准轨迹初值 `r=[0,0,1000] m`、`v=[260,0,20] m/s`、mass 120 kg。

这三者在 mission intent、时间、频率、坐标、状态和质量上都不同。即使某些字段名称相似，也不能共享 RunId、input hash、trajectory expected 或 terminal/metric verdict。

### 4.2 决策账本与关闭证据

| Id | 决策问题 | 关闭时必须提交的 evidence |
| --- | --- | --- |
| `YYZ-DEC-001` | canonical independent-science mission | ADR-0025 已关闭：`mission.yyz.00a.abstract-engineering-baseline@1` 只声明抽象工程资格的内部一致性、确定性、domain preservation 与独立实现一致性 |
| `YYZ-DEC-002` | target rate set 与 20/10 Hz 差异 | ADR-0023 已关闭：interval `1/5/2/1/4`、全零 offset、HeldLatest age `4/1`；`r3.kernel-yyz-target-rate.probe` 提供 source/proof/Image 与执行证据 |
| `YYZ-DEC-003` | geodetic/launch Cartesian mapping | ADR-0024 已关闭：exact launch anchor、`frame.yyz.00a.launch-local-enu@1`、ENU axes、analytic position/velocity 与 non-anchor fail-closed cases |
| `YYZ-DEC-004` | attitude convention | ADR-0025 已关闭：passive Hamilton `q_I_B`、forward/right/down basis、body `omega_BI_B`、right-wing-down bank、`q/-q` 与 non-unit policies |
| `YYZ-DEC-005` | environment | ADR-0025 已关闭当前 profile：`environment.fixture.yyz.uniform@1` exact bytes/domain；Earth rotation/curvature 与 changing tangent plane 排除 |
| `YYZ-DEC-006` | aero | ADR-0025 已关闭当前 profile：`aero-table.fixture.yyz.multiaffine@1` exact bytes、strict trilinear lookup 与 closed axes；opening Mach 超域证据保留 |
| `YYZ-DEC-007` | guidance/control/allocation | ADR-0025 已关闭当前 profile：`guidance-control.fixture.yyz.altitude-pitch@1` exact gains/limits 与 ideal realization；无 allocator 扩展 |
| `YYZ-DEC-008` | actuator/propulsion/mass | ADR-0025 已关闭当前 profile：exact ideal actuator、`propulsion-response.fixture.yyz.main@1`、constant-geometry mass bytes；opening mass override 为 `680 kg`，惯量不缩放 |
| `YYZ-DEC-009` | form/closure/numerics | ADR-0025 已关闭当前 profile：accepted Closure、FrozenInterval classical RK4 与 failure semantics；author base step 为 `0.01 s` |
| `YYZ-DEC-010` | terminal/metric | ADR-0025 已关闭当前 profile：committed-boundary earliest predicate、30 s duration/tick 3000、terminal-observation ordering；不采用 6.4% overshoot expected |
| `YYZ-DEC-011` | independent reference/tolerance | ADR-0025 已关闭 comparison method：80-digit Decimal、exact identity/status、abs/rel `2e-12`、`0.01/0.005/0.0025` ladder；domain failure prevents derivative/convergence samples |
| `YYZ-DEC-012` | payload/schema/integrity | approved sidecars or ADR/schema、canonicalization、aggregate hash、compatibility |

Owner decision 必须引用具体 bytes/revision。Meeting note、口头选择、默认参数或本设计作者的计算不能代替批准记录。

## 5. Bundle 的概念组成

以下是信息拓扑，不是已批准文件布局：

```text
REF-YYZ-001
  identity-and-authority
    manifest
    decision-ledger
    source-and-asset-index
    implementation-and-environment-index
  target-conformance
    mission-source
    graph-plan-proof examples
    success-and-failure step journals
    observation-and-csv mapping
    diagnostic examples
    manifest-lineage-metric examples
  independent-science
    model definitions and validity
    formula cases and intermediates
    trajectory and terminal truth
    convergence and cross-tool report
  legacy-behavior
    isolated capture inputs and commands
    raw datasets/logs
    normalized semantic observations
    legacy-to-semantic mapping
  evaluation
    tolerance policy
    difference/classification report
    mutation report
    artifact index and hashes
```

每个 artifact 至少需要：稳定 bundle-local identity、authority/source refs、logical path、media/schema identity、raw byte count/hash、semantic input refs、producer implementation/environment、validity 和 consumer。聚合 input/artifact hash 的 path separator、排序、Unicode、number encoding 与 canonicalization 在 `YYZ-DEC-012` 前不冻结。

现有 `gnczmkn.fixture-manifest/1` 能表达 expected fact 和 artifact 名单，但不能完整表达模型 payload、逐文件 input hash、formula trace、trajectory、classification 或 cross-tool evidence。实现必须使用已批准 sidecar，或先通过 ADR 建立新 schema；不允许把机器关键字段塞进 notes 逃避验证。

## 6. 00A target conformance lane

### 6.1 Source fact 分级

| 00A 内容 | 当前分类 | 激活后的验收方式 |
| --- | --- | --- |
| mission id、definition/version、初始 author input | `approved/executable-domain-locked` | ADR-0024/0025 与 frozen source hash；package query 重算 mapping、duration/rates 并执行 domain checks |
| 100/50/25/20 Hz → interval 1/2/4/5 | `structural-exact/executable` | ADR-0023 已采用；整数算术 exact，proof 指回 source refs，短跑验证完整调用与 age 序列 |
| base tick 10,000,000 ns、30 s → 3000 intervals | `structural-exact/executable` | target source 已采用，integer duration/grid exact；两次运行均提交 terminal tick 3000 |
| 60 Hz at 100 Hz → 5/3，拒绝编译 | `negative-structural` | stable category/stage/subject/source refs；message text 不作 identity |
| `asset://...@sha256:91b7` | `illustrative/rejected-as-integrity` | canonical profile 已替换为七个真实 asset id、revision、source pointer 与三个全长 SHA-256 locks；短写继续拒绝 |
| graph/plan/proof/run ids 中的短后缀 | `illustrative` | 由批准 canonical inputs 实际派生，validator 重算 |
| tick 2500 高度 1001.73/候选 1001.69/5 patches | `needs_provenance` | 分离 transaction structure 与科学数值；后者只来自 approved trajectory |
| pitch 1.84°、elevator -0.62° | `needs_provenance` | 对应 formula/trajectory fact、FieldId、unit/frame/time/commit ref |
| pitch overshoot 6.4% / threshold 8% | `rejected-for-current-claim` | owner 将 R3 claim 限于抽象工程资格一致性；当前无 completed trajectory，也不采用或反向拟合该数值 |

### 6.2 Machine-valid artifact matrix

| Candidate case | 最低 payload | Exact invariants | 科学/runtime 状态 |
| --- | --- | --- | --- |
| `YYZ-CONF-SOURCE-001` | Mission source + SourceMap + full asset refs | ids、versions、units、rates、duration、hash | canonical programmatic source 已采用 author facts；frozen JSON 记录 full source/asset locks，无 parser/public schema claim |
| `YYZ-CONF-PLAN-001` | canonical graph、BindingPlan、ExecutionPlanDescriptor、proof index | subject/source/edge/operator refs；rate intervals | programmatic target source 已通过 Compiler→proof→Image；完整 source-file schema 保持开放 |
| `YYZ-CONF-BINDING-001` | RunBinding 与 immutable plan/source/package refs | no structural mutation；binding hash 可重算 | target-rate 与 distinct canonical exact Image/RunBinding 均已执行；canonical opening science inputs 已固定 |
| `YYZ-CONF-STEP-SUCCESS-001` | published epoch、deltas/candidate、journal、validation、ModelCommit | operator order、owner uniqueness、epoch +1、commit refs | tick 31 与 tick 3000 target runtime 已执行；canonical profile 在 first step 前的 aero query fail closed |
| `YYZ-CONF-STEP-FAIL-001` | 同一步的 pre-commit failure | committed state/epoch/tick/hash exact unchanged | canonical tick-0 OutOfRange 已由 real Session 验证为 tick/epoch 0、zero interval commits；transaction injections继续覆盖其他 stages |
| `YYZ-CONF-OBS-001` | ObservationBatch + semantic CSV mapping | FieldId、sample tick/time、ModelCommit ref、column mapping | interval 4/offset 0 已进入 Source→Plan→Proof→Image；runtime sink 与 CSV 保持 pending |
| `YYZ-CONF-DIAG-001` | 100/60 invalid schedule | compile rejected；stage/subject/source/ratio exact | Compiler pending |
| `YYZ-CONF-EVIDENCE-001` | RunManifest、LineageEdge、dataset/metric/report refs | all refs resolve to committed artifacts；hash closure | R4 producer pending |

Case id 仅是本设计候选。若现有 schema 已经固定别的 identity，实施采用权威 schema，不新增平行 contract。

R3 target-rate 可执行记录为 `r3.kernel-yyz-target-rate.probe`：生产 `TruthPassthroughNavigation` 保持同 tick truth provenance；guidance→controller age 序列受最大值 4 约束，controller→actuator age 序列受最大值 1 约束；tick-31 短跑覆盖完整 cadence、bit determinism、controlled/zero-control 可区分性和共享 provider 的双 Session 隔离。两次 3000-tick 工程运行均在 tick 3000 完成，正式 runwide result 为 `Complete / duration-complete`，覆盖 tick 0～3000、3001 个 committed samples 和 30.0 s；该记录继续标记 `target_conformance/science_verdict_pending`。

R3 canonical 记录由 `r3.kernel-yyz-00a-canonical.probe` 与 `r3.yyz-00a-canonical.oracle` 形成。canonical Image fingerprint 为 `e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c`。真实 Session 已初始化 `[0,0,1000] m`、`[220,0,0] m/s`、`680 kg` 与 level-east passive attitude，并对 requested 3000-tick horizon 执行第一步。uniform wind 令 relative airspeed 为 `210 m/s`，Mach `0.6176470588235294` 超出 exact aero table 上限 `0.6`；产品与独立 reference 均报告 tick-0 `OutOfRange / table-query`，trajectory committed intervals 为零。difference report 接受全部可得 opening fields，最大绝对差 `1.347066989204615e-14` 位于 tick-0 near-zero North velocity，abs/rel tolerance 为 `2e-12`，`unresolved_count=1`，terminal science verdict 未形成。

### 6.3 成功 step 与失败 step

成功 fixture 必须把以下事实分开记录：

1. `Publish` 只投影 epoch `e` 的 committed state；
2. due/held inputs 带明确 sample age 与 temporal relation；
3. `Invoke`/`Advance` 只产生 owner-scoped deltas/candidates；
4. `Stage` 收集完整 journal，不改变 committed store；
5. `Validate` 对完整 candidate set 执行 finite/domain/state invariants；
6. `Commit` 是唯一把 epoch `e` 变为 `e+1` 的边界；
7. Observation 引用该 ModelCommit，而不是 mutable buffer 或未提交 candidate。

失败 fixture 在 `Validate` 之前或之中注入确定性错误，记录 failing stage/subject/diagnostic，同时证明 committed state bytes/hash、epoch 与 tick 不变。不能通过只比较几个 scalar、捕获异常文本或重新初始化来伪造 rollback。

### 6.4 Observation 与 CSV

Observation truth 是带 FieldId、unit、frame、sample time、commit ref、validity/quality 的 columnar semantic batch。CSV 只是一种 EncodingPlan 结果：

- header/column mapping 必须由显式 encoding metadata 给出；
- CSV 列名、顺序、浮点格式、目录和换行不进入模型 truth；
- initial、cycle、terminal observation boundary 明确；
- target CSV 不需要复刻 Legacy 201 列；
- codec round-trip 比较 semantic fields，raw byte equality 只用于声明完全固定的 codec golden。

## 7. Independent formula lane

### 7.1 Convention 前提

R0-SCI-001 提供的 SI、frame、binary64、integer tick、ExactGrid 和 quaternion profile 已由 owner 接受。YYZ reference 必须显式携带：

- quantity semantic id、unit 与 geometry kind；
- expressed/reference frame、origin 与 axis convention；
- quaternion direction、component order、Hamilton product 与 angular-rate meaning；
- sample/interval/candidate/committed time relation；
- validity domain、finite policy 与 NumericalStatus；
- model/algorithm/asset implementation identity。

缺少其中任何一项时，不允许按数组位置或名称猜测语义。

### 7.2 公式覆盖矩阵

| Kernel/quantity | 最低 candidate equation/关系 | 必须记录的 intermediate | 仍需 owner 决定 |
| --- | --- | --- | --- |
| Translation | `r_dot^I = v^I`；`v_dot^I = R_B^I(q) F^B / m + g^I` | transformed force、mass reciprocal、gravity、acceleration | frame/origin、gravity、force point、closure strategy |
| Rotation | `omega_dot^B = I_B^-1 (M^B - omega^B × (I_B omega^B))`（仅在批准假设下） | angular momentum、gyroscopic term、net moment、condition/domain | inertia tensor/time variation、solver、singular policy |
| Attitude | Accepted passive Hamilton convention `q_dot_I_B = -0.5 * pure(omega^B_IB) ⊗ q_I_B` | quaternion norm、product terms、candidate norm | normalization timing、adapter |
| Air data | `v_rel^I=v_vehicle^I-v_airmass^I`；`pure(v_rel^B)=q_I_B pure(v_rel^I) inverse(q_I_B)`；`V=norm(v_rel^B)`；`alpha=atan2(w,u)`；`beta=atan2(v,sqrt(u^2+w^2))`；`qbar=0.5 rho V^2`；`Mach=V/a` | sample/frame identity、wind subtraction、quaternion products、u/v/w、horizontal speed、V/rho/a | canonical atmosphere/wind source、sensor validity、aero applicability |
| Environment | supplied uniform definition + finite inertial position/tick query → environment response | query identity、position/tick invariance、rho/a/gravity_I/airmass_velocity_I、air-data/rigid-core consumer values | canonical Earth/altitude model、constants/assets、wind profile、applicability |
| Guidance | approved estimate/target/phase → typed GuidanceCommand | LOS/reference/errors、mode/mechanism state、limits | law、rate、held/max-age、feedback semantics |
| Control | approved attitude/rate error → MomentCommand | reference/error terms、gain/feedforward、saturation | error representation、sequence/singularity、anti-windup |
| Allocation | Moment/ForceCommand → ActuatorCommand | effectiveness matrix、solve residual、unclamped/clamped command | solver、limits、failure/reconfiguration policy |
| Actuator | command + committed actuator state → sample/interval/candidate | lag target、rate-limit delta、position clamp | exact discretization/ODE、`dt=0` init、sample time |
| Propulsion | supplied `T>=0`、unit `d_B`、application point、intrinsic moment 与正消耗率 → response + mass-flow interval | `F_B=T*d_B`、Closure-owned `r×F`、interval duration、consumed mass | command/throttle mapping、engine dynamics、phase/config、fuel exhaustion |
| Mass | committed positive mass + positive-consumption interval → `m_candidate=m_committed-rate*duration` | duration、consumed mass、signed delta、candidate；非正 candidate 失败 | CG/inertia evolution、dry mass、fuel state、configuration semantics |
| Aero | supplied coefficients + qbar/reference geometry → AeroResponse；完整路径后续再接 state/air/actuator/config + assets | `qbar*S`、`[-C_A,+C_Y,-C_N]`、`[b*C_l,cbar*C_m,b*C_n]`、aero reference point、transported CoM moment | coefficient lookup、interpolation/extrapolation、asset applicability、canonical geometry |
| Closure | body-frame aero + prop + other force/moment responses → FormInput；`g^I` 保持独立惯性加速度 | each contribution by source/frame、application point、transported moment、total force/moment、configuration revision、validity interval | canonical response sources/application points、candidate/algebraic strategy |
| Integration | committed state + frozen/approved interval model → candidate | every RK stage input/derivative/state/status | solver, stage guard, normalization/projection policy |
| Termination | committed/published semantic facts → typed decision | each predicate, priority, tick/time/state ref | exact predicates、StopBefore/After、terminal observation |
| Metrics | committed Observation sequence → metric artifact | selected signal/window/baseline/target/extremum/denominator | pitch overshoot definition、threshold、invalid cases |

表中的 equation 是审计起点，不是对未决模型的批准。若实际模型包含 rotating frame、variable inertia、Earth curvature、aero unsteadiness、engine dynamics 或 algebraic closure，必须由 owner 以明确方程替换/扩展，并增加 independent cases。

`REF-YYZ-FORCE-MOMENT-CLOSURE-001` 已执行 fixture-local `FrozenInterval` 子集：每个贡献携带唯一 source identity、同一 body frame、configuration revision 与半开 validity interval，按 `M_CoM^B = M_application^B + r_CoM_to_application^B × F^B` 搬移并在 RK stages 内保持总量。Decimal 解析 reference 与独立 C++17 closure→rigid-core 路径交叉验证。Canonical response assets、完整 mass properties、configuration 和 gravity model 仍待后续确定；variable mass/CoM/inertia、CandidateState 与 AlgebraicSolve 不在此切片范围内。

`REF-YYZ-AIR-DATA-KINEMATICS-001` 已执行 fixture-local supplied air-data 子集：同一边界的 Truth、WindQuery 与 AtmosphereQuery 携带显式 frame、clock 和 tick identity；右手体轴采用 `x-forward/y-right/z-down`；`q_I_B` 按被动 Hamilton 方向把惯性系相对风速转到体轴。`V>0` 且 `sqrt(u^2+w^2)>0`，zero-relative 与 pure-lateral 输入直接产生 domain error。Rearward flow 保持可计算，角度和声速分母均无 epsilon/floor。80 位 Decimal reference 与独立 C++17 probe 覆盖五个公式 case、两条等价性、九条失败和四条 Legacy 风格 mutation。Canonical atmosphere、wind、sensor、aero applicability 与产品 contract 仍待后续确定。

`REF-YYZ-AERO-DIMENSIONALIZATION-001` 已执行 fixture-local supplied coefficient 子集：右手 `x-forward/y-right/z-down` 体轴下，`F_B=qbar*S_ref*[-C_A,C_Y,-C_N]`，`M_aero_ref_B=qbar*S_ref*[b_ref*C_l,c_ref*C_m,b_ref*C_n]`，并以 `r_CoM_to_aero_ref_B × F_B` 搬移到质心。输入要求有限非负动压、有限正参考面积/展长/弦长、有限系数和参考点，并要求 frame、clock、tick 与 configuration revision 同边界一致。80 位 Decimal reference 与独立 C++17 probe 覆盖三个公式 case、两个尺度等价变换、十条失败和三条 sign/scale/reference-vector mutation。Coefficient lookup、插值/外推、asset provenance/适用域、canonical 几何与产品 contract 仍待后续确定。

`REF-YYZ-UNIFORM-ENVIRONMENT-001` 已执行 fixture-local supplied uniform environment 子集：pure query 接收有限惯性系位置、非负 tick、clock 与 configuration revision，返回位置/时间不变的 `gravity_I_mps2`、`airmass_velocity_I_mps`、`density_kgpm3` 和 `speed_of_sound_mps`。响应在同一 sample identity 进入已接受的 air-data `v_rel/qbar/Mach` 与 rigid-core `force_I/m+gravity_I` 关系。80 位 Decimal reference 与独立 C++17 probe 覆盖普通 consumer link、零密度/亚单位声速边界、position/tick 等价、严格输入域和 Legacy-style altitude density/gravity decay。Earth/geodetic/rotation、海拔模型、压力/温度/湿度、风廓线/阵风、canonical constants/assets 与产品 contract 仍待后续确定。

`REF-YYZ-PROPULSION-RESPONSE-001` 已执行 fixture-local supplied propulsion response 子集：有限非负推力标量乘显式单位体轴方向形成 `F_B`；响应携带 `r_CoM_to_application_B` 与作用点固有力矩，Closure 独占 `r × F` 搬移。有限非负燃料消耗率在同一 clock/revision 的半开区间内积分，Mass consumer 以 `m_candidate=m_committed-rate*duration` 形成候选，非正候选直接产生 domain error。80 位 Decimal reference 与独立 C++17 probe 覆盖三个公式 case、区间分割等价、十条输入域失败，以及推力反向、预搬移力矩和质量增加 mutation。Command/throttle mapping、engine/fuel dynamics、dry-mass policy、完整 CG/inertia、canonical assets 与产品 contract 仍待后续确定。

`REF-YYZ-MASS-PROPERTIES-001` 已执行由既有 accepted invariants 固定的 projection-only 子集：显式 committed MassState 在 `t_k` 投影正质量、body-origin 到 CoM 的点坐标和关于 CoM 的完整对称正定体轴惯量；Closure 以 CoM 与作用点坐标差生成 application vector，rigid-core consumer 使用质量倒数、完整惯量、角动量和陀螺力矩。待提交 scalar mass candidate 在当前 `FrozenInterval` 内不可见，下一边界从显式 next committed state 投影。80 位 Decimal reference 与独立 C++17 probe 覆盖两个 consumer case、body-origin 坐标共同平移等价、严格输入域，以及 early-candidate、omitted-CoM-offset 和 diagonalized-inertia 失败。Fuel/ablation/dry-mass/CG/inertia evolution、configuration jump、canonical assets 与产品 contract 仍待后续确定。

### 7.3 Legacy 公式不可原样继承的地方

审计发现的 Legacy seed 包括：

- form 使用 body-to-inertial 旋转并以 `+0.5 * (q * omega_q)` 更新 quaternion；零/非有限 quaternion 变为 identity，近零 inertia 对角元静默变 1，mass 静默 clamp 到 `1e-9`；
- air-data 使用固定 `atan2` 分母保护定义 alpha/beta；
- guidance 用 inverse attitude 做 LOS，并把 dynamic pressure 放进 `actual_force_body_n`；
- actuator 使用 `alpha = clamp(dt/tau)` 的离散更新，`dt <= 0` 直接设 target；
- propulsion/mass 通过离散 in-place fuel/burn 更新；
- aero 用最近行查询、三个 channel 平均形成 `F_CA`、roll rate 固定为 0；
- interaction query 修改 `last_input_`，目标设计要求 pure query；
- termination 按 free-text max-time/altitude/speed/hit-range 路径，summary 没有 00A 的 pitch overshoot metric。

这些事实进入 `observed_legacy` 和 disposition review，不自动进入 independent expected。特别是 15 明确要求删除“动压伪造 actual force”，因此 target formula case 必须把该路径作为拒绝 mutation。四元数 raw 分量也不能在不同 convention 之间直接比较；先通过经审查 adapter 转成同一物理旋转，再比较 orientation/derived vector facts。

### 7.4 Formula trace record

每个 formula case 至少记录：

- case/fact identity、equation revision 与 authority approval；
- 全部 literal semantic inputs 与逐文件/聚合 hash；
- 每个 input/intermediate/output 的 quantity、unit、frame、geometry、time relation；
- 分支、lookup bracket/weight、limit/saturation、iteration/stage 和 NumericalStatus；
- high-precision/analytic expected 的 producer/version/environment；
- binary64 implementation 的回显输入、输出与 error；
- field-specific comparator/tolerance ref；
- raw/derived evidence 与 artifact index hash。

Probe 不能读取 expected 后回显，也不能调用 target/Legacy 实现来生成自己的 truth。若公式只能通过数值积分求解，expected producer 与 target solver 至少在算法、实现语言或精度路径上具备经审查的独立性，并用 convergence/交叉工具补强。

## 8. Independent trajectory lane

### 8.1 分阶段建立 reference

完整 6DoF trajectory 在 `YYZ-DEC-001`–`011` 关闭前无法诚实生成。激活后按以下顺序构建：

1. **Input freeze**：canonical mission、assets、model choices、environment、rates、initial state、terminal 和 numerical policy 全部有 hash；
2. **Formula closure**：第 7 节各 kernel 的 pure cases 先通过，包含跨 kernel force/moment/mass balance；
3. **Short-horizon cases**：单 tick、单 rate-boundary、一个 guidance interval、一个 observation interval，检查 temporal/commit semantics；
4. **Open-loop trajectory**：冻结 command/actuator 或简化已批准输入，隔离 form/closure 数值误差；
5. **Closed-loop trajectory**：批准 guidance/control/actuator/plant 全链，报告逐状态误差与 stability/metric；
6. **Terminal/failure cases**：exact-grid normal terminal、domain failure 不提交、invalid rate compile failure；
7. **Cross-tool/convergence**：独立高精度或不同 solver reference 与 target candidate 分别运行；
8. **Legacy comparison**：只在 semantic overlap 上比较，并附 disposition；
9. **Target runtime handoff**：R3 后让 Session 消费同一 source/reference，不让 expected 依赖 Session 输出。

### 8.2 Trajectory record

每个 trajectory sample 最低包含：

- case id、tick、exact time、sample/commit identity；
- position、velocity、orientation、angular rate、mass/CG/inertia；
- actuator、propulsion、aero、closure 与 command 的批准 semantic subset；
- frame/unit/quality/status；
- source formula/intermediate refs；
- terminal/metric inputs；
- producer and artifact refs。

存储可以是 JSON、columnar binary 或其他批准 encoding；validator 按 semantic FieldId/sidecar 读取，不依赖文件成员/列/内存顺序。若保留每 tick 数据过大，可以保存批准的全量 critical fields、抽样观测和 rolling/terminal integrity proofs，但不能只留最终点来声称全轨迹正确。

### 8.3 Numerical independence 与 convergence

- reference truth 优先使用解析/高精度公式和更严格的数值策略；
- target candidate 不读取 expected，不共享 target kernel 或 Legacy source；
- 逐状态族分别报告 position、velocity、orientation、angular rate、mass 等误差，禁止把不同单位混成一个无量纲 max norm；
- orientation 使用批准的几何误差（例如 relative rotation angle），不直接用存在双覆盖的 quaternion component equality；
- dt ladder、reference step、order/ratio、roundoff floor 和比较时刻必须经 Runtime Numerics Lead 批准；
- 只在相同 physical time/grid/terminal policy 对齐后比较；不得用插值掩盖 off-by-one；
- non-finite、domain、solver failure 都是显式状态，不允许 tolerance 吞掉。

## 9. Termination 与 metric 设计

### 9.1 Terminal reference

每个 terminal case 必须声明：

- predicate id、inputs、unit/frame、threshold 与优先级；
- evaluation 使用 committed/published 还是 candidate state；
- ExactGrid、StopBefore/StopAfter 或 root-location policy；
- terminal tick/time/state/ModelCommit；
- terminal observation 是否先生成以及引用哪个 commit；
- typed reason/status；free-text 只作 display；
- 对未触发 predicate 的负证据。

Legacy 的 `max_time at t=2` 可以成为旧行为事实，不能自动决定 canonical 30 s target terminal。00A 的 30 s 也只有在 mission 被批准且 duration/grid exact 后才能成为 independent expected。

### 9.2 Pitch overshoot

“6.4%”当前没有可审计定义。批准 metric 至少要冻结：

- 输入 FieldId 是 truth pitch、estimated pitch、command error 还是其他 signal；
- step/event onset、baseline、command/steady target、settling/window endpoint；
- 正负方向、wrap/angle convention 和 extremum 选择；
- denominator；当目标变化为零或接近零时的 invalid/alternate policy；
- transient exclusion、sample rate 与 interpolation policy；
- threshold 8% 的 research requirement/approval ref；
- 产生 6.4% 的 Observation refs、计算 intermediate、implementation/hash。

若无法从批准 inputs 重算 6.4%，应将该值替换为 independently generated value 或明确标为 illustrative；不能通过调整 window 或 denominator 追配文档结果。

## 10. Tolerance report

### 10.1 Exact 与 numeric 分界

以下默认 exact，不接受数值 tolerance：identity/version、source/asset/implementation hash、unit/frame/geometry、rate interval、tick、event/operator order、owner、commit ref、terminal kind、classification、artifact completeness 与 finite/status。

Numeric tolerance 只作用于声明字段，并至少包含：

- comparator kind：absolute、relative、ULP、angular/geometric、invariant residual 或 convergence；
- scale/denominator、absolute floor 与 approved limit；
- 字段/状态族和 time/grid domain；
- truth precision/uncertainty、target rounding/error budget；
- owner、approval ref、rationale 与 version；
- observed max/location/distribution，不只给 pass/fail。

### 10.2 禁止的 tolerance 用法

- 一份全局 epsilon 覆盖所有量和单位；
- 使用 Legacy 单元测试的 `1e-9`/`1e-12` 直接成为 target policy；
- 放宽阈值直到 00A illustrative 数值或 Legacy output 通过；
- 对 hash、identity、tick、ordering、classification 或 missing field 应用 tolerance；
- 让 NaN/Inf 在比较器中静默 pass；
- 只比较 final state 或聚合 scalar，隐藏中间时刻/单状态族退化；
- 未声明 reference uncertainty、solver order 或 platform profile却声称 bitwise scientific portability。

Tolerance report 必须分别报告 formula、short-horizon、trajectory、terminal、metric 和 Legacy semantic comparison。Legacy capture threshold 与 target acceptance tolerance 是不同记录。

## 11. Frozen Legacy capture 与 semantic mapping

### 11.1 Fresh capture 程序

1. 从固定 ZIP 解压到新建的隔离 build workspace，核对 archive、prefix 和关键 entry hash；
2. 使用 `R0-LEG-001` 已复现的 Windows x64 / w64devkit GCC 16.2.0 / CMake 4.4.2 / Ninja 1.13.2 / Eigen 3.4.0 作为 canonical candidate；任何环境变化产生新 environment identity；
3. 核对 mission include closure、参数、aero/trajectory assets 和全部 byte hash；
4. 构建并用记录的普通 CLI 路径运行 YYZ mission 两次；
5. 保留每次 command、cwd、exit、stdout/stderr、environment、raw CSV、summary 与 byte hash；
6. 解析 CSV header，生成 semantic sidecar；保留 raw 与 normalized 两份及各自 hash；
7. 验证 101 rows、`t=0..2`、tick/time、terminal 与双跑 semantic hash；
8. 逐 semantic field 建立 Legacy → approved reference/target mapping；未知字段保持 unresolved；
9. capture/probe 只写隔离 workspace 和 evidence staging，不修改 frozen archive/extracted tree；
10. 最终 evidence 由 artifact index 完整列出，缺失 raw bytes 或 hash 即失败。

若必须 instrumentation，先记录 observation gap。临时 source overlay 需要 ADR/owner 批准并保存 overlay diff/hash；overlay run 不能冒充未修改 Legacy 的 canonical capture。

### 11.2 Semantic comparison 纪律

Legacy CSV 初值/trajectory input、published output、candidate state 和 summary metric 是不同语义。Mapping 至少声明：

- legacy header/path 与解析规则；
- target/reference semantic identity；
- unit/frame/geometry/time relation；
- sample boundary 与 terminal relation；
- optional transform/attitude adapter 的 definition/hash；
- disposition、difference rationale、owner/approval；
- comparator/tolerance 或 exact policy。

没有 mapping 的列不能因数值接近而自动匹配。Target 不存在的旧字段可以 Retire；target 新字段显示 `target_pending`，不能用缺失 Legacy 对照空通过。

### 11.3 候选 disposition

| 类别 | 候选事实 | 状态/说明 |
| --- | --- | --- |
| Preserve | `t_k`、sample/interval 边界、candidate 后统一提交、成功运行的可复现 semantic facts | 待逐 fact owner approval；实现形状可变化 |
| Preserve after adapter | 力/矩闭合、刚体平移/转动方程、物理 orientation | 仅在 convention/model 假设一致且 adapter 被批准后 |
| Retire | 24 节点、201 列、priority 数字、registration/provider/lookup 名、CSV 列序/路径、free-text reason、old Mission shape | accidental structure，不进入 target expected |
| Retire | dynamic pressure 伪造 actual force、mutable query `last_input_` | 15 已要求删除/纯化；应有 guard |
| NeedsDecision | 最近行 aero、三通道平均 `F_CA`、roll-rate=0、`dt<=0` 初始化、mass/inertia silent clamps | 尚无 owner 决策，不能计 pass |
| NeedsDecision | canonical opening Mach 与 accepted aero domain 的兼容选择 | 速率、scenario、frame、mass 与 exact assets 已由 ADR-0023～0025 固定；owner 需选择新的 qualified aero/environment asset 组合或修订 author speed，overshoot 6.4% 已退出当前 claim |
| Fix | 当前无自动批准项 | 只有 defect id、独立复现、影响、owner decision 和替代 evidence 齐全后才能使用 |

## 12. Difference report

每个 comparison row 至少包含：

- stable fact/field/case id；
- left/right lane、run、source、artifact、semantic field 与 sample identity；
- exact/numeric comparison、values/errors/tolerance ref；
- migration disposition：Preserve/Fix/Retire/NeedsDecision；
- rationale class：ScientificInvariant、DeclaredModelChoice、NumericalPolicy、ImplementationDefect、AccidentalStructure 或 NeedsDecision；
- owner、approval ref、target mapping status；
- raw evidence refs 和 validator version。

Summary 必须分别给出 `match`、`approved_difference`、`retired`、`target_pending`、`unresolved` 和 `invalid`。G1 需要 `unresolved = 0`；`target_pending` 只能用于明确属于后续 R1–R3 runtime 的项，不能隐藏 R0 本应完成的 source/formula/reference 缺口。

当前 `reports/r3-yyz-001-00a-difference.json` 采用 exact identity/status 与逐字段 abs/rel `2e-12`。所有可得 opening comparisons 均 accepted；near-zero North velocity 在 tick 0 形成最大绝对差 `1.347066989204615e-14` 和最大相对差 `1`，该字段由绝对阈值控制。`unexplained_difference_count=0`；唯一 unresolved coverage row 是 opening domain failure 后缺失的 tick 1～3000 trajectory，因此 `unresolved_difference_count=1`。`candidate_terminal_science_verdict=false`。

## 13. 完整性、派生与重放

1. 所有 source/input/asset/implementation/environment/artifact 都以 raw bytes 和全长 SHA-256 固定；
2. semantic/canonical hash 只由版本固定、经批准的 canonicalizer 生成，并同时保留 raw hash；
3. expected 只能由独立 producer 从固定 inputs 生成，或由有权 reviewer 明确签署；
4. generator、validator、comparator、canonicalizer 的 source/build identity 都进入 manifest；
5. 同一 input/profile 至少运行两次；raw 中允许变化的字段必须逐项声明，normalized hash 不得任意忽略；
6. artifact index 列出 logical path、bytes、hash、media/schema、producer、inputs 和 validity；孤儿或缺失引用失败；
7. rerun 不能覆盖第一次 evidence；两次 run identity 与 outputs 都保留；
8. Windows/Linux profile 分开记录；平台差异由 evidence 分类，不通过选择性删除解决；
9. products、independent reference 和 Legacy 分别构建/运行，再由外部 comparator 消费 artifacts；
10. 禁止 target include/link/call Legacy，禁止 independent reference import target/Legacy kernels。

## 14. Mutation 与生产 evaluator

生产 validator/comparator 必须同时用于正常 bundle 和 [`R0-SCI-003`](../tasks/backlog.json) 的直接失败用例。覆盖面至少包括：

| Evaluator | 必须拒绝的代表错误 |
| --- | --- |
| Identity/provenance | 三 lane 合并、source/asset/implementation hash 漂移、短 hash、重复/未知 id |
| Semantic contract | unit/frame/geometry/time relation 缺失或交换、CSV 位置访问 |
| Plan conformance | 非整数 rate、proof/source/subject/operator ref 断裂 |
| Formula/domain | attitude sign/order、zero quaternion、aero lookup/sign、fake force、non-finite/singular silent clamp |
| Temporal/transaction | actuator/mass/prop off-by-one、partial candidate commit、failure 后 epoch/tick 改变 |
| Observation/evidence | candidate observation、错误 commit ref、terminal 未观测、lineage/artifact ref 缺失 |
| Scientific comparison | mixed-unit norm、final-only、overshoot 无定义、缺 tolerance、过宽 tolerance |
| Legacy disposition | Retire 进入 expected、Fix 无 defect approval、NeedsDecision 计 pass |
| Reproducibility | 双跑 semantic drift、normalized fields 未声明、artifact index/hash 漂移 |

Mutation fixture 不得绕过 production parser 或直接断言一个独立测试函数。每次 rejection 都要返回稳定 category/stage/subject/source refs；human message 可以变化。

## 15. 实施顺序与 gate

### Slice 0：dependency 与 authority closure

- 关闭 `R0-SCI-001`、`R0-LEG-002`、`R0-SPEC-001`；
- 指派 assignee/reviewer，合法激活 backlog；
- 关闭 `YYZ-DEC-001`–`012`；
- 决定 sidecar/schema/ADR 路由。

### Slice 1：target structure fixtures

- 录入 00A source、plan/proof、binding、success/failure step、observation、diagnostic 与 lineage payload；
- 标注逐值 provenance/validity；
- 结构 machine-valid；尚无对应可执行证据的 runtime assertions 保持 `target_pending`；
- mutation 覆盖短 hash、rate、refs、commit/observation。

### Slice 2：formula oracle bundle

- 固定 mission/model/assets 和 conventions；
- 为第 7 节每个 kernel 建 pure case/intermediate；
- 独立 producer 与 target-candidate probe 零共享；
- 先关闭 formula/domain/closure，再允许 full trajectory。

### Slice 3：independent trajectory、terminal 与 tolerance

- short/open-loop/closed-loop cases；
- high-precision/cross-tool/dt-convergence；
- terminal 与 pitch metric；
- 逐字段 tolerance 和完整 evidence index。

### Slice 4：Legacy fresh capture 与差异分类

- 从 ZIP 隔离双跑；
- 保留 raw CSV/summary/log/command/environment；
- 生成 semantic mapping；
- 每项差异获得 disposition/owner/approval；
- 证明 target/reference 不依赖 Legacy topology/encoding。

### Slice 5：bundle closure 与后续 handoff

- 生产 evaluator 拒绝全部 mutation；
- repository/CI/evidence integrity 通过；
- `REF-YYZ-001` 转为 executable；
- R1 kernels、R2 Compiler 与 R3 Session 后续消费同一 bundle；
- runtime conformance 在对应阶段有真实 evidence 后关闭，不在 R0 伪造。

每个 slice 单独自审、验证并提交 Git；上一个 slice 没有审查证据时不进入下一个。

## 16. 退出检查

- 三条 lane 的 identity、authority、inputs、outputs 和 validity 没有混淆；
- 00A/15/Legacy 的 scenario/rate/frame/mass/asset 冲突全部有 decision 或明确阻断；
- 所有 00A 示例值为 approved expected、illustrative、target pending 或 rejected 之一，无未分类值；
- mission/assets/implementations 有 full hash，短 illustrative hash 全被拒绝；
- 6DoF formula coverage 包含 inputs、intermediates、domain、independent expected 和 field tolerance；
- trajectory 包含初值、时间序列、terminal、metric、convergence，不只 final state；
- pitch overshoot 可从引用 Observation 重算，或 6.4% 已从 expected 中移除；
- Legacy raw output fresh capture、双跑、semantic sidecar 和 artifact index 完整；
- Preserve/Fix/Retire/NeedsDecision 逐 fact 分类，`unresolved = 0`；
- 20 项 mutation 与额外 completeness cases 均由生产 evaluator 拒绝；
- 零产品 ↔ Legacy link/runtime dependency，零 CSV 列序/节点/provider 偶然结构依赖；
- fixture/schema/backlog 状态转换符合治理，新增公共 contract 有 ADR；
- Debug/Release、Windows/Linux hosted CI、repository verification 与 evidence hash 通过；
- Scientific Authority、Validation Lead、Runtime Numerics Lead、Model SDK Lead、Architecture owner 具名审查。

## 17. 准备设计自审

- 证据 lane 审查：target 结构、Legacy 观察和 independent truth 三者分离，未用一个 lane 填补另一个 lane 的空缺；
- 来源审查：target seed、archive、关键 mission/assets/source 与既有 run hash 均可回溯；
- 科学审查：20 Hz、680 kg、launch-local ENU、passive quaternion、exact R0 assets 与 tolerance 已由 owner-authorized facts 和 ADR-0023～0025 固定；aero domain incompatibility 保持显式，overshoot 未进入 claim；
- 失败审查：short hash、convention、domain、time/commit、observation、metric、difference 和 determinism 都有生产 mutation；
- 架构审查：R3 canonical profile 已沿 package query、Compiler、Image 与 Session 执行；independent reference 与产品/Legacy 零 kernel/link/runtime 依赖；
- 状态审查：R3-YYZ-001 保持 `review` 与 `assignee:null`，R3-FIX-001/R3-DIA-001/R4+ 未启动；该状态不构成 owner gate approval 或任务完成声明。
