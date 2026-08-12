# Vectorworks 自动尺寸标注插件：三个歧义功能的语义确认与设计文档

> 任务性质：语义确认 + 设计研究，**只写文档，不修改任何产品代码**。
> 适用仓库：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`（Vectorworks 2025/2026 C++ SDK 插件，10 个标注模式 + 10 个编辑模式）。
> 结论级别说明：每节结论都标注了证据来源（本机 SDK 头文件 / Vectorworks 官方帮助 / 中文 CAD 术语），以及「已确认」或「需运行时验证 / 需用户拍板」。

---

## 0. 背景与范围

### 0.1 插件现状速览（与本文三个功能相关的部分）

- 标注模式栏（kAnnotationModeGroup）：
  - `kAnnotationAuto` 自动识别 / `kAnnotationContinuous` 连续标注 / `kAnnotationLine` 线对象 / `kAnnotationManualBlock` 图块定位 / `kAnnotationIntersection` 交线 / `kAnnotationSelection` 选择对象 / `kAnnotationCenters` 中心点 / `kAnnotationBoundaries` 块边界 / `kAnnotationClosedSpace` 封闭空间 / `kAnnotationEnhanced` 加强标注。
- 编辑模式栏（kEditModeGroup）：
  - `kEditConvert` 转换标注（帮助文案：「将选中的标注转换为对齐标注」）/ 剪齐 / 对齐 / 分割延伸 / 文字方向 / 标注点 / 合并 / 避让 / 重置文字 / 重置文字位置。
- 关键实现常量（`AutoDimensionObj.cpp` 第 29–30 行）：
  - `kLinearDimensionTypeOrtho = 0`，`kLinearDimensionTypeAligned = 1`。
- 关键 SDK 调用点：
  - `AddLinearDimension()` 统一走 `gSDK->CreateLinearDimension(p1, p2, startOffset, 0.0, direction, dimensionType)`，随后 `ViewPlane::ApplyPlanarRef` + `ResetObject` + `AddAfterSwapObject`。
  - `kEditMerge` 用 `gSDK->CreateChainDimension(h1, h2)` 把相邻尺寸合并为单一链对象。
  - `kEditConvert` 用 `GetDimensionPoint(dim, ovDimStartPt/ovDimEndPt, ...)` 读取起终点，然后重建为 `kLinearDimensionTypeAligned`（`dir = normalize(end - start)`），再 `CopyDimensionPresentationFrom` + 交换。
  - 读取尺寸类型用 `GetDimensionUnsignedChar(dimension, ovDimClass, ...)`，且当前代码用 `dimensionClass > 1` 作为“不支持”的守卫。

### 0.2 本文要确认的三个功能

| 编号 | 用户原话 | 歧义点 |
| --- | --- | --- |
| 快速标注 #3 | 「快速新建转角标注，且可持续连续标注」 | 「转角标注」指什么；「可持续连续标注」是哪种交互 |
| 中心标注 #7 | 「标注对象的几何中心」 | 是多对象中心距，还是单对象几何中心定位 |
| 转换标注 #10 | 「将对齐标注转换为转角标注」 | ovDimClass 取值语义；对齐→转角在 SDK 里怎么表达；现有 kEditConvert 是否方向反了 |

### 0.3 证据来源

- 本机 SDK（2025，`C:\Users\keepl\Downloads\VectorworksSDK\2025\SDK\SDKVW(784374)`）：
  - `SDKLib\Include\Kernel\API\ObjectVariables.h`（ovDimClass = 26 等）
  - `SDKLib\Include\Kernel\API\APIBase.Legacy.Defs.h`（`GS_CreateLinearDimension` 权威注释）
  - `SDKLib\Include\Interfaces\VectorWorks\ISDK.h`（`CreateLinearDimension` / `CreateChainDimension`）
  - `SDKLib\Include\vs.py`（`LinearDim` / `CreateChainDimension` 文档字符串）
- Vectorworks 官方帮助（app-help.vectorworks.net，zh_hans / eng）：
  - 不受约束线性尺寸标注 / 受约束的线性尺寸标注 / 不受约束(受约束)的链式尺寸标注 / 标记对象中心。
- 中文 CAD 术语：
  - GstarCAD（浩辰）帮助「创建线性标注」：转角标注 = DIMLINEAR + 旋转；
  - AutoCAD 中文帮助 DIMLINEAR / DIMALIGNED / DIMCENTER；天正「快速标注：整体/连续/连续加整体」。
- Vectorworks 开发者社区（forum.vectorworks.net，2020 年 KN Stef / 2019 年 hagemeijer）关于 ovDimClass 取值。
- 插件源码：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`。

---

## 1. 快速标注 #3：转角标注 + 可持续连续标注

### 1.1 现状

- `kAnnotationContinuous`（连续标注）：选中线或路径对象后，按对象遍历出的直线段，**逐段创建 `kLinearDimensionTypeAligned`（对齐、真长）尺寸**，`dir = normalize(segment.end - segment.start)`（`CreateContinuousDimensions`）。这些是彼此独立的尺寸对象，**不是链对象**。
- `kEditMerge`（合并标注）：已经实现用 `CreateChainDimension` 把相邻尺寸合并为单一链对象（含 `ValidateAndSortMergeDimensions` 排序与 `HaveMatchingMergePresentation` 校验）。
- `kAnnotationAuto` / 线对象对 2D 直线会同时生成：水平投影（`Ortho`）、垂直投影（`Ortho`）、斜向真长（`Aligned`）、角度（`CreateAngleDimension`）四种。
- 现有「连续标注」帮助文案是「选中线或路径对象，连续创建标注」，走的是**选择驱动**（选中→批量生成），没有「点一下加一段、可持续点选」的交互链。

### 1.2 成熟产品语义（结论）

**1.2.1 中文 CAD 术语「转角标注」= 旋转线性标注（投影测量），不是对齐标注。**

