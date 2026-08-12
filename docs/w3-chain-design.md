# W3 研究：快速标注 / 可持续连续标注 —— 工具状态机设计与最小改动方案

> 任务性质：**只读研究 + 设计文档**，不修改任何产品代码。
> 仓库：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`（Vectorworks 2025/2026 C++ SDK 插件）。
> 关联文档：`docs/autodim-feature-design.md`（需求 #3 的语义确认与 MVP 推荐）；本文是它的「工具层」子研究，回答**工具怎么做才正确**。

> **实现状态（2026-08-13）**：本文 §3 的 H/V 投影链已落地为 `kAnnotationQuickChain=10`；任意角度链已落地为 `kAnnotationQuickChainAngle=11`（前两点定义 θ，后续点击把上一点与当前点投影到固定 θ 轴，`dimType=1 + dir=θ`，沿用「投影点对与 θ 共线」的安全路径，见 §8.3）。任意角度链的 offset 侧边符号沿用连续/交线模式的 `-offset` 约定，**仍需在 VW 2025/2026 冒烟确认**（若反向对调符号即可）。链式合并为单一链对象（W2）未做，依赖 P2-6 运行时验证。
> 结论级别：带「(SDK 源码/头文件已确认)」的为本地证据；带「需实测」的为 SDK 头文件看不到、需要运行时验证的项。

---

## 1. 现状梳理

### 1.1 工具事件类结构

- 工具扩展：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp` L1918-1929 `IMPLEMENT_VWToolExtension`（事件 sink 为 `CAutoDimensionObjDefTool_EventSink`，Universal name `KeeplAutoDimSelectionTool`）。
- 事件 sink 声明：`AutoDimensionObj.h` L42-59。当前 override：
  - `DoSetUp` / `DoSetDown` / `DoModeEvent` / `GetStatus` / `HandleComplete`；
  - 成员：`fAnnotationModeGroup=0`、`fEditModeGroup=1`、`fAnnotationMode=0`、`fEditMode=0`（L54-58）。
- 实现位置（`AutoDimensionObj.cpp`，行号以下文引用为准）：
  - `DoSetUp` L2097-2150：建两个 radio mode group（10 个标注模式 + 11 个编辑模式）、图标路径 `KeeplAutoDimTest/Images/Mode*.png`、按钮帮助文本；
  - `DoSetDown` L2152-2155；
  - `DoModeEvent` L2158-2169（切模式时写 `fAnnotationMode`/`fEditMode`）；
  - `GetStatus` L2171-2184；
  - `HandleComplete` L2186-2452。

### 1.2 标注模式常量与现状分派

- 常量：L29-42。`kAnnotationAuto=0` … `kAnnotationEnhanced=9`；编辑模式 `kEditNone=0` … `kEditResetTextPosition=10`；`kLinearDimensionTypeOrtho=0`、`kLinearDimensionTypeAligned=1`（L29-30）。
- `GetStatus`（L2171-2184）：
  - `fEditMode != kEditNone` → `GetOnePointToolStatus()`；
  - `kAnnotationManualBlock` / `kAnnotationIntersection` → `GetTwoPointToolStatus()`（两点模式）；
  - 其它（含 `kAnnotationContinuous`、Auto/Line/Centers 等）→ `GetOnePointToolStatus()`（单点模式）。
