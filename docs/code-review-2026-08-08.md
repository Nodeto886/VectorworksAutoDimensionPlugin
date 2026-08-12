# Vectorworks AutoDimensionPlugin 代码评审报告

- 评审日期：2026-08-08
- 仓库：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`
- 评审对象：工作区未提交改动（重点：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`，2026 版与其同构）+ `docs/autodim-feature-design.md`
- 评审方式：只读。全部结论基于 `Get-Content` / `Select-String` 实际读取源码、工作区 diff（`git diff`）、2025/2026 双版本 diff，并与本机 SDK 头文件交叉核对（`VectorworksSDK\2025\SDK\SDKVW(784374)\SDKLib\Include`）。
- 结论分级：P0（必须修）/ P1（应该修）/ P2（建议）/ 无问题项。

---

## 0. 结论总览

| 级别 | 数量 | 摘要 |
| --- | --- | --- |
| P0 | 0 | 未发现崩溃、越界、除零、空指针、undo 表损坏等必须立即修复的问题 |
| P1 | 2 | ① ManualBlock 插入点纵向定位尺寸的 offset 基准错误（用 `maxX` 代替 `minX`，尺寸线偏离点击点约一个包围盒宽度）；② 交线标注对墙体/楼板（kWallNode/kSlabNode）等建筑构件采集不到边，典型用法会静默失败 |
| P2 | 9 | 见各节：尺度相关硬编码容差、圆/弧取包围盒边、转换 offset 几何跳位、剪齐行为变更、字号强制移除、isometric 视图守卫文案、链式/合并需实测项等 |
| 无问题项 | 若干 | 线段求交 det/t/u 公式、WorldCube 构造、立面守卫、undo 配对、2025/2026 一致性等（详见 §8） |

---

## 1. 交线标注（CreateVirtualLineIntersectionDimensions，L641–714 + HandleComplete 分支 L2247–2268）

### 1.1 线段求交 det/t/u 公式与边界 —— 无问题项（已逐步验算）

设虚拟线 `v = (virtualLine.dx, virtualLine.dy)`，起点 `firstPoint`；候选边 `s = (segmentDx, segmentDy)`，起点 `segment.start`；`q = segment.start - firstPoint`。

- 代码 `det = segmentDx*virtualLine.dy - virtualLine.dx*segmentDy`，即 `cross(s, v)`（L672）。Cramer 法则解 `t*v - u*s = q`，矩阵 `[[vx,-sx],[vy,-sy]]` 行列式恰为 `cross(s,v)=det`。
- `t = (sx*qy - qx*sy)/det`（L676）✓、`u = (vx*qy - qx*vy)/det`（L677）✓——两式与 det 定义完全自洽。
- 边界：`t`/`u` 均以实际向量为参数（`SMeasuredSegment` 存的是原始世界坐标，非单位向量，见 `SDKComplexGeometry.h` 的 `AddWorldSegment`，`length=hypot`），因此 `[0,1]` 就是整段，`t/u ∈ [-1e-6, 1+1e-6]` 的容差合理（L678）。
- 共线/平行：`|det| ≤ 1e-6` 直接跳过（L673）。共线重叠时交点不唯一，跳过是合理取舍；但**若虚拟线与某条边完全重合，该边的端点不会成为交点**，链会在该处断裂（L673 的注释可补一句）。属设计取舍，非缺陷。
- 端点重合：两条边在虚拟线同一点相交时，交点被 1e-4 阈值去重（L680–685）✓。
- 排序：按 `(p - firstPoint)·v` 投影排序（L694–698），与 `t` 单调等价 ✓。
- 除零/空指针：`BuildLineMeasurement` 已挡零长（L643–646）；`intersections.size() < 2` 提前返回（L689）；循环从 `index=1` 开始（L704）✓。

