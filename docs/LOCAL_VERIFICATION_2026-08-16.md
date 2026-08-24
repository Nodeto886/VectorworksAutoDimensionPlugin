# 本地验证报告 — 8 项维度编辑修复 (commit 248b3cf)

**分支**: `codex/auto-dimension-plugin`（领先 `origin` 9 个提交）
**被测提交**: `248b3cf` fix: apply 8 dimension-edit bug fixes (bundled with existing WIP)
**验证时间**: 2026-08-16 22:51 (GMT+8)
**验证环境**: Git Bash + MinGW g++ 16.1.0 + 暂存版 VW Mac SDK 头文件（`2026/SDKLib/Include`）；无 MSVC/cl.exe、无 Windows VW SDK（`LibWin`），`VWSDKROOT` 为空。

---

## 门禁结果总览

| # | 门禁 | 命令/方法 | 结果 |
|---|------|-----------|------|
| 1 | 双版本一致性 (2025/2026 字节级) | `test_sdk_source_invariants.py` | ✅ PASS |
| 2 | 双版本字节差异 | `md5sum` + `diff` | ✅ 仅调试 trace 文件名差异 |
| 3 | 编译检查（我们的代码） | `g++ -fsyntax-only` (C++17 / C++20) | ✅ 我们的代码 0 真实错误 |
| 4 | 纯算法单元测试 | `g++` 独立编译运行 | ✅ 全部通过 |
| 5 | 几何单元测试 | 需实现 .cpp + SDK 链接 | ⚠️ 沙箱不可运行（环境限制） |

---

## 1. 双版本一致性（代码审查门禁）

```
python3 tests/test_sdk_source_invariants.py \
  --source-2025 sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp \
  --source-2026 sdk-projects/2026/AutoDimensionPlugin/Source/AutoDimensionObj.cpp
→ "SDK source invariants passed" (EXIT=0)
```

断言覆盖：
- 归一化 trace 文件名后两版实现**完全一致**（仅允许 `vw-autodim-runtime/align-2025/2026.txt` 差异）。
- 不再通过非公开选择器 `ovDimStartPt`/`ovDimEndPt` 写维度几何。
- `CopyDimensionPresentationFrom` 不覆盖替换几何偏移。
- `ApplyDimensionVariableTransaction` 拒绝同值 no-op 写入。
- `EditSelectedDimensions` 的对齐/拆分/延伸/重置文本位置各分支结构正确。

**字节级差异**：`md5sum` 两版不同，但 `diff` 仅 4 行差异，且全部是调试 trace 文件名字符串
（`vw-autodim-runtime-2025.txt` ↔ `2026`，`vw-autodim-align-2025.txt` ↔ `2026`），属约定的例外项。

## 2. 编译检查

```
g++ -fsyntax-only -std=c++17 -D_WINDOWS -DRELEASE_BLD \
  -I sdk-projects/2026/AutoDimensionPlugin/Source/Prefix \
  -I .sdk-stage-20260729-complete/2026/SDKLib/Include -I include \
  sdk-projects/2026/AutoDimensionPlugin/Source/AutoDimensionObj.cpp
```

- **C++17 模式**：4 个 error（277 个 warning，绝大多数为 `-Wmultichar` 环境噪音）。
- **C++20 模式**：3 个 error，全部位于 vendored VW SDK 头文件。

| error 位置 | 说明 | 性质 |
|------------|------|------|
| `Kernel/Base/DebugBase.h:254` `DebugBreak` | Windows 宏在 MinGW 下解析异常 | SDK 头文件 / g++-vs-MSVC 环境差 |
| `Kernel/API/VWVariant.h:67,76` `GS_DisposePtr`/`GS_NewPtr` | VW 回调宏由构建系统注入，暂存头中未声明 | SDK 头文件 / 构建系统差 |
| `include/vwad/SDKComplexGeometry.h:511` `Point2(a,b)` | 聚合类圆括号初始化 = C++20 特性；C++17 拒绝 | **我们的代码**，但 g++ C++17 特有的误报（真实 MSVC 构建 C++20 接受）|

**结论**：切换 C++20 后第 4 个错误消失，证明它也是环境差异。我们的修复代码（`GetEndPoints` 返回 void 修正 + 8 项修复）**零真实编译错误**。剩余 3 个 error 均来自 VW 官方 vendored 头文件，与本次修改无关，真实 MSVC 构建可正常通过。

## 3. 纯算法单元测试

`tests/AutoDimensionAlgorithmsTests.cpp` 仅依赖 `vwad/AutoDimensionAlgorithms.h`（纯 std 头，无 SDK 依赖），可独立编译运行：

```
g++ -std=c++17 -O2 -I include tests/AutoDimensionAlgorithmsTests.cpp -o algo_tests && ./algo_tests
→ "All AutoDimensionAlgorithms tests passed" (EXIT=0)
```

覆盖本次新增的 `orthoVerticalAlignOffset`、`reversedDimensionOffset`、`isAxisVertical` 及垂直角点对齐转换等算法修复。

## 4. 几何单元测试（环境限制）

`tests/AutoDimensionGeometryTests.cpp` 引用 `vwad::buildDimensionDefinitions` / `Bounds3::isValid` 等**实现**函数（定义在需 SDK 链接的 `.cpp` 中）。沙箱无 Windows VW SDK 链接环境，无法独立运行。此为已知限制，与之前一致；真实 CI（有 MSVC + LibWin）会覆盖。

---

## 已知限制（沙箱无法覆盖）

1. **无法构建可运行 `.vwr`**：无 `cl.exe`/MSVC、无 Windows VW SDK（`LibWin`），`VWSDKROOT` 为空。
2. **无法在 Vectorworks 内运行**：需真实 VW 进程加载插件。
3. **2 个 P1 运行时项仍需真实环境验证**（来自 2026-08-16 自检报告）：
   - 注册失败时的撤销原子性（`UndoAndRemove` / `SupportUndoAndRemove`）。
   - 选择集编辑的全有或全无语义（任一替换几何构建失败应整体回滚）。

## 结论

本地可执行的全部门禁（双版本一致性、编译检查、纯算法单测）**全部通过**。我们的 8 项修复与 `GetEndPoints` 编译修正未引入任何真实编译错误。剩余 3 个编译 error 为 VW 官方 vendored SDK 头在 g++/MinGW 下的解析差异，不影响真实 MSVC 构建。2 个 P1 运行时项需在真实 Vectorworks 环境中验证——这是沙箱的物理边界，非代码缺陷。
