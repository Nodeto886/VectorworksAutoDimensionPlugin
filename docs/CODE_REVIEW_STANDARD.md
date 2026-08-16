# 代码审查标准与流程（团队版）

> 适用范围：团队所有仓库（C++/Qt + Vectorworks SDK、TypeScript/React、Flutter/Dart、Lua、Python 工具链）。
> 维护者：技术负责人。最后更新：2026-08-16。
> 设计原则：**"编译通过 ≠ 功能正确"**——任何改动必须由"针对性测试 + 契约核对 + 一致性闸门"三重把关，而不是凭"能编过"放行。

---

## 1. 目标与原则

| 原则 | 说明 |
| --- | --- |
| 审查是**教学**，不是**守门** | 每条评论要讲清"为什么"和"怎么改"，让作者下次自己就能避免。 |
| **小步提交、早审查** | 单 PR 改动越小，审查质量越高。超大 PR（>800 行或跨多个不相关模块）必须拆分。 |
| **自动化优先** | 能由 CI / Skill 拦住的（编译、测试、双版本一致性、lint），不占用人工评审精力。人工只审"意图正确性与边界"。 |
| **契约先于实现** | 调 SDK / 第三方库 / 跨模块接口前，先核对真实签名与返回值语义，再写调用代码。 |
| **不改坏现有行为** | 修复 / 重构不得悄悄改变既有几何、偏移符号、撤销语义等"看起来无害"的细节。 |

---

## 2. 角色与职责

- **Author（作者）**：保证改动可编译、自测通过、写好自审清单；PR 描述说清"为什么改、改了哪、怎么验证"。
- **Reviewer（评审人，≥1）**：对**正确性 / 安全 / 可维护 / 性能 / 测试覆盖**负责；必须亲自读改动代码，不能只点 approve。
- **Maintainer（合并人）**：确认所有准入闸门（§3）绿了、评论已解决，再合并。小团队里 Maintainer 可由 Reviewer 兼任。

> 单人开发时，**Author 必须自己跑一遍自审清单（附录 B）并留痕**，且至少过一道自动化闸门；重大改动（几何/撤销/跨版本）建议隔天再以"旁观者视角"复审。

---

## 3. 审查准入闸门（Entry Gates）—— 提 PR 前必须满足

| 闸门 | 工具 / 命令 | 失败含义 |
| --- | --- | --- |
| 编译（含 SDK 源） | `vw-plugin-compile-check` skill（g++ `-fsyntax-only` 走真实 SDK 头）；或 `AutoDimensionPlugin.vcxproj` 本地 MSVC 出包 | 引入类型/签名错（如 `GetEndPoints` 返回 `void` 却当 `bool` 判） |
| 单元 / 算法测试 | `ctest --test-dir build/test --output-on-failure`；`vwad_geometry_tests` + `vwad_algorithm_tests` | 纯函数回归 |
| 数值回归 | `python3 tools/test_autodim_geometry.py` | 几何读数漂移 |
| **双版本一致性**（VW 项目专属） | `python3 tests/test_sdk_source_invariants.py --source-2025 …/2025/… --source-2026 …/2026/…` | 2025/2026 源漂移（仅允许两处版本戳 trace 文件名不同） |
| Lint / 格式 | 各语言既有规范（clang-format / prettier / dart format） | 风格噪声 |

**CI 接线要求**：`test.yml` 当前 `paths` 触发**未覆盖** `sdk-projects/**`、`include/vwad/AutoDimensionAlgorithms.h`、`tests/test_sdk_source_invariants.py`。标准生效后必须把这三条加入 CI 路径触发，否则双版本不变量改动会漏跑。

---

## 4. 审查流程