**P2-1（建议）**：去重阈值 `1e-4` 与 `kGeometryTolerance=1e-6` 都是**绝对世界单位**，与绘图比例强相关。微缩图纸（如坐标量级 0.01）上，间距小于 1e-4 的真实交点会被误合并；超大图纸上又可能把两条边在顶点处因浮点差产生的两个"几乎同点"交点算成两个。建议改为与边长/虚拟线长成比例的相对阈值（如 `max(1e-6*length, 1e-9)`），并统一到 `kGeometryTolerance` 语义。

### 1.2 候选对象选择 —— 无问题项（过滤安全），但建筑构件采集有缺口（P1）

- 预选优先：`selectedSources` 非空时只处理预选对象（L649–651），行为与「先选对象再点两点」的交互一致。
- 否则 `ForEachObjectN(allVisible)` + 包围盒过滤（L657–663）。该过滤是**保守且无漏检**的：若某边与虚拟线段相交，交点必同时落在两者 AABB 内，故对象 cube 与虚拟线 AABB 必然相交。✓
- `IsSupportedSource` 排除了 `dimHeaderNode` 与插件自身代理对象（L136–141），不会把已存在的尺寸当成求交对象 ✓。

**P1-1（应该修）**：`ComplexGeometry::Collect`（`SDKComplexGeometry.h` `TraverseObject` L276–337）只处理 line / polygon / polyline / qPoly / box / rBox / oval / arc / group / symbol / parametric / extrude / sweep。**kWallNode(=68)、kSlabNode(=71)（见 `Objs.TDType.h` L134/L137）没有 case，也不在容器回退名单里，采集结果为空**。而交线标注最典型的用途恰是"沿一条虚拟直线标注墙体间距"（平面图场景）。当前行为：墙体/楼板对象被 `IsSupportedSource` 接受进入候选，但 `Collect` 返回 0 条边 → 无交点 → 提示「两点连线上未找到与图元的交点」，静默失败。
建议（按成本排序）：
1. 最小修复：把 `kWallNode`/`kSlabNode` 加入 `CollectPlanBounds` 走 `GetObjectTopPlanBounds`（至少给出顶视包围盒四边，可恢复基本功能）；
2. 精确修复：用公开 SDK 提取墙体路径边（`ISDK::GetWallPathType` 等，`ISDK.h` L3387；墙体是 `prGroup` 容器，可尝试 `FirstMemberObj` 遍历内部 2D 路径），做真正的墙中线/面边求交。
无论选哪种，请在冒烟测试里覆盖"虚拟线横穿一堵墙"用例。

### 1.3 圆/椭圆/弧的求交精度 —— P2

`TraverseObject` 对 `kOvalNode/kArcNode` 走 `CollectPlanBounds`（`SDKComplexGeometry.h` L303–305），即用**顶视包围盒的四条边**代替真实曲线。虚拟线穿过圆形/弧形对象时，交点落在包围盒上而非真实曲线上，读数会错。对矩形（box/rBox）无此问题（包围盒即矩形本身）。建议在文档中注明该限制，或在 W2 对弧线做真实曲线求交（分段逼近）。

### 1.4 WorldCube 构造 / 立面守卫 / undo —— 无问题项

- `segmentCube` 用两点 AABB ±1 的 pad 构造（L2258–2260），仅用于 `ViewPlane::Begin` 的平面摆放；地面平面模式下不影响几何。✓
- 立面守卫：`EndElevationPlaneWithAlert`（L1556–1563）在 `plane.planar`（即四个标准立面视图）时 `ViewPlane::End` + 弹窗 + 返回（L2262–2264），无工作平面泄漏 ✓。
- undo：`SetUndoMethod(kUndoSwapObjects)` + `createdCount>0` 时 `EndUndoEvent()`（L703–707）。SDK 文档（`APIBase.Legacy.Defs.h` L6515–6522）明确「EndUndoEvent 不是必须，外部调用结束时 VectorWorks 会自动结束事件」，因此 createdCount==0 不调用 EndUndoEvent **不是缺口** ✓。