- GstarCAD（浩辰）中文帮助「创建线性标注」原文：*「在转角标注中，尺寸线与尺寸界线原点呈一定角度，使用 DIMLINEAR 命令创建线性标注时，在选取对象后，选择‘旋转’选项，并指定尺寸线与尺寸界线原点之间的角度值，即可创建转角标注。」*
- AutoCAD 中文语境：DIMLINEAR 默认是水平/垂直；「旋转(R)」子选项产生「转角标注」（尺寸线按指定角度旋转，读数 = 两点在旋转方向上的投影距离）；DIMALIGNED 才是「对齐标注」（尺寸线平行于两点连线，读数 = 真长）。
- 结论：**「转角标注」= 尺寸线方向相对被测对象边成一定角度的投影标注**；当角度为 0°/90° 时就是常见的水平/垂直投影标注。它与「对齐标注」（真长）是两种不同的测量语义。

**1.2.2 Vectorworks 的等价物：线性尺寸只有三类，无独立“旋转”工具。**

- 受约束线性尺寸标注工具（Constrained Linear Dimension）：尺寸线被约束在创建平面的 X 或 Y 轴（2D 视图下即水平/垂直投影）。模式：受约束线性 / 受约束链式 / 受约束基线 / **坐标（ordinate）** / **选定对象（为选中对象创建最大跨距受约束尺寸线）**。
- 不受约束线性尺寸标注工具（Unconstrained Linear Dimension）：尺寸线可“任意角度”，但该角度就是两点连线方向（等效 AutoCAD 对齐标注）。模式：不受约束线性 / 不受约束链式 / 不受约束基线。
- 因此 Vectorworks 里「转角的投影」要么是**受约束线性（0°/90° 特例）**，要么是**不受约束线性放在旋转后的测量平面/方向上**；不存在 AutoCAD 那种“尺寸线角度与测量方向解耦”的独立对象类型（详见 3.2 的 dimType 讨论与风险）。

**1.2.3 「可持续连续标注」= Vectorworks 链式模式 / 天正「快速标注→连续」。**

- Vectorworks 官方帮助（链式尺寸标注）：*「不受约束链式模式可用于创建一系列连接的尺寸标注线，每个线段显示其具体的测量值」*；交互为「单击设置测量起点 → 单击结束第一段 → 放置第一条尺寸线 → 将光标移至下一条线段端点单击 → 继续设置线段 → **双击结束该链**」。
- 链式首选项：**创建为单一链式对象，还是彼此相邻的单个尺寸对象**；并可启用冲突控制（文本自动避让）。
- 天正「快速标注」：提供 **整体 / 连续 / 连续加整体** 三种样式，其中「连续」= 提取对象节点创建连续直线尺寸（通常沿水平/垂直方向逐段投影）。
- 结论：用户要的「可持续连续标注」= 逐点续接的**链式交互**（每段一条尺寸线、显示各自测量值），可选合并为单一链对象；Vectorworks 原生链式首选项已经支持“独立对象”形态。

**1.2.4 SDK 层事实（已从本机头文件确认）。**

`APIBase.Legacy.Defs.h` 对 `GS_CreateLinearDimension` 的权威注释（原文）：

> Creates a linear dimension object. p1 and p2 are the two endpoints of the distance to be measured. startOffset is the distance from p1 to the dimension line. textOffset is CURRENTLY UNUSED. dir is the normalized difference between p2 and p1. If dir is passed in as (0,0), this value is calculated automatically. dimType indicates whether to allow only horizontal and vertical dimension lines, whether to rotate the dimension line to have the same direction as the vector between p1 and p2, or whether to create an ordinate dimension.

即 dimType：
- `0` = 仅允许水平/垂直尺寸线（受约束 / ortho）；
- `1` = 尺寸线旋转到与 p1→p2 同向（不受约束 / 斜向 / aligned）；
- `2` = 坐标（ordinate）标注。

`CreateChainDimension(h1, h2)`（vs.py 文档字符串）：*“Creates and returns a single chain dimension object when the two dimensions or chains that are passed in meet the requirements for being in a single chain dimension object.”* —— 即满足条件时把两个尺寸/链合并为一个链对象（插件 kEditMerge 已在使用）。

### 1.3 推荐 MVP 设计

**语义结论（一句话）**：`转角标注` = 投影测量（尺寸线方向与测量方向解耦或取 0°/90°），`可持续连续标注` = 逐点续接的链式交互；两者组合 = **一条沿固定方向的投影链**（建筑平面最常用的是水平/垂直投影链，等价天正「快速标注→连续」、Vectorworks「受约束链式」）。

**推荐 MVP：扩展现有「连续标注」模式，新增“方向”子选项，不新增模式按钮（改动最小、不破坏现有 10+10 布局）。**

- 子选项：`水平投影链` / `垂直投影链` / `沿路径对齐`（默认，= 现状行为）。
- 交互（水平/垂直投影链）：用户先选线/路径对象（或直接点选），工具按对象遍历出直线段后，对每段计算其**水平（或垂直）投影跨度**：
  - 水平投影段：`p1 = (minX, y0)`，`p2 = (maxX, y0)`（y0 取段中点的 y，保证轴对齐），`dimType = kLinearDimensionTypeOrtho`（0）；
  - 垂直投影段：`p1 = (x0, minY)`，`p2 = (x0, maxY)`，`dimType = kLinearDimensionTypeOrtho`（0）。
  - 偏移沿用现有 `offset = max(25, extent * 0.15)`。
- 若要做「逐点续接」交互链（W2）：仿 Vectorworks 链式——第一次点选定起点，后续每次点选以上一点为起点生成一条投影尺寸，双击结束；结束时可选调用 `CreateChainDimension` 逐对合并为单一链对象（直接复用 kEditMerge 的排序/校验逻辑）。
- 复用清单：`ViewPlane`、`AddLinearDimension`、`ApplyDimensionPresentation`、`CopyDimensionPresentationFrom`、kEditMerge 的 `ValidateAndSortMergeDimensions`。
- 若要“任意角度链”（真·转角，W2/W3）：前两次点选定义链方向 θ，之后每次点选把上一投影点与当前点投影到 θ 方向生成一条尺寸；方向 θ 的 SDK 表达见 3.3 的两种途径（dimType=1 + 显式 dir=θ，或旋转测量平面 + dimType=0），**其中 dimType=1 是否尊重 dir≠p1→p2 必须先在 Vectorworks 里实测**。

### 1.3.1 已知限制（2026-08-11 补充）

**交线标注（kAnnotationIntersection）对圆/椭圆/弧对象用顶视包围盒求交，非真实曲线。**

