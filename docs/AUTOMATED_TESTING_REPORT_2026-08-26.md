# 自动化测试体系 + 算法 Bug 修复报告 (2026-08-26)

**仓库**: `C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`
**日期**: 2026-08-26
**范围**: 为插件建立「免人工日常回归」的一键自动化测试体系；新增随机属性/变形/差分测试；修复该测试体系发现的 2 个算法核心真实 Bug。

---

## 0. 背景与目标

原测试体系只有**固定用例**（CTest 手写用例 + Python 数值回归固定案例）。固定用例的缺点是：只验证「你恰好想到的输入」，无法证明算法在随机/极端输入下仍满足数学性质。

目标：
1. 让开发者改完代码后**跑一条命令**即可回归全部可自动化的门禁，不必每天手动开 Vectorworks。
2. 用性质测试（property-based）+ 变形测试（metamorphic）+ 差分测试（differential）覆盖手写用例漏掉的输入空间。

---

## 1. 交付物清单

| 文件 | 类型 | 作用 |
|---|---|---|
| `scripts/run-tests.ps1` | 新增 | 一键跑 7 道门禁，输出 PASS/FAIL 汇总表 |
| `tests/AutoDimensionAlgorithmsPropertyTests.cpp` | 新增 | C++ 随机属性/变形/鲁棒性测试（18 组性质，固定种子可复现） |
| `tools/test_autodim_geometry.py` | 扩展 | +6 个随机差分/不变性测试（32/32） |
| `include/vwad/AutoDimensionAlgorithms.h` | 修复 | `convexHull` 上凸包下界守卫、集合覆盖冗余剪枝计数 |
| `CMakeLists.txt` | 修改 | 新增 `vwad_algorithm_property_tests` 目标 + Python 回归接入 CTest |
| `.github/workflows/test.yml` | 修改 | CI `unit-tests` job 增加属性测试编译运行 |
| `README.md` | 修改 | 自动化测试章节补充 run-tests.ps1 与覆盖说明 |

---

## 2. 一键运行器 `scripts/run-tests.ps1`

### 快速门禁（不编译真实插件，约几十秒）

```powershell
.\scripts\run-tests.ps1 -SkipBuild
```

### 全部门禁（含真实 MSBuild，约 1 分钟）

```powershell
.\scripts\run-tests.ps1
```

### 7 道门禁

| # | 门禁 | 内容 | 基线 |
|---|---|---|---|
| 1 | CTest | 几何 + 算法 + **属性测试**（Ninja/g++ Release） | PASS |
| 2 | Python 数值回归 | `tools/test_autodim_geometry.py`（32 用例） | PASS |
| 3 | SDK 源一致性 | `tests/test_sdk_source_invariants.py`（2025==2026） | PASS |
| 4 | g++ 语法检查 C++17 | 对 `AutoDimensionObj.cpp`，仅本项目源错误判 FAIL | PASS |
| 5 | g++ 语法检查 C++20 | 同上 | PASS |
| 6 | MSBuild 插件 2025 | 真实 Release 构建 | PASS |
| 7 | MSBuild 插件 2026 | 真实 Release 构建 | PASS |

> g++ 语法检查对「已知 SDK 头文件噪音」放行：`DebugBase.h` DebugBreak、`VWVariant.h` GS_NewPtr/GS_DisposePtr、及其级联进 `SDKComplexGeometry.h` 的 Point2 报错，均为 MinGW 特有、MSVC 真实构建不出现（与 `vw-plugin-compile-check` skill 结论一致）。门禁只对 `AutoDimensionObj.cpp` / `include/vwad/*` 中的真实错误判 FAIL。

---

## 3. 新增测试内容

### 3.1 C++ 属性测试 `tests/AutoDimensionAlgorithmsPropertyTests.cpp`

全部使用固定种子 `std::mt19937_64`，CI 可复现；零第三方依赖（纯 std）。

| 被测算法 | 性质 |
|---|---|
| `convexHull` | 凸性；包含全部输入点；旋转/平移/缩放不变性 |
| `minimumAreaBounds` | 包含全部输入点；面积 ≥ 凸包面积；旋转保持面积、缩放面积×k² |
| `findDominantAxis` | 方向单位长；confidence∈[0,1]；折叠角∈[0,45°]；旋转后方向跟随旋转、confidence 不变 |
| `segmentIntersection` | 交点落在两条线段上；参数∈[0,1]；交换输入顺序交点一致 |
| `segmentEllipseIntersections` | 交点满足椭圆方程；落在查询线段上；最多 2 个交点 |
| `assignIntervalLanes` | 同车道区间彼此间隔 ≥ gap，互不重叠 |
| `orderSegmentChains` | 每条非退化边恰好出现一次；链内边端点连续 |
| `selectCoveringCandidates` | 每个可覆盖特征必被覆盖；候选索引不重复 |
| `optimizeLabelLayout` | 平移等变性（位移不变）；候选网格边界内移动 |
| `TolerancePolicy` | 随尺度单调不减；不小于 absolute 下限 |
| 鲁棒性 | 退化/极端输入（1e12、1e-12、共线、重合点）不崩溃、不产 NaN/Inf |