- `HandleComplete` 现有分支：
  - 编辑模式分支（L2192-2204）：选中尺寸 → 批量编辑；
  - `kAnnotationClosedSpace`（L2205-2231）：**单点**，用 `GS_TrackTool` 取光标下对象；
  - `kAnnotationContinuous`（L2233-2245）：**选择驱动批量** —— 选中线/路径 → `CreateContinuousDimensions`（L611-639）逐段建 `kLinearDimensionTypeAligned`（对齐/真长）尺寸，`dir = normalize(segment.end-start)`，offset = `-max(25, len*0.15)`；不点选、不交互；
  - `kAnnotationIntersection`（L2247-2270）：**两点模式**，`this->GetToolPt2D(0)` / `this->GetToolPt2D(1)` 取两点 → `CreateVirtualLineIntersectionDimensions`；
  - `kAnnotationEnhanced`（L2270-2286）等：选择驱动或 `GS_TrackTool`；
  - `kAnnotationManualBlock`（L2321-2420）：两点模式，`GetToolPt2D(0)/(1)`，`AddLinearDimension(…, kLinearDimensionTypeOrtho, "manual-horizontal"/"manual-vertical", …)` —— **这就是现有 dimType=0 水平/垂直投影尺寸的直接可复用范例**；
  - 兜底单点路径（L2426-2452）：`GS_TrackTool` 取对象 → `CreateDimensionsForSource`。

### 1.3 尺寸创建与平面管理

- `AddLinearDimension`（L243-259）：`gSDK->CreateLinearDimension(p1, p2, startOffset, 0.0, direction, dimensionType)` → `ApplyDimensionPresentation`（L225-236：先 `ViewPlane::ApplyPlanarRef` 再 `ResetObject`）→ `AddAfterSwapObject`。
- `ViewPlane`（`include/vwad/SDKViewPlane.h`）：`Begin(cube)` 在顶视/平面返回非平面 ground-plan（`planar=false`），在四个标准立面切换工作平面并返回 `planar=true`；`End` 恢复工作平面。`EndElevationPlaneWithAlert`（cpp L1560-1567）在立面视图弹窗中止。
- 链式合并已有实现：`kEditMerge` 分支（L1144-1183）用 `CreateChainDimension(h1, h2)`（`ISDK.h` L1008）逐对合并，前置 `ValidateAndSortMergeDimensions`（L918-965）做方向/呈现一致性校验。

### 1.4 资源/图标机制

- `DoSetUp` L2101-2104：`annotationIconNames`（`std::array<const char*, 10>`）→ `AddRadioModeGroup(fAnnotationModeGroup, annotationImages)`（L2110）。**按钮顺序 = 数组顺序**。
- 图标文件：`sdk-projects/{2025,2026}/AutoDimensionPlugin/KeeplAutoDimTest.vwr/Images/Mode*.png` + `@2x`（26×20 / 52×40）。
- 生成脚本：`scripts/generate-mode-icons.ps1`（`$names` 列表 + `Draw-Icon` 的 switch 分支，System.Drawing 绘制）。
- 2025/2026 两份源码**仅 trace 文件名不同**（`vw-autodim-runtime-2025.txt` vs `2026.txt`），其余逐字节一致（`Compare-Object` 确认）；任何改动需双份同步。

---

## 2. 问题 a/b/c/d 的结论与 SDK 依据

### 2.1 结论 a：一次 HandleComplete 后工具保持激活，可持续点选（连续多次点击天然支持）

**结论：是。** 工具完成一次 `HandleComplete` 后仍保持激活，点数组被清空并回到「等待第一点」，用户可继续下一次点击；现有 `kAnnotationIntersection` 两点模式已经是「连续多次两点点击」的生产实证。

依据：

1. 事件分发（`VWExtensionTool.cpp`，SDK 自带源码，`SDKLib\Source\VWSDK\VWFC\PluginSupport\VWExtensionTool.cpp`）：
   - `Execute` 把 `ToolMessage::kAction_GetStatus`（=105，`MiniCadCallBacks.h` L6315）路由到 `GetStatus`（L451），把 `ToolCompleteMessage::kAction`（=104，`MiniCadCallBacks.h` L6529）路由到 `HandleComplete`（L482）。`HandleComplete` 之后没有任何卸载/退出逻辑；工具的 `DoSetDown` 只在工具被切走/暂停时触发（`VWExtensionTool.h` L39-42 注释：「When tool is selected or resumed」/「When tool is deselected or paused」）。
