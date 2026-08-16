# 代码评审报告 — CI 门禁接入与 Workflow 重构（dogfood 新标准）

- 评审日期：2026-08-16
- 评审人：Code Review Expert（火眼眼）
- 评审对象：
  - `.github/workflows/test.yml`（commit `2f86af7` 重构 + `4e40a81` 门禁接入）
  - `tests/test_sdk_source_invariants.py`（docstring 同步）
- 评审方式：静态走读 + CI 三 job 命令本地复现 + `function_body` 提取实地验证
- 优先级：**🔴 P0 / 🟡 P1 / 💭 P2**（与 `CODE_REVIEW_STANDARD.md` 一致）
- 目的：用团队审查标准审本会话新提交的两处 CI 改动，验证它们真的让 CI 变绿且门禁可信。

---

## 0. 结论总览

| 级别 | 数量 | 说明 |
| --- | --- | --- |
| 🔴 P0 | 0 | 无未初始化 / 契约错 / 行为漂移 |
| 🟡 P1 | 0 | 无阻塞合并项；三 job 本地全绿、门禁提取正确 |
| 💭 P2 | 4 | 2 项本轮引入已顺手修，2 项门禁稳健性建议 |

**总体结论：通过（带 4 项 P2，其中 2 项审查中发现并已当场修复）。** CI 从"缺 SDK 必红"变成"默认三 job 全绿 + SDK 全量构建按需 opt-in"，双版本一致性门禁真正落进 CI 且对当前源提取正确。

---

## 1. 审查证据（CI 命令本地复现，对应 `test.yml` 三个 job）

| Job | 精确命令 | 结果 |
| --- | --- | --- |
| `unit-tests` | `g++ -std=c++17 -O2 -I include tests/AutoDimensionAlgorithmsTests.cpp -o algo_tests && ./algo_tests` | `All AutoDimensionAlgorithms tests passed`（EXIT=0）✅ |
| `numeric-regression` | `python3 tools/test_autodim_geometry.py` | `27/27 tests passed — ALL GREEN`（EXIT=0）✅ |
| `source-gate` | `python3 tests/test_sdk_source_invariants.py --source-2025 …/2025/… --source-2026 …/2026/…` | `SDK source invariants passed`（EXIT=0）✅ |

**`function_body` 提取实地验证**：对当前 2026 源，
`CopyDimensionPresentationFrom` / `ApplyDimensionVariableTransaction` / `EditSelectedDimensions`
三者均正确提取到**函数定义体**（以 `{` 开头、内容为定义，如 `if (!dimension || …)` / `if (dimensions.empty()) return 0;`），
未误抓调用点 → 门禁断言落在正确代码块上。

---

## 2. 逐条审查（按 `CODE_REVIEW_STANDARD.md` §5）

### §5.3 可维护性 / §5.6 VW 专属 — 通过项

- ✅ **双版本一致性已落 CI**：`source-gate` job 独立、不依赖 Windows VW SDK，满足 §3 闸门接线要求。
- ✅ **paths 触发已覆盖**：`sdk-projects/**`、`include/vwad/**`、`tests/**` 均在 `paths:` 内，满足 §3 “必须加入 CI 路径触发”的硬性要求（之前未覆盖会漏跑）。
- ✅ **正确解决既有 CI 红**：原单 `test` job 在 ubuntu CI 因无 Windows VW SDK / MSVC 必败；现拆为三 SDK 无关 job（真绿）+ opt-in `sdk-build`（`if: vars.VW_SDK_AVAILABLE == 'true'`），stock CI 不再因缺 SDK 而红。

### 💭 P2-1 文档漂移（本轮引入，已修）

`test_sdk_source_invariants.py` docstring 仍写 “independent of the SDK-dependent `test` job”，
但 `test` job 已在 `2f86af7` 重命名为 `sdk-build`。已改为 `sdk-build` job。
属本轮改动引入、未同步——审查中发现即修（零风险）。

### 💭 P2-2 `sdk-build` 缺显式 x64 平台（本轮引入，已修）

`windows-latest` 默认 VS 生成器平台为 **Win32**；Vectorworks 是 64-bit 宿主，插件必须 **x64** 构建，
否则链接 VW SDK 的 64-bit lib 失败。已加 `-A x64` 并注释。仅影响 opt-in 路径，但能在有人启用时避免踩坑。

### 💭 P2-3 `function_body` 不锚定函数定义（稳健性，建议）

`function_body(source, name)` 用 `source.find(name)` 取**首次出现**，再找其后第一个 `{`。
若未来某次改动让 `EditSelectedDimensions` / `ApplyDimensionVariableTransaction` 的**调用点**出现在**定义之前**
（如在某入口函数里先调用），`find` 会抓到调用点所在代码块而非定义体，
导致后续断言静默误判（假阴/假阳）。**当前源首次出现即定义**（已实地验证），故未触发。
建议加固：用 `re.search(r"CAutoDimensionObj::" + re.escape(name) + r"\s*\(", source)` 锚定定义起始，而非裸 `find(name)`。

### 💭 P2-4 normalize 仅替换两个具体文件名（稳健性，建议）

字节一致性 normalize 只替换 `vw-autodim-runtime-{2025,2026}.txt` 与 `vw-autodim-align-{2025,2026}.txt`。
若未来新增其他版本戳差异（如第三处带 2025/2026 的文件名），normalize 不会处理，会误报 “drift”。
保守做法有意只覆盖已知两处，但建议在注释里说明“仅这两处是约定的合法差异”，避免后人误以为漏覆盖。

---

## 3. 标准自验证

- CI 三 job 全绿，证明 §3 闸门在 CI 真正生效（双版本一致性 + 数值回归 + 纯算法单测）。
- §5.6 VW 专属“两份必须保持一致的源是否有一致性测试兜底” → `source-gate` 正是兜底，已落实。
- 无 P0/P1，门禁对当前源可信。P2-3 / P2-4 为门禁长期稳健性，建议后续加固，但不阻塞合并。

---

## 4. 本次审查的修复

- P2-1（docstring 过时 job 名）、P2-2（`sdk-build` 加 `-A x64`）已当场修复，与本报告同 commit 提交，本地未推送（延续“不推”决策）。
- P2-3 / P2-4 留作门禁后续加固项，未动（避免引入新风险）。