**P2-2（建议）**：守卫文案「仅支持顶视/平面视图」与实际逻辑不符——实际只拦 4 个标准立面，**等轴测/透视等其它 3D 视图会落到地面平面数学继续执行**（`SDKViewPlane.h` `Begin` 的既定行为，`docs/vectorworks-2025-2026-sdk-notes.md` 也写明这是有意设计）。行为本身可接受，但文案建议改为「仅支持平面/非立面视图」或在等轴测下加提示，避免用户困惑。

---

## 2. 符号定位（ManualBlock 分支，L2320–2426）

### 2.1 插入点取值 —— 无问题项

`hasInsertionPoint = (sourceType == kSymbolNode || kParametricNode)`（L2364）；`GetEntityMatrix(sourceObject).P()` 作为插入点（L2367–2369）。与中心点模式 `GetSpacingSource` 的取法完全一致（L1585–1588、L1620–1624）✓。非符号回退到包围盒中心（L2365）。

### 2.2 投影终点构造 —— 无问题项

- 水平（点击相对中心更偏竖向，`|dx|<=|dy|`，L2382–2384）：两条水平尺寸 `(插入点.x, minY)→(minX, minY)` 与 `(插入点.x, minY)→(maxX, minY)`（L2391/2394），测量"插入点→左/右边界" ✓。
- 垂直：`(minX, 插入点.y)→(minX, minY)` 与 `(minX, 插入点.y)→(minX, maxY)`（L2401/2404），测量"插入点→下/上边界" ✓。
- 方向选择逻辑正确：点右侧 → 纵向尺寸、点上方 → 横向尺寸，与 offset 公式自洽。

### 2.3 offset 符号与基准 —— **P1（应该修）**

**P1-2（应该修）**：纵向插入尺寸（bottom/top）的 offset 基准错误。

```cpp
// L2398 / L2401 / L2404
const WorldCoord offset = secondPoint.x - maxX;   // ← 轴在 x=minX，却用 maxX 做基准
AddLinearDimension(WorldPt(minX, insertionPoint.y), WorldPt(minX, minY), offset, ...);   // 轴 x=minX
AddLinearDimension(WorldPt(minX, insertionPoint.y), WorldPt(minX, maxY), offset, ...);   // 轴 x=minX
```

`CreateLinearDimension` 的 SDK 注释明确「startOffset is the distance from p1 to the dimension line」（`APIBase.Legacy.Defs.h` L2097–2098）。纵向两条尺寸的 p1 都在 `x=minX`，要把它放到用户点击的 `secondPoint.x`，offset 应为 `secondPoint.x - minX`。当前 `secondPoint.x - maxX` 会令尺寸线落在 `x = minX + (secondPoint.x - maxX) = secondPoint.x - 包围盒宽度`——**整体左偏一个符号宽度**（符号越宽偏差越大）。该公式疑似从下方回退分支 `manual-vertical`（L2414，其轴在 `x=maxX`，用 `secondPoint.x - maxX` 是正确的）复制而来，复制到插入分支时忘了换基准。
（对照：横向插入尺寸用 `secondPoint.y - minY`，轴在 `y=minY`，基准一致，正确。）

**另需实测确认**（与上一条同源，建议并入 P1 修复时的冒烟用例）：`CreateLinearDimension` 的 offset 正负侧边约定。本仓库既有证据（边界标注宽度 `-offset` 在南侧、高度 `+offset` 在东侧；回退横向 `+offset` 在点击高度、回退纵向 `+offset` 在点击 x）一致指向**引擎按世界坐标解析侧边：横向轴 +offset 朝 +y、纵向轴 +offset 朝 +x，与 p1→p2 方向无关**。若按此约定，横向插入两条尺寸（方向一西一东）都会落在点击高度，无问题；但该约定需在 2025/2026 各跑一次冒烟确认（见 §9 冒烟清单）。若实测发现引擎按 p1→p2 方向决定侧边，则横向插入的 left/right 两条会分列轴线两侧，需对其中一条取反。

### 2.4 零跨度 guard 与回退 —— 无问题项