2. 状态机语义（`MiniCadHookIntf.h` L85-93）：
   - `kToolWaitingForFirstPoint=5`、`kToolCollectingPoints=6`、`kToolWaitingForMouseUp=7`、`kToolCompleted=8`、`kToolCancel=9`、`kToolSwitchToCursor=2`。
   - 官方文档「SDK:Setting Tool Status」（web，辅助证据）：*「VectorWorks sends kToolGetStatus … how many clicks the user has to make in order the tool to be completed and kToolHandleComplete to be send. OnePointToolStatus — The tool will require single click from the user before sending kToolHandleComplete」*；「SDK:Tool Plug-in Events」：*「If kToolCompleted is returned the tool's kToolHandleComplete action handler will be called」*。
3. 生产实证：`kAnnotationIntersection` 用 `GetTwoPointToolStatus()`，每次 HandleComplete 读 `GetToolPt2D(0)` 与 `GetToolPt2D(1)`（cpp L2252-2253）。若完成一次后点数组不重置、工具退出，用户就不可能连续多次「两点点击」得到多组交线尺寸——而该模式是已上线功能（runtime trace 可见），**逻辑上必然存在「完成后清点 + 工具保持激活」**。

> 需实测（应用行为，SDK 头文件不可见）：完成后点数组由宿主清空的确切时机。回归方法见 §5 风险 1。

### 2.2 结论 b：单点模式下，HandleComplete 里用 `GetToolPt2D(0)` 取本次点击的原始世界坐标

**结论：用 `this->GetToolPt2D(0)` 即可，不需要 `GS_TrackTool` 取坐标。**

依据：

1. `VWTool_EventSink::GetToolPt2D`（`VWExtensionTool.h` L95；实现 `VWExtensionTool.cpp` L713-729）内部调 `GS_GetToolPt2D(gCBP, ptIndex, pt)`（`APIBase.Legacy.Defs.h` L6128：*「Gets the specified 2D ToolPoint. The index is a zero based index. False is returned if out of range 0<=index<GS_GetNumToolPts」*）。`GetToolPointsCount`（L93；实现 L703-706）= `GS_GetNumToolPts`（Defs.h L6120）。
2. 时序上点仍可用：点击产生 `ToolPointAddedMessage`（kAction=100，`MiniCadCallBacks.h` L6449-6457，携带 `fWorldPt`），状态返回 `kToolCompleted` 后才发 `ToolCompleteMessage`（L482）；两次消息之间 `Execute` 没有任何清点逻辑（`VWExtensionTool.cpp` L451-482）。单点模式下此时点数组恰有 1 个点（index 0），`GetToolPt2D(0)` 即本次点击的世界坐标。
3. 插件先例：`kAnnotationIntersection` 已在 HandleComplete 里用 `GetToolPt2D(0)/(1)`（cpp L2252-2253）；`kAnnotationManualBlock` 同（L2322、L2352）。
4. `GS_TrackTool`（Defs.h L6276）的用途是**对象拾取**：*「Returns in overObject the object the cursor is currently over … and in overPart one of cursOnNothing / cursOnObject / cursOnHandle …」*——它返回对象与部位，不返回坐标。现有单点对象模式（Auto/Line 等，L2426-2432）用它正是因为那些模式要「对象」而非「坐标」。链式快速标注要的是坐标，用 `GetToolPt2D(0)`。
5. 补充 API：
   - `GetToolPtCurren2D`（`VWExtensionTool.h` L97；`GS_GetToolPtCurrent2D` Defs.h L6137，*「Gets the 2D Current Smart Cursor value」*）返回**当前光标/智能光标位置（未提交）**，用于 `Draw()` 橡皮筋预览，不用于 HandleComplete 取已点击点；
   - `GetToolPt3D` / `GetToolPtCurren3D`（L96/L98）为 3D 版本；平面视图下 2D 即世界坐标；
   - `fMousePos` 在本 SDK 的**事件 sink 工具模型里不存在**（全 SDK Include/Source 检索无此符号）。它是旧式 ToolDef 内部字段；本插件用的是 `VWTool_EventSink` 事件模型，取点一律走上述 `GetToolPt*` 成员。