`SDKComplexGeometry.h` 的 `TraverseObject` 对 `kOvalNode/kArcNode` 走 `CollectPlanBounds`，即用对象顶视包围盒的四条边代替真实曲线参与线段求交。虚拟线穿过圆形/弧形对象时，交点落在包围盒边上而非真实曲线上，标注读数会与曲线真实位置不符。对矩形（box/rBox）无此问题（包围盒即矩形本身）。墙/楼板（kWallNode/kSlabNode）同样走包围盒路径。

- 影响范围：仅交线标注；其余模式（线对象/边界/中心点等）不依赖该求交路径。
- 缓解：MVP 在帮助文案中注明；W2 若需精确读数，可对弧/圆做真实曲线求交（分段逼近，`SMeasuredSegment` 已按段存储，可对弧边细分后参与求交）。

### 1.4 风险与开放问题

1. **dimType=1 + dir≠p1→p2 的行为未验证**（关键验证点）：SDK 注释说 dimType=1 是把尺寸线“旋转到 p1→p2 方向”，若引擎强制忽略自定义 dir，则任意角度投影链只能通过“旋转测量平面 + dimType=0”实现（代价是交互/平面管理更复杂）。→ 开工前在 2025/2026 各做一次最小冒烟测试。
2. **「转角」是否包含任意角度**需要用户拍板：如果用户只要水平/垂直投影（最常见），MVP 就是 1.3 的投影链，风险几乎为零（`dimType=0` 创建路径在 Auto/线对象模式已被现有代码验证过）。
3. **链对象 vs 独立尺寸对象**：默认建议先做独立尺寸对象（与现状一致、可撤销粒度小），合并为链对象作为可选项（复用 kEditMerge）。
4. 与现有「连续标注」按钮的文案/图标区分（新增“转角链”按钮 vs 子选项）会影响 UI 一致性和帮助文案。

---

## 2. 中心标注 #7：标注对象的几何中心

### 2.1 现状

- `kAnnotationCenters`（中心点标注）：要求至少选中两个对象，`CreateSpacingDimensionsForSelection` 用最近邻（Prim 式 MST）把选中对象串成链，对每条相邻对创建一条 `kLinearDimensionTypeAligned`（dimType=1、dir=中心连线方向）的**中心到中心间距尺寸**。
- 中心定义（`GetSpacingSource`）：平面视图 = 对象包围盒中心（`GetCubeCenter`），符号/参数对象用插入点（`GetEntityMatrix` 的 P）；非平面视图 = 遍历二维几何后的轴对齐包围盒中心。
- 帮助文案：「选中多个对象，标注对象中心间距。」—— 现状语义明确是**多对象中心距**。

### 2.2 成熟产品语义（结论）

**成熟产品里「中心标注」的主流语义 = 多对象中心到中心间距（中心距标注）。** “单个对象的几何中心”是另一类独立功能（中心标记/圆心标记），不是尺寸标注。

- 多对象中心距：
  - AutoCAD：没有独立“中心标注”命令；用线性/对齐标注捕捉圆心/中点即可得到中心距；天正的「逐点标注」「快速标注」可连续捕捉圆心/对象节点做中心距链。
  - Vectorworks：无一键“中心距”工具，靠约束/不受约束线性尺寸捕捉对象中心。
  - **现有 kAnnotationCenters（最近邻中心距）与该主流语义一致，方向正确。**
- 单对象中心标记（独立的“标记”功能，非尺寸）：
  - Vectorworks「标记对象中心」（Center Mark，尺寸标注/备注工具集）：*“中心标记工具可将一个对象或对象面分为四个象限，同时标记对象的精确中心。此工具适用于圆、椭圆、矩形、圆角矩形，以及通过圆形边创建的三维对象的面；中央由两条相交的线条标记。”*
  - AutoCAD `DIMCENTER`（圆心标记 / 中心线，DIMCEN 系统变量控制）；天正：圆心标记 + 中心线。
- 单对象“几何中心定位尺寸”（把中心的位置用尺寸表达出来）：成熟产品没有专用命令，做法就是**以中心点为目标点、从参考边（或原点）引水平/垂直定位尺寸**（如“中心距左侧参考边 X、距下边 Y”）。
- 结论：**「标注对象的几何中心」最可能是「多对象中心到中心间距」（现有 kAnnotationCenters 已覆盖）；若用户要的是单对象几何中心定位，则应新增“中心定位”而非改动现有中心距模式。**

### 2.3 推荐 MVP 设计（若需要新增“单对象几何中心定位”）

- 尺寸类型：两条 `dimType = kLinearDimensionTypeOrtho`（0）线性尺寸：
  - 水平定位：`p1 = (参考左边线 x, center.y)`，`p2 = (center.x, center.y)` —— 标注中心到左参考边的水平距离；
  - 垂直定位：`p1 = (center.x, 参考下边线 y)`，`p2 = (center.x, center.y)` —— 标注中心到下参考边的垂直距离。
  - （也可用中心点向参考边引 X/Y 坐标式读数，但坐标标注 dimType=2 交互与显示差异大，MVP 不推荐。）
- 参考线选择（最小实现）：默认取对象轴对齐包围盒的左/下边（无额外交互）；进阶（W2）允许用户点选两条参考边（更贴合 AutoCAD/天正习惯）。
- 中心点计算：MVP 沿用 `GetSpacingSource` 的中心定义（包围盒中心 / 符号插入点），与现有中心点模式保持一致；真·几何质心（面积质心）作为 W2 增强。
- 落地方式：新增一个标注模式按钮「中心定位」，或并入现有「中心点」模式作为子选项；**不要改变现有中心距模式的输出语义**。

### 2.4 风险与开放问题

1. **语义确认**：需求 #7 到底要“中心距”（已有）还是“单对象中心定位”（新增）？这是本节最需要用户拍板的一点。
2. **中心定义**：包围盒中心 vs 几何面积质心 vs 符号插入点——对非对称对象结果差异明显，需统一口径（建议默认包围盒中心，文档中明确写出）。
3. 中心十字标记（Vectorworks Center Mark / AutoCAD DIMCENTER）是标记对象而非尺寸；SDK 是否有公开的“创建中心标记”API 未确认，**MVP 不建议做十字标记**，只做定位尺寸。
4. 若做用户点选参考边，需要引入两点交互（`GetTwoPointToolStatus`）与参考边吸附，复杂度高于 MVP。