### 3.2 Python 差分/不变性 `tools/test_autodim_geometry.py`（+6 用例）

| 用例 | 方法 |
|---|---|
| `test_random_intersections_match_independent_reference` | 与独立线性系统求解对拍，且交点必在虚拟线与候选段上 |
| `test_random_collect_count_matches_brute_force` | 去重后交点集合与暴力参考完全一致 |
| `test_random_translation_invariance` | 平移 ±1e6 后交点数不变 |
| `test_random_scale_stability` | 尺度 1e-3~1e3 等比缩放后交点数不变 |
| `test_random_no_crash_on_degenerate` | 退化/极端输入不崩溃、交点为有限值 |
| `test_random_*` 沿用固定种子 `random.Random(0xAD_A1D_2F)` | 可复现 |

---

## 4. 测试体系发现的 2 个真实 Bug（已修复）

### Bug 1: `convexHull` 上凸包缺下界守卫

**Evidence**（修复前，300 随机案例 vs 独立参考凸包）：

```text
POINT mismatch iter=1
SIZE mismatch iter=6: ours=5 ref=6
SIZE mismatch iter=7: ours=4 ref=5
mismatches=104/300
```

**Finding**：`convexHull` 合并下/上凸包时，上凸包遍历用 `output.size() >= 2` 作为弹栈下界，可把**已完成的下凸包右端顶点**当「非左转」弹掉。参考凸包对拍 104/300 不一致；示例输入含 `(94.77, 79.21)`、`(92.87, -7.38)` 两个真右端顶点被丢弃。

**Path**：`include/vwad/AutoDimensionAlgorithms.h` 中 `append` 增加 `minimumSize` 参数 —— 下凸包用 1，上凸包用 `lowerSize`（`output.size() > minimumSize` 才允许弹栈），保住下凸包已完成部分。

**Evidence**（修复后）：`mismatches=0/300`；属性测试全部通过。

### Bug 2: `selectCoveringCandidates` 冗余剪枝重复计数导致漏覆盖

**Evidence**（修复前，固定随机种 0xC0FFEED，iter=3）：

```text
featureCount=4 candidates=29 selected=1 missing=0
cand[28] cost=0.76 feats=[1,2]
selected: [28]
```

**Finding**：冗余剪枝阶段 `counts[feature]` 按候选特征列表逐条累加，同一候选内重复特征被多次计数。例如候选 `{3,0,0,0}` 把特征 0 计 2 次，使「唯一覆盖特征 0 的候选」被误判为冗余而删除，最终留下未覆盖特征。修复前随机案例大量触发。

**Path**：剪枝改为先统计**每个特征由哪些不同候选覆盖**（`coverers[feature]` 去重后 append），候选冗余条件改为「该候选覆盖的每个特征都有 ≥2 个不同候选覆盖」；`counts <= 1` 判定随之改为 `coverers[feature].size() < 2`。

**Evidence**（修复后）：`All AutoDimensionAlgorithms property tests passed`；Python 32/32；CTest 全绿。

---

## 5. 最终回归结果（2026-08-26）

```text
>>> CTest (geometry+algorithm+property) ... PASS
>>> Python numerical regression ... PASS
>>> SDK source invariants ... PASS
>>> g++ syntax check (C++17) ... PASS
>>> g++ syntax check (C++20) ... PASS
>>> MSBuild plugin (2025) ... PASS
>>> MSBuild plugin (2026) ... PASS
=============================== TEST SUMMARY ===============================
  7 passed, 0 failed
===========================================================================
```

---

## 6. 仍需人工的边界（诚实声明）

- **Vectorworks 实机交互冒烟**：模式栏、尺寸创建/编辑、撤销重做、立面工作平面恢复等必须宿主加载插件才能验证，无法在 CI 或沙箱自动完成。对应清单：`docs/smoke-test-checklist.md`（12 项）。
- 新测试体系把「可自动化的部分」全部自动化；人工只需关注实机交互那一层。

---

## 7. 附：复现命令

```powershell
# 一键全门禁
.\scripts\run-tests.ps1

# 仅快速门禁
.\scripts\run-tests.ps1 -SkipBuild

# 单独跑属性测试（g++ 直编，与 CI 一致）
g++ -std=c++17 -O2 -I include tests/AutoDimensionAlgorithmsPropertyTests.cpp -o /tmp/algo_property_tests
/tmp/algo_property_tests

# 单独跑 Python 数值回归
python tools/test_autodim_geometry.py
```