> 兜底方案（若实测发现 HandleComplete 时点数组已被清空）：override `PointAdded()`，从 `VWTool_EventSink::Execute` 保存的 `fMessage`（protected，L357 `fMessage = message`）`dynamic_cast<ToolPointAddedMessage*>` 取 `fWorldPt` 缓存到成员（`MiniCadCallBacks.h` L6453）。见 §5 风险 1。

### 2.3 结论 c：工具成员变量（fLastChainPoint / fChainActive）在多次 HandleComplete 之间持久存在（同一工具实例）

**结论：是。** `CAutoDimensionObjDefTool_EventSink` 实例在工具激活时创建一次，工具切走（`DoSetDown`）时才销毁；成员变量跨多次 `HandleComplete` 存活。

依据：

1. 扩展生命周期：`IMPLEMENT_VWToolExtension`（`VWExtensionTool.h` L490-499）通过 `CreateToolEventSink` 创建事件 sink；`VWExtensionTool` 持有 `fToolEventSink`（L308），随工具激活/退出而创建/销毁。
2. 现有代码已在依赖这一点：`fAnnotationMode`/`fEditMode` 是普通成员（`AutoDimensionObj.h` L54-58），由 `DoModeEvent` 写入（cpp L2158-2169），跨无数次 `HandleComplete` 生效——这是当前插件的基础工作方式（生产验证）。因此新增 `fLastChainPoint`/`fChainActive` 同样可行。
3. 生命周期边界：`DoSetUp(bRestore)` / `DoSetDown(bRestore)` 在选中/暂停/恢复时被调用（`VWExtensionTool.h` L39-42）。建议在 `DoSetDown` 复位链状态（避免残链）；若希望跨「暂停/恢复」保留（`bRestore=true`），可仅当 `!bRestore` 时复位——MVP 建议一律复位，简单可预期。

### 2.4 结论 d：新增独立标注模式 `kAnnotationQuickChain=10`（推荐），不改造 `kAnnotationContinuous`

**结论：推荐新增模式**（新按钮 + 新图标），**不改** `kAnnotationContinuous`。

依据/权衡：

1. `kAnnotationContinuous` 现状是**选择驱动的批量模式**：`GetStatus` 走单点，`HandleComplete`（L2233-2245）要求先选中线/路径，然后 `CreateContinuousDimensions`（L611-639）对每条直线段批量生成对齐（aligned）尺寸。它没有「点一下、加一段」的交互；在其上叠加逐点链交互会让同一按钮承载两套完全不同的交互语义（选中批量 vs 逐点续接），`GetStatus` 也无法同时表达两种状态，**改造风险高、会破坏既有功能**。
2. `docs/autodim-feature-design.md` §1.3 曾建议「扩展现有连续标注模式加方向子选项」——那是针对「选中路径后按 H/V 投影批量生成」的变体；本文的 W3 范围是**逐点交互链**，结论不同：交互链需要独立的工具状态机（首点锚定/续接/复位），独立模式最干净。
3. 新增模式的资源改动范围（小且局部）：
   - `AutoDimensionObj.cpp`：常量 `kAnnotationQuickChain=10`（L42 后）；`annotationIconNames` 数组（L2101-2104）追加 `"ModeQuickChain.png"`（`std::array<const char*, 10>` → `11`）；`buttonHelp`（L2126-2146）追加一条帮助；`GetStatus` 走 `GetOnePointToolStatus()`；`HandleComplete` 新增分支；新增 `OnDefaultEvent`（ESC 复位，见 §3）。
   - `AutoDimensionObj.h`：声明 `OnDefaultEvent` + 链状态成员。
   - 图标：`sdk-projects/{2025,2026}/AutoDimensionPlugin/KeeplAutoDimTest.vwr/Images/ModeQuickChain.png` + `ModeQuickChain@2x.png`（两个 vwr 都要放）；`scripts/generate-mode-icons.ps1` 的 `$names` 加 `"ModeQuickChain"` 并加 `Draw-Icon` 分支（保证可再生成、两版一致）。
   - 2026 源码同步同一份 diff（仅 trace 文件名不同）。
   - 不改 `kAnnotationContinuous` 按钮、不改现有 10+10 之外的布局（新增第 11 个标注按钮，排在「加强标注」之后）。