```
1. 选题拆分  → 一个 PR 只解决一件事；跨模块改动拆成序列 PR。
2. 本地自审  → Author 跑 §3 全部闸门 + 附录 B 自审清单，截图/留痕。
3. 提 PR     → 用附录 A 模板填写（目的 / 改动点 / 验证 / 风险）。
4. CI 闸门   → 所有 job 绿；任一红 = 自动打回，不进人工评审。
5. 人工评审  → Reviewer 按 §5 清单逐条核对，用 P0/P1/P2 标注（见 §6）。
6. 修改回复  → Author 逐条回应：已改的注明 commit；不采纳的必须说明理由。
7. 批准合并  → Maintainer 确认闸门全绿 + 评论清零，Squash/Merge。
8. 合并后复查 → 重大改动合并后 24h 内由另一人做一次"旁观者复审"。
```

---

## 5. 审查清单（按维度）

> 优先级沿用团队现有语言：**🔴 P0（必须修，阻塞合并）/ 🟡 P1（应该修）/ 💭 P2（建议）**。
> 与 Code Review Expert 标记对应：🔴←blocker，🟡←suggestion，💭←nit。

### 5.1 正确性（Correctness）
- 🔴 有没有**未初始化变量 / 空指针 / 越界 / 除零**？边界条件（空集合、退化输入、零长向量）是否都挡了？
- 🔴 是否**悄悄改变了既有行为**（偏移符号、坐标基准、undo 配对、默认参数）？几何/符号类改动尤其要核对。
- 🔴 SDK / 第三方调用**返回值语义**是否与代码假设一致？（例：`GetEndPoints` 是 `void` 不是 `bool`；`GetObjectVariable` 失败返回 `false` 而非抛异常。）
- 🟡 浮点比较是否用容差（`kGeometryTolerance`），而非 `==`？阈值是否与图纸比例强相关（绝对 vs 相对）？
- 🟡 循环/递归有无**死循环 / 整数溢出 / 符号翻转**风险？

### 5.2 安全（Security）
- 🔴 外部输入 / 文件 / 网络数据是否校验？路径是否防穿越？
- 🔴 有无注入面（SQL / 命令 / 模板 / 序列化）？是否走参数化 / 白名单？
- 🟡 密钥 / token 是否误入源码或日志？

### 5.3 可维护性（Maintainability）
- 🟡 命名是否自解释？"魔法数"是否提为具名常量（如 `kGeometryTolerance`）？
- 🟡 是否把**可单测的纯逻辑**从 3000+ 行巨型函数里抽出来（见本轮 `orthoVerticalAlignOffset` / `reversedDimensionOffset` 抽离）？
- 🟡 重复逻辑是否提取？两份必须保持一致的源（如 2025/2026）是否有**一致性测试**兜底？
- 💭 注释是否解释"为什么"（不是复述"做什么"）？复杂几何/算法是否有推导出处？

### 5.4 性能（Performance）
- 🟡 有无 N+1 / 重复遍历 / 每帧分配？大数据集（>170 灯具、长音频）下是否退化？
- 💭 不必要的值拷贝 / 可 `const ref` 的地方？

### 5.5 测试（Testing）
- 🔴 修复类 PR 是否带**复现用例 / 回归测试**？纯函数改动是否有单测？
- 🟡 边界与异常路径是否覆盖（垂直转角偏移符号、反转点、撤销失败回滚）？
- 🟡 新增的"看起来通用"的数学，是否补了**项目专属语义**用例（如 VW 垂直转角尺寸偏移约定）？

### 5.6 语言 / 项目专项
- **C++ / Qt / VW SDK**：内存所有权（`AddAfterSwapObject`/`AddBeforeSwapObject` 配对）、`WorldCoord` 即 `double`、预编译头顺序、跨平台宏（`_WINDOWS`/`__APPLE__`）。
- **VW 插件专属**：双版本源字节一致（§3 闸门）；撤销原子性——任何"新建换旧"路径失败都要 `UndoAndRemove()` 回滚整个未结束事务（原仅 merge 有，本轮已推广到 convert/align/split/points）；诊断 trace 文件名必须带版本戳且被一致性测试规范化。
- **TypeScript / React（paperwork-studio）**： Convex action 与前端 wizard 的类型契约；未提交 WIP 多时**先审查状态再继续**（团队约定）；SSO/401 鉴权路径。
- **Flutter / Dart**：PU 模式必须对齐全量 `FullSizePlusWorker`；纯时间模型（frames/ms/seconds），禁用 BPM/beat。
- **Lua / Python 工具链**：全局副作用、路径硬编码、shebang 与 venv 隔离。