---

## 3. 转换标注 #10：对齐标注 → 转角标注

### 3.1 现状

- `kEditConvert`（转换标注）帮助文案：「将选中的标注转换为对齐标注」。
- 实现（`EditSelectedDimensions` 的 `kEditConvert` 分支）：读取选中线性尺寸的 `ovDimStartPt`/`ovDimEndPt` 与 `ovDimClass`（`>1` 拒绝），然后重建 `CreateLinearDimension(start, end, 0.0, 0.0, normalize(end-start), kLinearDimensionTypeAligned)`，`CopyDimensionPresentationFrom` 后交换对象。
- 即：**当前 kEditConvert 把任何线性尺寸（含已是对齐的）一律重建成“对齐（真长）”**。

### 3.2 成熟产品语义：ovDimClass 取值语义（结论）

- `ObjectVariables.h`：`ovDimClass = 26`，类型 `unsigned char`，标注为 “Public for VS”（VectorScript 可用）。
- 交叉验证的取值映射（来源：本机 `APIBase.Legacy.Defs.h` 的 `CreateLinearDimension` 注释 + developer.vectorworks.net 同名文档 + Vectorworks 论坛 KN Stef 2020 / hagemeijer 2019）：

| ovDimClass / CreateLinearDimension dimType | 含义（SDK 注释） | Vectorworks 工具 | 对应中文 CAD |
| --- | --- | --- | --- |
| 0 | 仅允许水平/垂直尺寸线 | 受约束线性（Constrained） | 线性标注（水平/垂直投影；DIMLINEAR 默认） |
| 1 | 尺寸线旋转到 p1→p2 方向（真长） | 不受约束线性（Unconstrained / 斜向） | 对齐标注（DIMALIGNED） |
| 2 | 坐标（ordinate）标注 | 受约束线性 → 坐标模式 | 坐标标注（DIMORDINATE） |

- 结论：**`ovDimClass` 0 = 受约束（水平/垂直投影），1 = 不受约束/对齐（真长），2 = 坐标**。插件当前的 `kLinearDimensionTypeOrtho=0 / kLinearDimensionTypeAligned=1` 与 `dimensionClass > 1` 守卫（排除坐标）与上述映射一致，无需改动。

### 3.3 推荐设计：转换方向结论与实现方式

**结论：现有 kEditConvert 与需求 #10 方向相反。**

- 需求 #10 要「把对齐标注转换为转角标注」：对齐（dimType=1，真长）→ 转角（投影，尺寸线成角度）。
- 现有 kEditConvert 是「转换为对齐」（把 0 和 1 都变成 1，且 dir=p1→p2 真长）——即**把转角/受约束往对齐方向转**，与用户诉求相反；对已是对齐的尺寸则是空操作。
- 修正建议：**把 kEditConvert 的默认语义改为「转投影（转角）」，对齐→转角；对已是受约束（0）的尺寸，转成“改方向”**（H↔V 或指定角度）。

**推荐实现（MVP，先只做 0°/90°）**：

1. 读取 `ovDimStartPt` / `ovDimEndPt`（扩展线原点）与 `ovDimClass`。
2. 让用户选择目标方向：`水平` / `垂直`（两点交互、指定任意角度 θ 留到 W2）。
3. 重建尺寸（保持 p1/p2 不变，只改尺寸线方向，读数变为投影）：
   - 水平：`CreateLinearDimension(p1, p2, 0, 0, Vector2(1,0), kLinearDimensionTypeOrtho)`（或 `dimType=1 + dir=(1,0)`，两者取实测表现更稳者）；
   - 垂直：`Vector2(0,1)`，同上；
   - 保留 `CopyDimensionPresentationFrom`、平面引用与 offset（offset 从原尺寸读取，不要硬编码 0——现实现传 0 会改变尺寸线位置，属附带缺陷，建议一并修正）。
4. 指定任意角度 θ（W2）：候选途径 A `dimType=1 + dir=(cosθ,sinθ)`；候选途径 B 旋转测量平面 + `dimType=0`；候选途径 C 创建后写 `ovDimDirection`（“Not for public use”，不推荐）。**途径 A 是否被引擎尊重需实测（同 1.4 的风险 1）。**

### 3.4 风险与开放问题

1. **dimType=1 + dir≠p1→p2 的渲染行为必须实测**（关键验证点）：这决定“任意角度转角”能否用最简单方式实现；若不行，MVP 就锁定 0°/90°（dimType=0），任意角度走旋转平面方案。
2. **重建的固有代价**：丢失关联信息/链成员关系（现状已如此，保持一致即可）；重建时 offset 应沿用原值，避免尺寸线跳位。
3. **术语对齐**：需要与用户确认「转角标注」= 投影（DIMLINEAR 旋转）而非“斜向对齐”；若用户实际把「转角」理解为「斜向」，则需求 #10 就变成“对齐→斜向”（可能无操作），务必先对齐术语。
4. 转换后文本方向/位置（`ovDimTextRotation` 等）可能需按新方向重算，纳入冒烟用例。

---

## 4. W2/W3 波次实施建议

### 4.1 可以直接开工（低风险，结论已确认）

1. **#3 水平/垂直投影链**：在现有「连续标注」模式内加“水平投影链 / 垂直投影链”子选项，逐段生成 `dimType=0`（轴对齐 p1/p2）尺寸。`dimType=0` 的创建路径已被 Auto/线对象模式验证，无新 SDK 风险。
2. **#7 现状确认与文案澄清**：保留 `kAnnotationCenters` 的中心距语义（与成熟产品一致），仅把模式帮助文案与需求文档写清楚（“中心距 = 多对象中心到中心”），不写代码。
3. **#10 修正 kEditConvert 方向（0°/90° 版）**：把“转对齐”改为“转水平/垂直投影”，重建时保留 offset 与呈现；先做 H/V，任意角度后续。**开工前做 10 分钟冒烟测试确认 dimType=1 + 显式 dir 的表现**（决定实现路径 A 还是 B）。

### 4.2 需要用户拍板后才能开工