---

## 3. 推荐的最小改动方案：H/V 投影链（逐段 dimType=0）

### 3.1 交互状态机

单点模式 + 内部链状态（成员），**不需要** `GetPolyToolStatus` / 双击：

```
状态：fChainActive=false（未锚定） → 首点锚定（不产尺寸） → fChainActive=true
      每点：anchor → click 生成一条 H/V 投影尺寸（dimType=0），anchor=click（续接）
复位：ESC（OnDefaultEvent） / 切换模式（DoModeEvent） / 切走工具（DoSetDown）
```

- **首点**：仅记录 `fChainAnchor = click`，不生成尺寸（锚定）。
- **第 2..n 点**：从 `fChainAnchor` 到 `click`，按主导方向生成轴对齐投影段：
  - 水平（`|dx| >= |dy|`）：`p1 = anchor`，`p2 = (click.x, anchor.y)`（尺寸线取锚点 y），`dimType=0`；
  - 垂直（`|dx| < |dy|`）：`p1 = anchor`，`p2 = (anchor.x, click.y)`（尺寸线取锚点 x），`dimType=0`；
  - 生成后 `fChainAnchor = click`。
- **偏移**：沿用现有惯例 `offset = max(25, len*0.15)`；水平段 `-offset`、垂直段 `+offset`（对齐现有 horizontal/vertical 用法的符号，cpp L1375/L1378；符号方向需冒烟验证，见 §5 风险 2）。
- 零长度（`len <= kGeometryTolerance`）：跳过该段、**不更新锚点**并弹提示，避免原地叠段（伪代码同）。

### 3.2 结束 / 复位

| 方式 | 可行性 | 做法 |
| --- | --- | --- |
| **ESC**（推荐） | 可用 | 无点时按 ESC → `ToolMessage::kAction_OnEscapeKeyWithNoToolPts`（=125，`MiniCadCallBacks.h` L6331）→ `VWTool_EventSink::Execute` 的 `default:` 路由到 `OnDefaultEvent`（`VWExtensionTool.cpp` L546-548；`OnDefaultEvent` 默认实现 L876-882 仅返回 0）→ 在 override 的 `OnDefaultEvent` 里识别该 action，`fChainActive=false`，返回 `kToolSpecialKeyEventHandled`（=1，`MiniCadHookIntf.h` L95-97）。 |
| 切换标注/编辑模式 | 可用 | `DoModeEvent`（cpp L2158-2169）里 `fChainActive=false`。 |
| 切走工具 | 可用 | `DoSetDown`（cpp L2152-2155）里复位。 |
| 右键 | **不可靠** | `ENABLE_RIGHT_MOUSE_DOWN_TOOL_MESSAGE=0`（`MiniCadCallBacks.h` L6344；`VWExtensionTool.h` L80-83），右键不产生工具消息，只弹上下文菜单。 |
| 双击 | 不建议 MVP | 单点模式下双击会先产生两个点（两段尺寸）再发 `DrawingDoubleClick`（kAction=102，`MiniCadCallBacks.h` L6313；`VWExtensionTool.cpp` L516-521），要「双击结束且不生成最后一段」需在第二击前拦截，易误触；留作 W2。 |

### 3.3 伪代码（实施子代理可直接照做）