- `width<=tol && height<=tol` 弹窗并 `ViewPlane::End` 后返回（L2373–2377）✓。
- `hasInsertionPoint && width>tol && height>tol` 才走插入分支，否则回退整体尺寸（L2408–2417），且回退分支分别按 `width`/`height` 单独判零 ✓。
- 插入点落在边界外时（如符号插入点在包围盒外），left/right（bottom/top）各自的 `> tol` guard 只生成有效侧 ✓。

---

## 3. 转换标注（EditSelectedDimensions 的 kEditConvert，L984–1010）

### 3.1 投影方向选择与语义 —— 无问题项

- 守卫 `dimClass != 1`（L987）：只处理对齐（sloped）尺寸，与需求「dimClass==1 的对齐尺寸重建为水平/垂直投影尺寸」一致。`ovDimClass` 的枚举在 `ObjectVariables.h` L292–298 注释中为 `0=fix_ang / 1=sloped / 2=ordinate`，与设计文档 #10 的 0/1/2 映射一致 ✓。
- 方向选择 `|dx|>=|dy| → (end.x, start.y)`（横向投影），否则 `(start.x, end.y)`（纵向投影）（L994–996）✓。
- 幂等：转换后 `ovDimClass` 变为 0，再次执行 kEditConvert 会被守卫跳过，不会二次转换 ✓。
- 零长守卫 `length <= tol` 提前 break（L991）✓。

### 3.2 offset 语义 —— P2（几何跳位）

`sourceOffset` 从原对齐尺寸 `ovDimStartOffset` 读取并原样传入新正交尺寸（L992–997）。对齐尺寸的 offset 是"到斜轴的**法向**距离"，正交尺寸的 offset 是"到水平/垂直轴的**竖直/水平**距离"。两者数值相同但几何位置不同：斜 30°、offset=100 的对齐尺寸，转成水平尺寸后若要保持原尺寸线位置，新 offset 应为 `100/cos30°≈115.5`（并视象限取符号）；直接沿用 100 会让尺寸线向测量轴靠拢约 `offset*(1-cosθ)`。对接近 H/V 的尺寸影响可忽略，对陡斜尺寸肉眼可见"跳位"。设计文档 #10 的目标是「offset 沿用原值避免跳位」，现状只做到"数值沿用"，未做到"位置保持"。
建议：MVP 可接受现状（H/V 为主），W2 按 `offset' = offset / cosθ`（横向转换）折算，并在冒烟用例里对比转换前后尺寸线位置。

### 3.3 swap/delete 事务 —— 无问题项（与 SDK 文档一致）

- `CopyDimensionPresentationFrom` 先拷贝呈现（含 `ovDimStartOffset`、文字、类等，L430–468），失败即短路，`replacementAdded=false`，走 else 用 `DeleteObject(replacement, false)` 清掉未注册的临时对象（L1006）✓。
- 成功路径：`AddAfterSwapObject(replacement)` → `AddBeforeSwapObject(dimension)` → `DeleteObject(dimension, true)`（L1000–1002）。`APIBase.Legacy.Defs.h` L6482–6499 规定：AddBeforeSwapObject 后必须以 `DeleteObject(h, true)` 删除（undo 表持有引用）；L6311–6320 规定：AddAfterSwapObject 已注册的对象删除时应传 `true`。两种失败路径的 `DeleteObject(replacement, replacementAdded)` 参数均正确 ✓。
- `CopyDimensionPresentationFrom` 会把 `ovDimTextRotation` 等原样拷给新尺寸（`kHorVert`/`kAlign` 语义可能与新方向不匹配）——设计文档 #10 风险 4 已列，纳入冒烟用例即可（P2）。

**P2-3（建议）**：若选中的是对齐**链**尺寸（`CreateChainDimension` 产物，`ovDimStartPt/EndPt` 取整体端点），kEditConvert 会把它折叠成单条正交尺寸。`ovDimStartPt/EndPt` 标注为 "Not for public use"（`ObjectVariables.h` L223–226），无法可靠区分链；建议在冒烟测试确认链尺寸被选中转换时的行为，必要时在守卫中排除。