---

## 6. 评论与沟通规范

- 每条评论带**优先级 + 文件名:行号 + 为什么 + 建议改法**。
  > 🔴 **正确性：未初始化输出** `AutoDimensionObj.cpp:658`
  > `GetEndPoints` 在 SDK 中返回 `void`，`if (gSDK->GetEndPoints(...) && ...)` 是硬编译错。
  > 建议：改为语句调用，仅用"非退化"校验结果采纳。
- **提问优于臆断**：意图不清时先问"这里为什么要取负？"，而不是判错。
- **分离阻塞与非阻塞**：P0 必须解决才能合并；P2 可建 issue 跟踪，不堵合并。
- **赞扬好代码**：抽离纯函数、补了项目专属单测、写好推导注释——点名表扬，强化好模式。

---

## 7. 定义完成（Definition of Done）/ 合并标准

一个 PR 可以合并，当且仅当：

1. 🔴 全部 P0 已解决；
2. §3 所有 CI 闸门绿（含双版本一致性）；
3. 至少 1 名 Reviewer 批准，且所有评论已逐条回应/解决；
4. PR 描述完整（附录 A），自审清单（附录 B）已勾选；
5. 无遗留"改坏现有行为"风险（§5.1 第二项）。

---

## 8. 与本团队工具链集成

- **`vw-plugin-compile-check` skill**：每次改 `AutoDimensionObj.cpp` 前先过类型检查，杜绝"编过但功能坏"。
- **CTest + 双版本不变量**：几何/算法改动必须附带可跑的回归；一致性测试是 2025/2026 同源的硬保障。
- **Code Review Expert（火眼眼）**：大改动可调用本专家做系统性审查，输出即按 P0/P1/P2 分级。
- **未提交 WIP 审查顺序**（团队既有约定）：paperwork-studio 等堆积大量 WIP 时，**先 `git status` 审查状态，再决定动作**，不盲跳。

---

## 附录 A：PR 模板（建议提交到 `.github/PULL_REQUEST_TEMPLATE.md`）

```markdown
## 目的
<!-- 为什么改？关联 issue / bug 编号 -->

## 改动点
<!-- 列文件 + 一句话说明；跨版本改动注明 2025/2026 同步 -->

## 验证
- [ ] 编译闸门（compile-check / vcxproj）通过
- [ ] `ctest` 全绿
- [ ] 数值回归 `tools/test_autodim_geometry.py` 通过
- [ ] 双版本一致性 `test_sdk_source_invariants.py` 通过（VW 项目）
- [ ] 新增/修改的测试覆盖了什么

## 风险与遗留
<!-- 哪些行为可能变化、哪些待实测、哪些开了 issue -->
```

## 附录 B：Author 自审清单（提 PR 前勾选）

- [ ] 改动可编译（含 SDK 源，过 compile-check）
- [ ] 所有测试绿（C++ + 数值回归 + 双版本）
- [ ] 没有未初始化 / 空指针 / 越界 / 除零
- [ ] SDK 调用返回值语义已核对（不是凭记忆）
- [ ] 没有悄悄改变既有偏移符号 / 坐标基准 / 撤销配对
- [ ] 可单测逻辑已抽离并有单测（含项目专属语义）
- [ ] 双版本源字节一致（仅允许版本戳 trace 文件名不同）
- [ ] PR 描述 + 自审清单已填
