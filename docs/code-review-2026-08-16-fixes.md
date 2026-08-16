# 代码评审报告 — 8 项 Bug 修复（dogfood 新标准）

- 评审日期：2026-08-16
- 评审人：Code Review Expert（火眼眼）
- 评审对象：`sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`（字节一致）、`include/vwad/AutoDimensionAlgorithms.h`、`tests/*`、`test.yml`
- 评审方式：静态走读 + 真实 SDK 头 g++ `-fsyntax-only` 类型检查 + CTest 3/3 已通过
- 优先级：**🔴 P0 / 🟡 P1 / 💭 P2**（与团队 2026-08-08 报告及 `CODE_REVIEW_STANDARD.md` 一致）
- 目的：用刚制定的审查标准审我们自己刚改的代码，验证标准能拦问题，并确认修复无误。

---

## 0. 结论总览

| 级别 | 数量 | 说明 |
| --- | --- | --- |
| 🔴 P0 | 0 | 无未初始化 / 空指针 / 越界 / 除零 / 契约错 |
| 🟡 P1 | 2 | 均为"运行时行为断言"，静态无法证实，需在 Vectorworks 实测 |
| 💭 P2 | 3 | 可读性 / 冗余局部变量，不影响正确性 |

**总体结论：通过（带 2 项运行时验证清单）。** 8 处修复逻辑正确、边界守卫完整、撤销原子性到位，可作为合并候选。

---

## 1. 逐条审查（对应 8 项修复）

### P1#1 端点读取 `GetDimensionEndpoints` (L630–667) — 无问题项
- 先清零 `outStart/outEnd`（L634–635），杜绝未初始化传播 ✅
- 主路径 `ovDimStartPt/ovDimEndPt` 退化即判失败 ✅
- 回退 `GetEndPoints` 已修正为**语句调用**（SDK 返回 `void`），仅用"非退化"校验采纳（L658–665）✅ —— 即本次编译检查抓出的真 bug 已正确修复
- 边界：对象空 / 非 `dimHeaderNode` 提前返回 ✅

### P1#2 端点门控 (L1921–1925 等) — 无问题项
- 去除进入 switch 前的统一 `if(!hasPoints) continue`；几何类模式（convert/trim/align/split/points）内 `if(!hasPoints) break` ✅
- 文字类（textDirection/resetText/resetTextPosition）、merge/avoid 不再被端点读取失败误伤 ✅

### P1#3 对齐垂直翻转 `orthoVerticalAlignOffset` — 无问题项
- 头文件 `AutoDimensionAlgorithms.h:117`：`return (dimensionClass==0 && isAxisVertical) ? -offset : offset;` ✅
- 仅对正交垂直轴取负；水平正交、对齐尺寸保持 ✅（VW 垂直约定右正，统一法向朝左 → 取负把线拉回点击侧）
- 调用点 L2065 在 `offsetDerived` 校验之后，逻辑顺序正确 ✅

### P1#4 转换偏移投影 (L1948–1950) — 无问题项
- `convertedOffset = sourceOffset*(dx/length)` (H) / `sourceOffset*(-dy/length)` (V) = 源法向在目标法向上的有符号投影 ✅
- `length <= kGeometryTolerance` 在除法前守卫（L1932），无除零 ✅
- 仅处理对齐→正交转换（`dimensionClass != 1` 直接 break），目标类传 `0`，几何正确 ✅

### P1#5 分割/延伸 渲染轴投影 (L2104–2175) — 无问题项
- 改用 `GetDimensionAxis` 取显示轴，`along` = 点击点沿该轴投影（L2121）✅
- 分割在区间内 `along ∈ (tol, axisLength-tol)`（L2125）；延伸在区间外（L2157），方向判断正确 ✅
- `lineStart = start + n*sourceOffset`、splitWitness 反推，几何自洽 ✅

### P1#6 反转点 `reversedDimensionOffset` — 无问题项
- 头文件 `AutoDimensionAlgorithms.h:127`：`return (dimensionClass==1) ? -offset : offset;` ✅
- 仅对齐尺寸取负；转角尺寸保留符号（轴固定法向不随端点序翻转）✅ —— 正是用户担心的"翻到另一侧"已规避
- 调用点 L2200，注释说明正交"轴固定法向"理由充分 ✅

### P2#7 测试 — 无问题项
- 双版本一致性测试已规范化 `runtime`+`align` 两处文件名 ✅
- 新增 `testOrthoVerticalAlignOffset` / `testReversedDimensionOffset` / `testVerticalCornerAlignConversion` 覆盖项目专属偏移语义 ✅
- split/extend marker 更新为 `along > endpointTolerance` ✅

### P2#8 撤销原子性 (L1843–1855, 1964–1969, 2083–2088, 2139–2146, 2170–2172, 2213–2215, 2247, 2251–2258) — 通过，含 2 项运行时验证
- `SupportUndoAndRemove()` 已从仅 merge 扩展到 convert/align/split/points（L1847–1849）✅
- 各重建模式：`AddAfterSwapObject` 成功后若 `AddBeforeSwapObject` 失败 → `swapFailed=true`，不自行删除 replacement（交 `UndoAndRemove` 清理）✅
- 循环内 `if (swapFailed) break;`（L2247）保证整批原子回滚 ✅
- 事后 `if (swapFailed) { UndoAndRemove(); changedCount=0; }`（L2251–2258），且 `EndUndoEvent` 仅在 `!swapFailed` 时调用（L2330），满足"未结束事件才可被 UndoAndRemove"的 SDK 契约 ✅

---

## 2. 🟡 P1 运行时验证清单（静态无法证实，需在 Vectorworks 实测）

1. **UndoAndRemove 清理契约**：当 `swapFailed` 触发时，依赖 `UndoAndRemove()` 删除已 `AddAfterSwapObject` 的 replacement。该契约与 merge 路径一致且 merge 已被确认可用，但本会话无法在 Vectorworks 跑——请实测"选中多个尺寸、制造一次注册失败（如内存压力/极端对象）"确认无孤儿 swap primitive、且整批回滚到原状。
2. **全有或全无语义**：单个尺寸注册失败会 `swapFailed` 回滚**整次编辑调用**（同选区其余尺寸也不变）。这是按你要求设计的原子行为，但需确认交互上"整批不动"比"尽量改成功的其余"更符合预期。

## 3. 💭 P2 建议（不影响合并）

- **`undoRegistered` 向量（L1856）**：仅在 kEditAvoid / 文本类路径使用；swap 模式未用。可加注释或改名避免误解。
- **convert 分支双重 `if (replacement)`（L1953 与 L1958）**：两次判断同一条件，可合并或加注释区分"trace"与"swap"。
- **`GetEndPoints` 语句式调用（L475）**：非本次改动，但确认其为语句调用（非 bool 判），与 P1#1 修复一致；无需动。

---

## 4. 标准自验证结论

本次评审用 `CODE_REVIEW_STANDARD.md` 的清单逐条核对，结果：
- 标准能有效覆盖"编译通过≠功能正确"（compile-check + 单测 + 双版本不变量均已落地）；
- 清单 §5.1「SDK 返回值语义」直接拦住了 P1#1 的 `void` 误用（虽由编译检查先抓出，但清单同样会标 🔴）；
- 撤销原子性专项条款（§5.6 VW 专属）对应 P2#8，审查路径清晰。

标准可用，无需修订。