1. **#3「转角」口径**：仅水平/垂直投影，还是任意角度链？链是否要合并为单一链对象（复用 kEditMerge）？是否接受在现有「连续标注」按钮下加子选项（不新增按钮）？
2. **#7 目标**：只要中心距（现状已满足），还是要新增「单对象几何中心定位」（参考边自动 or 点选；中心 = 包围盒中心 vs 面积质心 vs 插入点）？
3. **#10 目标方向来源**：固定 H/V 子选项，还是允许用户两点指定任意角度；以及是否接受 3.2 的 ovDimClass 术语映射（0=受约束投影，1=对齐，2=坐标）作为团队统一口径。
4. 三个功能是否合并到同一个“标注-编辑”迭代里发版，还是按 W2（#3 投影链 + #10 H/V 转换）→ W3（#3 任意角度链 + #7 中心定位）分两波。

### 4.3 开工前统一验证清单（三个功能共用）

- [ ] 在 Vectorworks 2025 与 2026 各跑一次：`CreateLinearDimension(dimType=1, dir=(1,0), p1,p2 斜向)` 的渲染与读数（水平投影？真长？方向被忽略？）。
- [ ] 确认 `ovDimClass` 读回值：分别用 dimType=0/1/2 创建后读 `ovDimClass`，验证 0/1/2 映射。
- [ ] `CreateChainDimension` 对“同方向、同偏移、端点相接”的两条独立投影尺寸是否可合并（复用 kEditMerge 路径即可）。
- [ ] 重建尺寸时 offset 沿用的正确单位（`ovDimStartOffset` vs `ovDimStartOffsetInCurrUnits`）。

---

## 附：关键引用

- 本机 SDK：`SDKLib\Include\Kernel\API\APIBase.Legacy.Defs.h`（`GS_CreateLinearDimension` 注释，行 ~2095）；`ObjectVariables.h`（`ovDimClass=26`，行 292）；`vs.py`（`CreateChainDimension`，行 2936）。
- Vectorworks 帮助：不受约束线性尺寸标注 <https://app-help.vectorworks.net/2025/zh_hans/VW2025_Guide/Dimensions/Unconstrained_linear_dimension.htm>；受约束的线性尺寸标注 <https://app-help.vectorworks.net/2023/zh_hans/VW2023_Guide/Dimensions/Constrained_linear_dimensioning.htm>；不受约束的链式尺寸标注 <https://app-help.vectorworks.net/2022/zh_hans/VW2022_Guide/Dimensions/Unconstrained_chain_dimension.htm>；标记对象中心 <https://app-help.vectorworks.net/2025/zh_hans/VW2025_Guide/Dimensions/Marking_object_centers.htm>。
- GstarCAD 创建线性标注（转角标注定义）：<https://www.gstarcad.com/help/GstarCAD_2024_zh-CN/detail/GUG_DIM_CREATEDIM/DIMLINEAR.html>。
- developer.vectorworks.net：VCOM:VectorWorks:ISDK::CreateLinearDimension；VS:LinearDim。
- Vectorworks 论坛：KN Stef（2020，ovDimClass 映射 0/1/2）、hagemeijer（2019，dimType=0=constrained）。



---

## 5. 文字避让增强设计

> 性质：只读研究 + 设计文档（不写产品代码）。基于 `sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`；2026 同名源码内容与 2025 不同，改动需双份同步。证据来源：本机 SDK 头文件（`ObjectVariables.h`、`ISDK.h`）、仓库内 `include/vwad/SDKComplexGeometry.h`。

### 5.0 目标与一句话结论

- 目标：在保留现有「多轮碰撞避让」框架的前提下，① 把用户选中的**源图形**纳入碰撞障碍（增强点 A）；② 当上下/左右小幅挪动无法消除重叠时，**沿标注线方向外推**文字到 witness 外侧（增强点 B）；③ 明确避让范围取舍（增强点 C）。
- 一句话结论：**A** = 在 kEditAvoid 入口收集 `CollectSelectedSources()` 的二维几何（AABB 障碍为主、线段集为辅），与尺寸包围框共用同一套重叠判定，并按「远离源图形」重选调整方向；**B** = 在 8 轮内仍重叠或 inside 比值越界时，按「离哪条 witness 更近 / 重叠重心」决定方向，用带符号的 `ovDimTextOffsetInCurrUnits` 外推并置 `ovDimTextPosCalculated=false`，每个尺寸只外推一次、整体迭代上限放宽到 12。
- 最小可用原则：先端到端跑通（A 用 AABB、纯增量、无源图形时行为与现状完全一致）再上 B，B 之后再上可选的 C。

### 5.1 现状梳理

#### 5.1.1 调用链与输入

- 入口：`HandleComplete()`（~2198 行）→ `EditSelectedDimensions(kEditAvoid, plane)`（959–963 行）→ `AvoidDimensionTextCollisions(dimensions, undoRegistered)`（734–840 行）。
- `EditSelectedDimensions` 的 kEditAvoid 分支**提前 return**；`CollectSelectedSources()` 在该路径**从未被调用**（全函数只在 `kEditTrim` 分支的 965 行收集源对象）——这是「不避让源图形」的直接代码证据。
- 输入 `dimensions` = `CollectSelectedDimensions()`（377 行）：遍历 `FirstSelectedObject()` 只保留 `dimHeaderNode` → **只有被选中的尺寸参与判定**，未选中的相邻尺寸完全不进入。
- 输入 `plane`：由 `HandleComplete` 用 `BeginViewPlaneForSources(selectedDimensions)` 建立（当前以选中尺寸本身求包围盒平面）；`AvoidDimensionTextCollisions` 内部未使用 plane。

#### 5.1.2 数据结构（局部 struct）

`DimensionBoundsInfo`（736–743 行）：

