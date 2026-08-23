# 阶段门

## Gate 总览

| Gate | 阶段 | 核心结果 | 解锁内容 |
| --- | --- | --- | --- |
| B0 | Bootstrap | 构建、测试、文档、任务和 legacy 边界可验证 | R0 |
| G0 | R0 架构闭合 | 术语、变化分流、proof schema、依赖守卫闭合 | R0 科学验收 |
| G1 | R0 科学基线 | minimal 3DoF、YYZ、CAVH oracle bundle 可执行 | R1 |
| G2 | R1 Model Ecosystem | algorithms、recipes、mechanisms 与 packages 可独立测试 | R2 |
| G3 | R2 Plan Firewall | source 可生成完整、可解释、不可变 plan | R3 |
| G4 | R3 事务语义 | normal/failure/cancel/terminal 有唯一提交结果 | R3 纵向切换 |
| G5 | R3 纵向证明 | YYZ、多实体、故障和 activation fixture 通过 | G6 |
| G6 | R3 legacy 独立 | 新 runner 只依赖新 Compiler/Session | R4、R6 |
| G7 | R4 Evidence Firewall | observation、manifest、lineage 与 sink failure 闭合 | R5 |
| G8 | R5 研究闭环 | DATCOM/配平/分析/仿真/报告纵向链通过 | M2 |
| G9 | R6 Experiment/Python | 多 Session、case、reset/step 与 Python 复用同一语义 | R7/R8 |
| G10 | R7/R8 多入口边界 | authoring、control、snapshot 和 backend 无内部捷径 | M3 |

## Gate 决策记录

每次 gate 先运行当前阶段的 executable preflight、相关 CTest 和 repository verification。preflight 只判断技术输入与显式 blocker，不代替 owner 作出阶段门结论。

仓库所有者作出决定后，在 `docs/quality/gate-decisions/` 保存一份最小记录，包含 gate 范围、实际运行结果、未解释科学差异数量、当前限制、`Passed`/`Conditional`/`Failed` 结论和明确解锁范围。记录不复制 CI 输出，不生成机器互签或验收回执。

`Conditional` 不能解锁会消费缺失契约的后续阶段。

## R3 unsigned technical preflight（2026-08-24）

本节只记录当前可执行技术输入，不构成 owner decision、签名或 M1 记录。

| Gate | 技术状态 | 现有证据 | 明确 blocker |
| --- | --- | --- | --- |
| G4 | **Not ready** | normal/failure/cancel/terminal、command retry/fatal、multi-owner rollback 与 activation transaction matrices 可执行 | 架构 11 §8.2 的 `CriticalEvidence` sink 与 commit 后 `ExternalEffect` 路径尚未实现 |
| G5 | **Technical evidence ready; owner decision pending** | `R3-YYZ-001` 已完成；stuck-actuator、two-entity causal 与 inactive-child activation fixtures 覆盖 owner、因果、原子提交/回滚、归因、隔离和确定性 | 仓库所有者尚未接受 `R3-FIX-001` 与 `R3-DIA-001`，也未作 G5 决定 |
| G6 | **Not ready** | source/CMake guards 继续证明 Legacy 不进入当前产品 targets | `apps/cli/main.cpp` 仍是 composition/toolchain skeleton，尚无只走 Application→Compiler→Session 的新 runner；完整 R3/G6 deletion guard 仍为 `deferred-by-gate` |

`R3-GATE-001` 保持 `planned`，signed M1 evidence bundle 尚未交付。R4～R8 继续锁定；本 preflight 不创建 gate 结论、release、tag 或解锁记录。

R0 直接检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/validate-r0-gate-readiness.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/validate-r0-gate-readiness.ps1 -RequireDecisionReady
```

第一条核对技术输入；第二条在 owner 决定前检查仍需处置的真实 blocker。