---

## 4. 设计文档核对（docs/autodim-feature-design.md #3/#7/#10）

- **#10 转换标注**：文档 §3.1 描述的"现状"（kEditConvert 把一切重建成对齐、offset 硬编码 0）与工作区改动前的提交一致；**工作区改动已实现文档 §3.3 的修正方向：对齐→H/V 投影、保留 offset、用 swap/delete 事务** ✓。与文档的差异仅一处：文档建议"保持 p1/p2 不变只改 dimType"，实现改为"把 p2 显式投影到轴（projectionEnd）"。两者皆可，显式投影更确定（不依赖引擎对斜点对的 dimType=0 行为），建议在文档中补一句说明。
- **#3 快速标注**：文档的 dimType 语义结论（0=受约束 H/V、1=对齐、2=坐标）与 `ovDimClass` 头文件注释一致 ✓；「dimType=0 的创建路径已被 Auto/线对象模式验证」属实（本仓库 manual-horizontal/vertical/boundary/convert 均走该路径）。
- **#7 中心标注**：现状 `kAnnotationCenters` = 多对象最近邻中心距（`CreateSpacingDimensionsForSelection`，L1628–1711），与文档 §2.2 的成熟产品语义结论一致，无需改动 ✓。
- 文档 §1.4 风险 1（dimType=1 + dir≠p1→p2 必须实测）——见 §9.3：采用"先投影到 θ 轴"后该风险可绕开。

---

## 5. 2025 / 2026 一致性 —— 无问题项

`git diff --no-index` 双版本对比：**仅 `vw-autodim-runtime-2025.txt` / `vw-autodim-runtime-2026.txt` 一处文件名差异**，其余逐字节相同。评审结论对两版本同样适用；2026 额外需关注的是 `resources/AutoDimensionPlugin.credentials.json.template`（`docs/vectorworks-2025-2026-sdk-notes.md` 已记录）✓。

---

## 6. 对既有模式的潜在回归

1. **kEditTrim 行为变更（P2-4，建议）**：原实现用 `ForEachObjectAtPoint` 在尺寸端点附近自动搜源对象；工作区改为"只允许显式选中的源对象影响剪齐"（L965–972、L1015，注释也说明了原因）。这是有意的正确性收紧（旧搜索半径 `max(10, length*0.1)` 不可靠），但属于行为回退：用户只选尺寸点剪齐时，现在会静默无效。建议更新帮助文案或在选中不含源对象时给明确提示。
2. **ApplyDimensionPresentation 移除强制 9pt 字号（P2-5，建议）**：`ovDimTextSizeInPoints=9.0` 的强制写入被移除（diff `@@ -215,15 +235,9 @@`）。创建出的尺寸将随当前标注标准走默认字号，输出外观与旧版可能不同。若是有意为之建议在提交说明/文档标注；否则保留该设置。
3. **kEditMerge 增强（无回归，1 个待实测项 P2-6）**：新增 `ValidateAndSortMergeDimensions` 排序校验 + 失败时对已建链 `DeleteObject(chain, true)` 回滚（L1151–1182），优于旧版裸调 `CreateChainDimension`。待实测：`GS_CreateChainDimension` 文档只说"creates and returns a new chain dimension object"（`APIBase.Legacy.Defs.h` L1952–1956），**未说明是否消费/删除输入的两条尺寸**；若引擎不消费输入，合并会在原尺寸之上叠加链对象造成重复。请在冒烟测试确认（这也直接影响 §9 链式合并方案）。
4. 其余既有模式（centers/continuous/boundary/auto/line/enhanced）函数体仅做 trace 宏改名与中文文案，逻辑未变；新增的立面守卫只加在原本就不支持立面的模式上 ✓。

---

## 7. 无问题项清单