- `handle`：尺寸句柄；
- `bounds`（`WorldRect`）：`gSDK->GetObjectBounds` 返回的**整个尺寸对象的平面包围框**（含尺寸界线、尺寸线、文字；世界坐标 AABB）——是当前重叠判定的唯一几何输入，属「整对象框」代理而非「纯文字框」；
- `textOffset`（`ovDimTextOffsetInCurrUnits`）：文字沿标注线的位置——inside 时是 start→文字 与 start→end 的比值（[0,1]），outside 时是从较近端点起算的带符号当前单位距离（<0 靠 start，>0 靠 end）；
- `textAboveLine`（`ovDimTextAboveLineInCurrUnits`）：文字到尺寸线的距离（当前单位）；
- `textInside`（`ovDimTextPosInside`）：文字是否在 witness 内侧；
- `dimensionLength`：`ovDimStartPt→ovDimEndPt` 的欧氏长度，用于把固定步长换算成 inside 比值步长。

读取守卫（752–762 行）：须同时读到 bounds、起终点、textOffset、textAboveLine、textInside；长度 ≤ `kGeometryTolerance` 的尺寸被跳过；`dimensionBounds.size() < 2` 直接返回 0。

#### 5.1.3 迭代与调整策略

- 常量：`kMaximumIterations = 8`（767 行）、`kAdjustmentStep = 15.0`（768 行）、重叠容差 `kTolerance = 2.0`。
- 每轮（770 行起）：双重循环所有尺寸对 `(first < second)`，AABB 判重叠（777–781 行）。
- 冲突消解**只作用于 `first`（序号较小者）**，`second` 永不动：
  - 垂直分量主导（`|dy| > |dx|`，dy/dx 为两框中心差）：`ovDimTextAboveLineInCurrUnits += 15`（**只增不减**，无方向选择）；
  - 水平分量主导：`ovDimTextOffsetInCurrUnits += direction * step`，`step` = inside 时 `15 / length`、outside 时 `15`；`direction = dx > 0 ? -1 : +1`（把 first 的文字推离 second）；inside 时若新比值越出 [0,1] 则 `continue`（该对当轮不调整，且后续轮次会重复同样失败 → **永久无法消除**）；
  - 每次成功写 `ovDimTextOffsetInCurrUnits` / `ovDimTextAboveLineInCurrUnits` + `ovDimTextPosCalculated=false`，走 `ApplyDimensionVariableTransaction`（undo 注册 + 失败回滚），成功后立即 `GetObjectBounds` 刷新 bounds（812 行）并标记 `adjusted[first]`。
- 终止：`finalCollisions == 0` 提前退出，或 8 轮用尽；返回 `adjustedCount`（**被改过的尺寸个数**，不是消除的冲突数）。

#### 5.1.4 可复用 helper 清单

- 读对象变量：`GetDimensionPoint`（343）/`GetDimensionReal`（349）/`GetDimensionBool`（367）/`GetDimensionShort`/`GetDimensionUnsignedChar`/`GetDimensionString`。
- 写对象变量：`ApplyDimensionVariableTransaction`（404，多写原子化 + 回滚）、`RegisterDimensionForUndo`（390，`AddBothSwapObject`）。
- 选择集：`CollectSelectedDimensions`（377）、`CollectSelectedSources`（1500，前向声明于 949，内部用 `IsSupportedSource` 过滤掉尺寸与插件对象自身）。
- 几何：`ComplexGeometry::Collect`（`SCollection{points, segments, detailSegments, transforms, activeContainers, truncated}`）、`CalculateAxisAlignedBounds`、`FindDominantAxis`、`CalculateOrientedBounds`、`CollectPlanBounds`（内部用 `gSDK->GetObjectTopPlanBounds` 得 `WorldRectVerts` 再转 4 条边）；另有 `GetObjectCube`、`ViewPlane::Project`、`CoordLengthToPageLengthN`。
- 说明：`GetObjectTopPlanBounds(MCObjectHandle, WorldRectVerts&)`（ISDK.h）已被 `SDKComplexGeometry.h` 的 `CollectPlanBounds` 使用，可直接复用来取源图形「顶视平面包围盒」，作为 `Collect` 失败/截断时的回退。

#### 5.1.5 已知差距（本文要补的）

1. 源图形不进判定：kEditAvoid 不收集源对象，尺寸压在源图形上时不会避让。
2. 只有「尺寸 vs 尺寸」重叠；判定用整对象 AABB，非纯文字框。
3. 调整方向无「远离源」偏好：垂直分支只往 +textAboveLine 单方向；水平分支只动 first。
4. inside 文字比值越界即放弃，密集排布时**永远不会外推到 witness 外侧**——这正是增强点 B 要解决的。

### 5.2 增强点 A：避让源图形

#### 5.2.1 数据流（何时收集、以什么形式）

- **何时收集**：在 `EditSelectedDimensions` 的 kEditAvoid 分支进入 `AvoidDimensionTextCollisions` 之前（或作为新参数传入函数内部），**每趟执行只收集一次**：`std::vector<MCObjectHandle> sources = CollectSelectedSources();`（已被 `IsSupportedSource` 过滤，天然排除尺寸与插件对象自身）。
- **表示形式（两级，MVP 只用 AABB）**：
  - 障碍框集合（推荐 MVP）：对每个 source 调 `ComplexGeometry::Collect` → `CalculateAxisAlignedBounds` 得世界坐标 AABB；若 `Collect` 为空/`truncated`，回退 `GetObjectTopPlanBounds` → `WorldRectVerts` → 手动求 AABB。存成 `std::vector<WorldRect> sourceBounds`，与 `DimensionBoundsInfo::bounds` 同构，重叠判定数学零新增。
  - 线段集（B 或精调用）：保留 `Collect(source).segments`（世界坐标，`TransformPoint` 已应用），用于「点到线段最近距离」和「重叠重心投影」；`kMaximumSegments=256` / `kMaximumObjects=512` 已有截断上限。
- 数量防护：沿用 `ComplexGeometry` 的截断上限；source 过多时按平面范围预筛或只取前 `kMaximumObjects` 个，避免每轮二次复杂度爆炸。

#### 5.2.2 重叠判定（如何参与）

- 把 `sourceBounds` 视为**只读障碍**，与 `dimensionBounds` 并列；每轮内新增第三类冲突扫描：
  - 类型 1：尺寸 vs 尺寸（现状 777–781 行逻辑不变）；
  - 类型 2（新增）：尺寸 vs 源图形，同一 `kTolerance` 判 AABB 重叠。
- 优先级：类型 2 先于类型 1（先清「压源」再清「互压」）；同一尺寸压到多个源时，先处理重叠面积/距离更大的源。
- `finalCollisions` 计数计入类型 2；终止条件不变（轮内全清或轮次耗尽）。