```cpp
// ---- AutoDimensionObj.h（CAutoDimensionObjDefTool_EventSink 内，L54-58 之后）----
// 新增 override 与链状态成员
virtual Sint32   OnDefaultEvent(ToolMessage* message);   // ESC 复位链

private:
    bool        fChainActive = false;
    VWPoint2D   fChainAnchor;          // 上一锚点（世界坐标）
    size_t      fChainCreatedCount = 0; // 本链已生成段数（trace/提示用）

// ---- AutoDimensionObj.cpp ----
// L42 之后新增模式常量
static constexpr size_t kAnnotationQuickChain = 10;

// DoSetUp（L2101-2104）：数组扩为 11，末尾追加
const std::array<const char*, 11> annotationIconNames = {
    "ModeAuto.png", "ModeContinuous.png", "ModeLine.png", "ModeManualBlock.png",
    "ModeIntersection.png", "ModeSelection.png", "ModeCenters.png", "ModeBoundaries.png",
    "ModeClosedSpace.png", "ModeEnhanced.png", "ModeQuickChain.png"
};
// DoSetUp buttonHelp（L2126 区域，追加一条）
buttonHelp.Append(VectorWorks::SModeBarButtonHelp(
    "快速连续标注", "点击首点后，逐点续接生成水平/垂直投影（转角）尺寸；ESC 结束。",
    VectorWorks::eModeBarButtonType_RadioMode));

// DoSetDown（L2152-2155）复位链
fChainActive = false; fChainCreatedCount = 0;

// DoModeEvent（L2158-2169）：切模式复位链
if (fAnnotationMode != kAnnotationQuickChain) { fChainActive = false; fChainCreatedCount = 0; }

// GetStatus（L2171-2184）：quick-chain 走单点
else if (fAnnotationMode == kAnnotationQuickChain) {
    fResult = pStatusProvider->GetOnePointToolStatus();
}

// HandleComplete 新增分支（放在 kAnnotationIntersection 分支附近即可）
if (fAnnotationMode == kAnnotationQuickChain) {
    if (this->GetToolPointsCount() < 1) return;               // 防御
    const VWPoint2D click = this->GetToolPt2D(0);             // 本次点击世界坐标（结论 b）
    if (!fChainActive) {                                      // 首点：仅锚定
        fChainAnchor = click;
        fChainActive = true;
        fChainCreatedCount = 0;
        VWAD_RUNTIME_TRACE("quick-chain anchor=" + FormatPoint(WorldPt(click.x, click.y)));
        return;
    }

    const WorldCoord dx = click.x - fChainAnchor.x;
    const WorldCoord dy = click.y - fChainAnchor.y;
    const WorldCoord len = std::hypot(dx, dy);
    if (len > kGeometryTolerance) {
        // 与 kAnnotationIntersection（L2256-2262）同款：两点包围立方体 + pad，建立测量平面
        const WorldCoord pad = 1.0;
        WorldCube segCube(
            WorldPt3(std::min(fChainAnchor.x, click.x) - pad, std::min(fChainAnchor.y, click.y) - pad, -pad),
            WorldPt3(std::max(fChainAnchor.x, click.x) + pad, std::max(fChainAnchor.y, click.y) + pad, pad));
        ViewPlane::SViewPlane plane = ViewPlane::Begin(segCube);
        if (EndElevationPlaneWithAlert(plane, "快速连续标注仅支持顶视/平面视图，当前标准立面视图不可用。")) {
            return;
        }
        size_t created = 0;
        gSDK->SetUndoMethod(kUndoSwapObjects);
        const WorldCoord offset = std::max<WorldCoord>(25.0, len * 0.15);
        if (std::abs(dx) >= std::abs(dy)) {                   // 水平投影段
            AddLinearDimension(fChainAnchor,
                               WorldPt(click.x, fChainAnchor.y),
                               -offset, Vector2(0.0, 0.0),
                               kLinearDimensionTypeOrtho, "quickchain-h", plane, created);
        } else {                                              // 垂直投影段
            AddLinearDimension(fChainAnchor,
                               WorldPt(fChainAnchor.x, click.y),
                               offset, Vector2(0.0, 0.0),
                               kLinearDimensionTypeOrtho, "quickchain-v", plane, created);
        }
        if (created > 0) { gSDK->EndUndoEvent(); ++fChainCreatedCount; }
        ViewPlane::End(plane);
    } else {
        gSDK->AlertInform("与上一锚点距离过近，未生成标注。");
        return;                                               // 不更新锚点，避免原地叠段
    }
    fChainAnchor = click;                                     // 当前点成为新锚点（续接）
    return;
}

// 新增 OnDefaultEvent override（ESC 复位）
Sint32 CAutoDimensionObjDefTool_EventSink::OnDefaultEvent(ToolMessage* message)
{
    if (message && message->fAction == ToolMessage::kAction_OnEscapeKeyWithNoToolPts) {
        if (fChainActive) {
            fChainActive = false;
            fChainCreatedCount = 0;
            gSDK->AlertInform("快速连续标注已结束。");
        }
        return kToolSpecialKeyEventHandled;   // = 1（MiniCadHookIntf.h L95）
    }
    return VWTool_EventSink::OnDefaultEvent(message);
}
```