- 线段求交 det/t/u 公式推导（§1.1）；排序投影（§1.1）；候选 AABB 过滤无漏检（§1.2）。
- WorldCube 构造与 `ViewPlane::Begin/End` 配对；`EndElevationPlaneWithAlert` 提前返回路径均 `End` 工作平面，无泄漏。
- 所有 `SetUndoMethod`/`AddBefore/After/BothSwap`/`DeleteObject(useUndo)` 用法与 SDK 文档一致；`createdCount==0` 不调 `EndUndoEvent` 属合法（VW 自动结束）。
- 空指针/除零/越界：`BuildLineMeasurement` 零长守卫、`intersections.size()<2` 守卫、`index>=1` 循环、`dimensions.front()` 由调用方空集合守卫、`GetToolPt2D` 由 `GetToolPointsCount()<2` 守卫。
- 2025/2026 一致性（§5）。
- 设计文档 #7（中心距语义）与实现一致。

---

## 8. W3 专家评审：「快速标注 #3」可持续连续标注（交互式链式尺寸）

### 8.1 H/V 投影链在工具交互上是否可行 —— 可行，且是最小改动路径

工具框架（`VWExtensionTool.h` L61–99、`VWPluginToolProviders.h` CStatusProvider L94–102）提供 `GetPolyToolStatus()`（多点/折线状态）与 `GetToolPointsCount()` / `GetToolPt2D(i)` / `ClearAllToolPoints()`。链式交互的标准做法：
- `GetStatus`（现 L2171–2184）：链模式返回 `pStatusProvider->GetPolyToolStatus()`（多点点击 + 双击/回车结束），其余模式保持现状；
- `HandleComplete`（现 L2186 起）：`GetToolPointsCount() >= 2` 后，对相邻点对循环调用现有 `AddLinearDimension(..., dimType=0, ...)`，一次 `SetUndoMethod` + 一次 `EndUndoEvent` 包住整条链；
- 双击结束由框架折线状态天然提供（必要时在 `DrawingDoubleClick`/`DoDoubleClick` 收尾）。

改动面：`GetStatus` 加一个分支 + `HandleComplete` 加一个循环，**复用 `AddLinearDimension`/`ViewPlane`/`EndElevationPlaneWithAlert`，不新增 SDK 调用面**。比设计文档 §1.3 建议的"扩展现有连续标注加方向子选项"更轻——直接新增一个链模式或子选项即可。

### 8.2 dimType=0 是否足够支撑「转角标注」语义 —— 足够

`GS_CreateLinearDimension` 注释：dimType=0 = "allow only horizontal and vertical dimension lines"（受约束 H/V）。本仓库 manual-horizontal/manual-vertical/boundary/convert 四类调用全部用 dimType=0 + 轴对齐 p1/p2 且已验证可用——「转角标注」若定义为 H/V 投影（等价 DIMLINEAR 默认），dimType=0 就是专门工具，**没有新的 SDK 风险**。前提：p1/p2 必须轴对齐（用投影端点，而非原始点击点）。

### 8.3 比设计文档更稳的实现建议