#### 5.2.3 调整方向优先远离源图形

- 对每个「尺寸 vs 源」重叠对计算：
  - 源图形近似重心 `Cs`（重叠源 AABB 中心，多源取重叠面积加权中心）；
  - 尺寸方向单位向量 `u = normalize(end - start)`、法向 `n = (-u.y, u.x)`；
  - 尺寸框中心 `Cd`、文字近似位置 `T`（由 `ovDimStartPt + u * (textOffset * L)` 估算，outside 时按符号折算）。
- 方向决策（替代现有「只 +15 / 只看 dx 符号」）：
  - 法向分量（textAboveLine 分支）：比较 `Cs` 与 `T` 在 `n` 上的投影 → 文字移到**远离源的那一侧**，`newAbove = textAboveLine ± step`，符号由源所在侧决定（不再恒 +15）；
  - 沿标注线分量（textOffset 分支）：看 `(Cs - T)·u` 的符号 → 往源投影的反方向挪；仍分 inside 比值 / outside 绝对距离两套步长；
  - 若两个候选轴都能消除重叠，选「移动后 `T` 到最近源 AABB 距离更大」的轴（贪心离源）。
- 防抖：同一尺寸同一轮只按一个轴调整一次（先类型 2 后类型 1），避免反复横跳。

#### 5.2.4 A 的算法步骤（伪代码）

```
kEditAvoid:
  dims      = CollectSelectedDimensions()
  if dims.empty: return
  sources   = CollectSelectedSources()                    # 新增，仅此一次
  obstacles = BuildAvoidSourceObstacles(sources)          # AABB 列表（MVP）
  AvoidDimensionTextCollisions(dims, obstacles, undo)     # 签名增加 obstacles

AvoidDimensionTextCollisions 每轮:
  # 1) 尺寸 vs 源（新增，优先）
  for d in dims:
    for o in obstacles:
      if Overlap(d.bounds, o, kTolerance):
        axis, sign = ChooseAxisAwayFrom(d, o)             # n 或 u 轴 + 远离源
        if TryMoveText(d, axis, sign, step): refresh(d.bounds)
        else: markUnresolved(d)
  # 2) 尺寸 vs 尺寸（现状逻辑）
  for i < j in dims:
    if Overlap(dims[i].bounds, dims[j].bounds, kTolerance):
      TryMoveText(dims[i], ...)                           # 仅动 first，方向远离 second
  # 3) finalCollisions = 类型1 未清 + 类型2 未清
  if finalCollisions == 0: break
```

### 5.3 增强点 B：沿标注线方向外推

#### 5.3.1 触发条件

以下任一情况且该尺寸仍未「外推过」时启用：

1. inside 文字步长调整因 `newOffset` 越出 [0,1] 被 `continue`（当前永久失败点）；
2. 同一尺寸连续 2 轮仍是同一冲突对象（互压或压源）且无法用 ±15 清除（密集排布信号）；
3. 垂直/水平分支都尝试过但重叠面积未减少。

#### 5.3.2 外推方向：由「离哪条 witness 更近 / 重叠重心」决定

- 记尺寸起点 `S`、终点 `E`、`u = normalize(E - S)`、`L = |E - S|`；重叠重心 `C`（尺寸 vs 尺寸取两框中心，尺寸 vs 源取源 AABB 中心，多源取加权）。
- 投影 `s = (C - S)·u`：
  - 方案一（近端外推，推荐默认）：`s < L/2` → 向 **start 侧**外推（outside offset 为负，`textOffset = -startClearance`）；否则向 **end 侧**外推（为正）；
  - 方案二（远离重叠重心）：若 `s < 0` 或 `s > L`（重叠重心已在 witness 外）→ 直接外推到重心所在侧之外；否则取「远离 s」的方向。
  - 组合规则：默认按方案一，若该侧外推后仍撞源/撞相邻尺寸，则换另一侧（每尺寸最多试两侧各一次）。
- 外推后写：`ovDimTextOffsetInCurrUnits = ±extrapolatedOffset` + `ovDimTextPosCalculated = false`；**不写** `ovDimTextPosInside`（其语义是只读状态描述；是否必须显式置位需实测，见 5.5.4 第 3 点）。

#### 5.3.3 步长与迭代上限

- 初始外推量 `extrapolatedOffset = max(kAdjustmentStep * 2, textHalfWidth + gap)`；MVP 无公开「文字框宽」API，先用 `kAdjustmentStep * 2 = 30`（当前单位）起步、每轮再 `+ kAdjustmentStep`（15）递增；若需贴合真实文字宽度，可用 `ovDimTextSizeInPoints` × 页面比例近似（`CoordLengthToPageLengthN` 反向换算），标记为可调参数。
- 迭代上限：单尺寸外推**只触发一次**（`extrapolated[dim] = true` 后不再拉回 inside，防震荡）；整体轮数 `kMaximumIterations` 从 8 放宽到 **12**（A/B 共用）；外推步数每尺寸 ≤ 3 步，超过则放弃该冲突并计入 `finalCollisions`（保证有界终止）。
- 终止后 trace：`edit-avoid ... extrapolated=N unresolved=M`，便于实测调参。

#### 5.3.4 B 的算法步骤（伪代码）

```
for iteration in 1..12:
  # A 部分：类型2（尺寸 vs 源）+ 类型1（尺寸 vs 尺寸），步长 ±15
  ...
  # B 部分：对本轮仍未消除冲突的尺寸（按 5.3.1 判定）
  for d in unresolved:
    if d.extrapolated: continue
    dir = DecideExtrapolateDirection(d, overlapCentroid)  # 近端 witness / 远离重心
    off = max(2 * step, initialClearance)
    for k in 1..3:
      if TryWriteOutsideTextOffset(d, dir * off):          # 置 ovDimTextPosCalculated=false
        d.extrapolated = true; refresh(d.bounds); break
      off += step
  if finalCollisions == 0: break
```

### 5.4 增强点 C（可选）：避让范围取舍