### 3.4 涉及函数清单（最小改动）

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimensionObj.h` | L42-59 | 声明 `OnDefaultEvent`；新增 `fChainActive` / `fChainAnchor` / `fChainCreatedCount` |
| 同左 `.cpp` | L42 后 | `kAnnotationQuickChain = 10` |
| 同左 `.cpp` | `DoSetUp` L2101-2104 / L2126 | 图标数组 +1、帮助文本 +1 |
| 同左 `.cpp` | `DoSetDown` L2152 | 复位链 |
| 同左 `.cpp` | `DoModeEvent` L2158 | 切模式复位链 |
| 同左 `.cpp` | `GetStatus` L2171 | quick-chain 走单点 |
| 同左 `.cpp` | `HandleComplete` L2186 | 新增 quick-chain 分支（复用 `AddLinearDimension` / `ViewPlane` / `EndElevationPlaneWithAlert`） |
| 同左 `.cpp` | 新增 | `OnDefaultEvent` override（ESC 复位） |
| `sdk-projects/{2025,2026}/.../KeeplAutoDimTest.vwr/Images/` | — | 新增 `ModeQuickChain.png` + `@2x` |
| `scripts/generate-mode-icons.ps1` | `$names` + switch | 加 `"ModeQuickChain"` 与绘制分支（保持可再生成） |

复用但不修改：`AddLinearDimension`（L243）、`ViewPlane::Begin/End/ApplyPlanarRef`（`SDKViewPlane.h`）、`EndElevationPlaneWithAlert`（L1560）、`ApplyDimensionPresentation`（L225）。

可选增强（W2，不在 MVP）：结束时用 `CreateChainDimension` 把本链逐对合并为单一链对象（复用 `kEditMerge` 的 `ValidateAndSortMergeDimensions`（L918）+ L1144-1183 合并流程）；`Draw()` 里用 `GetToolPtCurren2D` 画锚点→光标的橡皮筋预览。

---

## 4. 风险与需实测的点

1. **（关键）HandleComplete 后点数组的宿主清空时机**（应用行为，SDK 头文件不可见）：结论 a/b 依赖「完成后点被清空、下次点击仍是单一新点」。回归验证：在现有 `kAnnotationIntersection` 下连续做 3 组两点点击，确认每组产出不同尺寸；若出现异常，链模式改用 §2.2 兜底方案（`PointAdded()` 缓存 `ToolPointAddedMessage::fWorldPt`），并把首点锚定逻辑挪到 `PointAdded()`。
2. **dimType=0 的 offset 符号/方向**：`-offset`（水平）/`+offset`（垂直）沿用现有对象模式的约定（cpp L1375/L1378），但链段 p1=anchor 与对象模式的 p1 方向不同，尺寸线落位需在 2025/2026 各冒烟一次（feature-design §1.4 风险 1 同类）。若落位反向，对调符号即可。
3. **混合 H/V 段的尺寸线冲突**：相邻段若一横一竖，各自按锚点 y/x 放置尺寸线，可能与前一段或图形重叠；MVP 接受（转角链本身交替方向），W2 可做「统一链外偏移」。
4. **首点无反馈**：首点不产尺寸，用户可能困惑。MVP 用 `AlertInform`/`SetModeBarHelpText`（`VWExtensionTool.h` L122）提示「点击下一点，ESC 结束」；W2 加橡皮筋预览。
5. **ESC 在有点/无点两种状态**：单点模式每次完成即清点，ESC 基本总是 `kAction_OnEscapeKeyWithNoToolPts`；若实测偶发有点残留，第一次 ESC 会走 `kToolCancel` 清点（不触发 OnEscape），再按一次 ESC 才复位——行为可接受，无需特判。
6. **双击**：单点模式双击会先产两段再发 `DrawingDoubleClick`，MVP 不拦截；W2 若要做「双击结束」，需 `DrawingDoubleClick()` 返回 false 并配合 PointAdded 逻辑，重点测试勿误产末段。
7. **2026 同步**：两版源码仅 trace 文件名不同，diff 必须镜像；图标两个 vwr 都要放；`generate-mode-icons.ps1` 一次生成两处。
8. **与 feature-design 的关系**：若后续还要「选中路径→H/V 投影批量链」（feature-design §1.3），建议做成 quick-chain 模式的子选项（路径自动拆段），仍不动 `kAnnotationContinuous`。

---

## 5. 附：SDK 依据索引（文件:行号）

- 工具事件类：`VWFC/PluginSupport/VWExtensionTool.h` L16-149（`VWTool_EventSink`）、L39-42（DoSetUp/DoSetDown 注释）、L52（GetStatus）、L63（HandleComplete 纯虚）、L87（OnDefaultEvent）、L93-99（点成员）、L122（SetModeBarHelpText）、L490-499（CreateToolEventSink 宏）
- 事件分发实现：`VWFC/PluginSupport/VWExtensionTool.cpp` L353（Execute）、L451（GetStatus）、L457-465（PointAdded/PointRemoved）、L482（HandleComplete）、L516-521（DrawingDoubleClick）、L546-548（default→OnDefaultEvent）、L703-729（GetToolPointsCount/GetToolPt2D）、L876-882（OnDefaultEvent）
- 状态/提供者：`VWFC/PluginSupport/VWExtensions.h` L112（TToolStatus）、L113-121（IToolStatusProvider）；`VWFC/PluginSupport/VWPluginToolProviders.cpp` L814-837（CStatusProvider → GS_*ToolStatus）
- 状态常量：`Kernel/API/MiniCadHookIntf.h` L77-93（kToolSwitchToCursor=2 / kToolWaitingForFirstPoint=5 / kToolCollectingPoints=6 / kToolWaitingForMouseUp=7 / kToolCompleted=8 / kToolCancel=9）、L95-97（kToolSpecialKeyEventHandled=1）
- 消息枚举：`Kernel/API/MiniCadCallBacks.h` L6305-6360（ToolMessage：GetStatus=105、DrawingDoubleClick=102、OnEscapeKeyWithNoToolPts=125、OnEnterReturnKeyForToolCompletion=127）、L6449-6457（ToolPointAddedMessage fWorldPt）、L6525-6536（ToolCompleteMessage=104）
- 取点/拾取：`Kernel/API/APIBase.Legacy.Defs.h` L6120（GS_GetNumToolPts）、L6128（GS_GetToolPt2D）、L6137（GS_GetToolPtCurrent2D）、L6276（GS_TrackTool 注释）
- 尺寸 API：`Kernel/API/APIBase.Legacy.Defs.h` L2095-2102（GS_CreateLinearDimension 权威注释：dimType 0=仅 H/V、1=与 p1→p2 同向、2=ordinate）；`Interfaces/VectorWorks/ISDK.h` L1008（CreateChainDimension）、L1029（CreateLinearDimension）
- 插件代码：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`（行号见正文）、`AutoDimensionObj.h` L42-59