1. **任意角度链不需要依赖未验证的 dir≠p1→p2**：设计文档 §1.4 风险 1 的担心只在"保留原始点击点、强行把尺寸线扭到 θ 方向"时才存在。正确做法是**先把点击点投影到链轴 θ 上，再用投影点对创建尺寸**——投影点对天然与 θ 共线，`dimType=1 + dir=θ` 时 `dir == p1→p2 方向`，与现有 `kAnnotationContinuous`（L635，`dir=normalize(段向量)`）完全同一已验证路径，无任何引擎未知。该方案下"任意角度链"的风险降到与 H/V 链同级。
2. **每段偏移统一基线，避免重叠**：H/V 链的所有段应共享同一基线（如取首点 y 作为水平链基线），offset 用**一次算好的统一值**（如 `max(25, 链总跨度*0.15)`），而不是每段按各自长度算——否则相邻段尺寸线高度不一，视觉上交错重叠。相邻段共享端点时即形成无缝隙连续链；若用户点击回溯（P_i 的投影不在 P_{i-1} 与 P_{i+1} 之间），两段会重叠，MVP 可允许重叠并在文档说明，或按投影位置排序后取相邻投影对（后者更接近传统"连续标注"，但会丢失点击顺序语义，二选一需产品拍板）。
3. **链对象 vs 独立尺寸对象**：MVP 建议每段独立尺寸对象（与现状一致、撤销粒度小、不依赖 `CreateChainDimension` 的输入消费行为）。收尾合并为单一链对象作为可选增强，直接复用 kEditMerge 的 `ValidateAndSortMergeDimensions` + `HaveMatchingMergePresentation`；但**开工前必须先实测 `CreateChainDimension` 是否消费输入尺寸**（见 §6.3），否则合并会叠加重复。
4. **零长/重合点防护**：多点状态下双击会在末尾追加重复点，`HandleComplete` 里应跳过 `length<=tol` 的相邻点对；顶点吸附由工具框架自带。
5. **冒烟清单（开工前 10 分钟，2025/2026 各一次）**：
   - dimType=0 + 轴对齐点对：±offset 落在哪一侧（确认 §2.3 的世界坐标侧边约定）；
   - dimType=0 + 斜点对（对照设计文档 §3.3 的"保持 p1/p2 不变"写法）实际渲染；
   - dimType=1 + 共线投影点对沿 θ 的渲染与读数；
   - `CreateChainDimension` 是否删除输入尺寸；
   - kEditConvert 选中链尺寸时的行为；
   - 转换前后 offset 数值/位置对照（§3.2）。

### 8.4 明确结论

**选 H/V 投影链作为 W3 的 MVP**（改动最小、dimType=0 路径已被本仓库四类既有调用验证、无新 SDK 风险）；**开工前先做一轮 10 分钟 SDK 行为冒烟实测**（上面 6 项清单），其中第 1 项顺带验证并修复本次评审的 P1-2 偏移基准；**任意角度链作为 W2/W3 增强**，采用"先投影到链轴 θ + dimType=1 + dir=θ"的路径实现，**不依赖**设计文档里标注为"必须实测"的 dir≠p1→p2 行为。

---

## 附：关键代码/头文件定位

- 插件源码（2025/2026 同构）：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`
  - 交线标注 L641–714；HandleComplete 交点分支 L2247–2268；候选 AABB 过滤 L657–663；去重 L680–685
  - ManualBlock L2320–2426；插入 offset L2388/2398；插入尺寸 L2391–2404；回退 L2408–2417
  - kEditConvert L984–1010；EditSelectedDimensions undo 头 L956、尾 L1183；剪齐源对象 L965–972
  - EndElevationPlaneWithAlert L1556–1563；GetStatus L2171–2184
- 几何采集：`include/vwad/SDKComplexGeometry.h`（`SMeasuredSegment` 原始坐标；`TraverseObject` L276–344；`CollectPlanBounds` L191–203）
- 视图平面：`include/vwad/SDKViewPlane.h`（`Begin` 只对 4 个标准立面产生 planar）
- SDK 头文件（2025）：`APIBase.Legacy.Defs.h`（CreateLinearDimension 注释 L2095–2103；DeleteObject L6311–6320；AddBefore/After/BothSwap L6464–6499；EndUndoEvent L6515–6522；CreateChainDimension L1952–1956）；`ObjectVariables.h`（ovDimStartOffset L230–231；ovDimClass 枚举注释 L292–298；ovDimDirection "Not for public use" L221）；`Objs.TDType.h`（kWallNode=68 L134、kSlabNode=71 L137）；`ISDK.h`（ForEachObjectN L2637；GetWallPathType L3387）
- 工具框架：`VWFC/PluginSupport/VWExtensionTool.h`（GetToolPointsCount/GetToolPt2D/ClearAllToolPoints L93–99；DoDoubleClick/DrawingDoubleClick L68–70）；`VWFC/PluginSupport/VWPluginToolProviders.h`（CStatusProvider GetPolyToolStatus L94–102）
- 设计文档：`docs/autodim-feature-design.md`（#3 §1、#7 §2、#10 §3、波次 §4）