- **推荐分层**：
  - C0（必做，沿用现状语义）：只有**被选中尺寸**是「可移动 + 互相避让」的对象；A/B 都基于该集合。好处：行为可预期（用户只改选中对象）、实现最小。
  - C1（可选，下一阶段）：把与选中尺寸包围盒在平面范围相交的**未选中 `dimHeaderNode`** 也纳入障碍（只读，**永不动它们**）。理由：把选中文字推到未选中文字上在视觉上仍是失败；用 `GetObjectCube` / `GetObjectBounds` 预筛即可。取舍：选择集外的对象「被推着不动」可能让用户误以为没生效，需在帮助文案/文档里说明。
- 不建议第一版就「自动扩大选择集并移动未选中尺寸」——违反「只动选中项」的最小可用原则，undo 语义也更复杂。

### 5.5 改动范围、影响与实测点

#### 5.5.1 改动函数清单（2025/2026 两份源码都要改）

| 位置 | 改动 |
| --- | --- |
| `EditSelectedDimensions` kEditAvoid 分支（959–963 行） | 调用 `AvoidDimensionTextCollisions` 前收集 `CollectSelectedSources()`，并把障碍列表传入（签名扩展或加参数） |
| `AvoidDimensionTextCollisions`（734–840 行） | ① 新增 `sourceBounds` 障碍输入；② 每轮新增类型 2 冲突扫描与「远离源」方向决策；③ 新增 B 外推分支与 `extrapolated` 防抖；④ `kMaximumIterations` 8→12（或做成可调常量）；⑤ `finalCollisions` / trace 计数纳入类型 2 与外推 |
| 新增静态 helper（建议放 AutoDimensionObj.cpp，或复用头文件） | `BuildAvoidSourceObstacles(sources)`：Collect + CalculateAxisAlignedBounds（回退 GetObjectTopPlanBounds）→ `std::vector<WorldRect>`；可选 `ProjectOverlapCentroid(dim, obstacle)` |
| `SDKComplexGeometry.h`（可选，尽量不动） | 仅在需要「线段集参与距离判定」时补小工具；MVP 建议零改动 |

#### 5.5.2 新增字段/状态（全部为函数内局部；无插件对象参数、无模式栏/UI 改动）

- `std::vector<WorldRect> sourceBounds`（或轻量 `SMeasureObstacle` struct）；
- `std::vector<bool> extrapolated`、`std::vector<bool> blockedBySource`；
- 每尺寸记录 `lastUnresolvedRound`（用于 5.3.1 的「连续 2 轮未消除」判定）；
- 常量：`kMaximumIterations = 12`、`kExtrapolateInitial = 2 * kAdjustmentStep`、`kExtrapolateMaxSteps = 3`、`kExtrapolateStep = kAdjustmentStep`。

#### 5.5.3 对现有行为的影响

- 无源图形被选中（纯尺寸避让）时：A 新增的障碍集为空，行为与现状**逐位一致**（同一分支、同一方向规则）——这是回归锚点。
- 有源图形时：文字会额外远离源对象；密集场景文字可能被外推到 witness 外侧（外观变化，undo 仍走 `ApplyDimensionVariableTransaction`，可撤销）。
- 8→12 轮：对现有能收敛的用例只影响「多花几轮尝试」，不改变已成功的结果；对不收敛用例从「8 轮后放弃」变成「12 轮后放弃」，trace 可见。
- 未选中相邻尺寸：C0 下完全不受影响（C1 才作为只读障碍）。

#### 5.5.4 需要实测的点

1. **GetObjectBounds 是否含文字**：确认整对象 AABB 作为「文字/框重叠」代理可接受；若文字明显超出/小于对象框，考虑用 `ovDimTextOffsetInCurrUnits` + `ovDimTextAboveLineInCurrUnits` + `ovDimTextSizeInPoints` 估算文字框（需实测渲染尺寸）。
2. **单位一致性**：`ovDimTextOffsetInCurrUnits` / `ovDimTextAboveLineInCurrUnits` 是「当前单位」，现有代码直接写 15.0 世界坐标，需在 mm/inch 单位文档下验证（1:1 平面视图通常成立；必要时用 `CoordLengthToPageLengthN` 换算）。
3. **outside 外推的最小写集**：写大绝对值 `ovDimTextOffsetInCurrUnits` + `ovDimTextPosCalculated=false` 是否足够让文字渲染到 witness 外侧；是否需要同时写 `ovDimTextPosInside`（该变量 Public for VS，但语义是状态描述，写它可能被引擎忽略/回写）——必须在 2025 与 2026 各验证一次。
4. **textAboveLine 符号约定**：+15 恒增在哪一侧离尺寸线更远；源在另一侧时 `-15` 是否真的把文字移到另一侧（决定 A 的方向规则）。
5. **回归锚点**：选中两条重叠尺寸（不选源）跑避让，结果应与当前版本一致；再叠加选中源图形，验证「先离源、再互避」。
6. **密集排布**：3+ 条同向尺寸叠放，确认外推在 12 轮内收敛且无震荡（textOffset 不来回跳）。
7. **截断防护**：`Collect` 截断（>512 对象 / 256 段）时 obstacle 缺失不致命——记录 trace，不阻断流程。

### 5.6 分阶段落地建议（最小可用）

- **阶段 1（先端到端）**：A 的 AABB 障碍版（无源图形时零行为变化）→ 冒烟：选源+尺寸 → 避让；选纯尺寸 → 回归一致。
- **阶段 2**：B 外推（8→12 轮、外推防抖）→ 冒烟：密集 3+ 尺寸收敛。
- **阶段 3（可选）**：C1 未选中相邻尺寸作只读障碍。
- 每阶段单独提交、单独 trace 验证；2025/2026 双份源码同步改。

### 5.7 自查清单

- [x] 现状梳理：输入 / `DimensionBoundsInfo` / 迭代与调整策略 / 终止条件 / 可复用 helper（5.1）
- [x] 增强点 A：数据流（何时收集、障碍框 + 线段集形式）+ 重叠判定 + 远离源方向（5.2，含伪代码）
- [x] 增强点 B：触发条件 + 方向决策（witness 近端 / 重叠重心）+ 步长与迭代上限（5.3，含伪代码）
- [x] 增强点 C：取舍与分层建议（5.4）
- [x] 改动函数清单 + 新增字段 + 行为影响 + 实测点（5.5）