# W5 设计研究：参数面板 / 设置 —— 载体选型与 MVP 方案

> 任务性质：**只读研究 + 设计文档**，不修改任何产品代码。
> 仓库：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`（Vectorworks 2025/2026 C++ SDK 插件）。
> 源码：`sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`（两版功能同构；W4 合并后**行号不再完全一致**，见 §1.0 速查表）。
> 公共几何库：`include/vwad/SDKComplexGeometry.h`。
> 关联文档：`docs/autodim-feature-design.md`、`docs/code-review-2026-08-08.md`、`docs/w3-chain-design.md`。
> 结论级别：带「(SDK 已确认)」的为本地 SDK 头文件证据（`C:\Users\keepl\Downloads\VectorworksSDK\2025\SDK\SDKVW(784374)\SDKLib\Include`）；带「需实测」的为 SDK 头文件看不到、需要运行时验证的项。
> **行号版本说明**：本文行号复核于 **2026-08-08 最终快照**：2025（2745 行）与 2026（2763 行）**均已合并 W4 避让增强**（`kMaximumIterations=12`）。仓库是并发开发，开工前务必用 §1.0 的关键字重新 grep 复核。

---

## 0. 结论总览（先给答案）

1. **设置载体（问题 1）**：Vectorworks C++ 插件的成熟做法有四类——① 工具/菜单内弹出 `VWDialog` 模态对话框；② 用户级键值偏好 **Saved Settings**（`gSDK->GetSavedSetting/SetSavedSetting/DeleteSavedSetting`，`ISDK.h` L1710/L1725/L2462-2463）；③ 标准文件夹配置文件（`IApplicationFolders` / `GS_GetFilePathInStandardFolderN`）；④ 插件参数对象 PIO 参数（`SParametricParamDef`，本插件已有 `KeeplAutoDimTestObj` 先例，`MiniCadCallBacks.h` L1064-1065 定义 `kFieldReal=3` 等字段类型）。**推荐 = ②存储 + ①对话框 UI + 模式栏按钮触发**（见 §3）。
2. **值得配置的参数（问题 2）**：MVP 5 个 = 尺寸偏移基数 25、通用偏移比例 0.15、文字避让步长 15、文字避让迭代上限 12（W4 已合并，2025 L815 / 2026 L825）、交线去重阈值 1e-4。后续扩展集见 §4.2。
3. **默认值与向后兼容（问题 3）**：**默认值 = 当前硬编码值**；Saved Settings 键缺失时回退到默认常量，天然无迁移、无需迁移逻辑（符合「不保留向后兼容、不增加迁移逻辑」的约束）。
4. **读取时机（问题 4）**：**每次 `HandleComplete`（或每次需要参数时）调用 `GetAutoDimSettings()` 现读**，不做 `DoSetUp` 一次性缓存。理由：实时生效（设置对话框点 OK 后，下一次点击即用新值）、最小改动（无缓存失效逻辑）、成本可忽略（一次 5 次 TXString 字符串查询）。

---

## 1. 现状：硬编码参数清单（附 cpp 行号）

### 1.0 行号速查表（2025 / 2026，复核于 2026-08-08 最终快照）

> 用法：本表是「关键字 → 行号」锚点。2025 与 2026 在 W4 合并后行号不再逐行一致（2026 整体右移约 +10~+18 行），下表同时给出两版数字；若源码又变，用右侧关键字 `Select-String` 复核即可。

| 锚点 | 2025 | 2026 | 复核关键字 |
|---|---|---|---|
| 交线去重 `1e-4` | 683 | 683 | `1e-4) { duplicate` |
| `BuildAvoidSourceObstacles` | 739 | 735 | `BuildAvoidSourceObstacles` |
| `AvoidDimensionTextCollisions` | 770 | 779 | `AvoidDimensionTextCollisions(` |
| `kMaximumIterations = 12` | 815 | 825 | `kMaximumIterations` |
| `kAdjustmentStep = 15.0` | 816 | 826 | `kAdjustmentStep` |
| `kExtrapolateInitial = 2 *` | 817 | 827 | `kExtrapolateInitial` |
| `kExtrapolateStep =` | 819 | 829 | `kExtrapolateStep` |
| `kTolerance = 2.0` | 820 | 830 | `kTolerance` |
| 合并容差 `1e-4` | 1145/1148/1159 | 1163/1166/1177 | `const WorldCoord tolerance` / `std::abs(cross) >` / `overlapTolerance` |
| 角度系数 `offset * 1.25` | 1617/1683 | 1635/1701 | `offset * 1.25` |
| `kMinimumDimension` | 2269 | 2287 | `kMinimumDimension` |
| `DoSetUp` | 2315 | 2333 | `EventSink::DoSetUp` |
| `DoModeEvent` | 2379 | 2397 | `EventSink::DoModeEvent` |
| `HandleComplete` | 2415 | 2433 | `EventSink::HandleComplete` |
| 快速链偏移 `25.0, len * 0.15` | 2529 | 2547 | `25.0, len * 0.15` |
| `gArrParameters[]` | 2120 | 2138 | `gArrParameters` |
| `IMPLEMENT_VWParametricExtension` | 2136 | 2154 | `IMPLEMENT_VWParametricExtension` |
| `ForEachObjectAtPoint(... 10.0 ...)` | 2606 | 2624 | `ForEachObjectAtPoint` |

### 1.1 硬编码参数清单（行号以 2025 为准；2026 见上表）

| # | 字面量/常量 | 值 | 2025 行号 | 所在函数 | 语义 |
|---|---|---|---|---|---|
| 1 | `kGeometryTolerance` | 1e-6 | L55（文件级） | 全局 | 几何容差：零长、共线、端点比较等约 40 处（`SDKComplexGeometry.h` 亦引用） |
| 2 | `25.0`（偏移基数） | 25.0 | L500、L522、L571、L636、L701、L726、L1451、L1583、L2529 | `CreateEnhancedDimensionsForSource` ×3 / `CreateContinuousDimensions` / `CreateVirtualLineIntersectionDimensions` / `CreateBoundaryDimensionsForSelection` / `CreateElevationDimensionsForSource` / `CreateDimensionsForSource` / `HandleComplete`(quickchain) | 尺寸线偏移下限 `max(25.0, extent * ratio)` |
| 3 | `0.25`（中心/半径比例） | 0.25 | L500、L522 | `CreateEnhancedDimensionsForSource` | 弧/圆/椭圆 半径、直径、弧长偏移比例 |
| 4 | `0.15`（通用比例） | 0.15 | L571、L636、L701、L726、L1451、L1583、L2529 | 同上（除 L500/522 外全部） | 连续/交线/边界/封闭空间/整体/快速链 偏移比例 |
| 5 | `1e-4`（交线去重点阈值） | 1e-4 | L683 | `CreateVirtualLineIntersectionDimensions` | 交点去重：`hypot(...) <= 1e-4` 视为重复点 |
| 6 | `kMaximumIterations` | **12（W4 已合并）** | L815 | `AvoidDimensionTextCollisions` | 文字避让迭代上限 |
| 7 | `kAdjustmentStep` | 15.0 | L816（使用于 L874、L885-886、L931、L942-943） | `AvoidDimensionTextCollisions` | 文字避让单步位移（世界坐标） |
| 8 | `kTolerance` | 2.0 | L820（使用 L823-824、L872） | `AvoidDimensionTextCollisions` | 文字框重叠判定容差 |
| 9 | `kExtrapolateInitial` | 2 × 避让步长 | L817 | `AvoidDimensionTextCollisions` | W4 外推初值（=2×15=30） |
| 10 | `kExtrapolateStep` | = 避让步长 | L819 | `AvoidDimensionTextCollisions` | W4 外推步长 |
| 11 | `1e-4` / `*1e-6` | 1e-4 | L1145、L1148、L1159 | `ValidateAndSortMergeDimensions` | kEditMerge 合并判定容差（偏移/方向/重叠） |
| 12 | `1.25`（角度偏移系数） | 1.25 | L1617、L1683 | `CreateDimensionsForSource` | 角度尺寸偏移 = 整体偏移 × 1.25 |
| 13 | `kMinimumDimension` | 1e-6 | L2269 | `OnAutoDimMessage_GetSupportedTypes`（函数内局部常量） | auto-dim 尺寸过滤下限，非用户语义 |
| 14 | `1.0`（拾取 cube padding） | 1.0 | L2486、L2519 | `HandleComplete`(quickchain ×2) | 拾取包围盒外扩，不建议暴露 |
| 15 | `10.0`（对象拾取半径） | 10.0 | L2606 | `HandleComplete`(manual-block) | `ForEachObjectAtPoint` 搜索半径，不建议暴露 |

### 1.2 与既有评审/设计文档的对应关系

- `docs/code-review-2026-08-08.md` P2-1：`1e-4`（L683）与 `kGeometryTolerance`（L55）是**绝对世界单位**，与绘图比例强相关，建议改为相对阈值 —— 本文 §4.1 将其纳入 MVP（可配置默认 1e-4），相对化逻辑列入后续扩展。
- `docs/autodim-feature-design.md` §5.5.2：文字避让增强的 `kMaximumIterations=12`、`kExtrapolateInitial=2*kAdjustmentStep`、`kExtrapolateMaxSteps=3`、`kExtrapolateStep=kAdjustmentStep` —— **W4 已落地**（两版 L815/825=12、L817/827 与 L819/829 外推常量已存在）；本文默认值直接取 12，无漂移风险（见 §4.1 注 1）。
- `docs/code-review-2026-08-08.md` §5.5.4：`ovDimTextOffsetInCurrUnits` / `ovDimTextAboveLineInCurrUnits` 是「当前单位」，现有代码直接写 15.0 世界坐标，1:1 平面视图通常成立 —— 本文 §6 风险 1 讨论单位换算。

### 1.3 单位现状说明（影响参数「单位」列）

- 创建尺寸：`AddLinearDimension(p1, p2, startOffset, ...)` → `gSDK->CreateLinearDimension`，`startOffset` 为 `WorldCoord`（`APIBase.Legacy.Defs.h` L2093 注释：「startOffset is the distance from p1 to the dimension line」），即**世界坐标**。25.0 / 0.15×extent 都是世界坐标。
- 避让：直接读写 `ovDimTextOffsetInCurrUnits` / `ovDimTextAboveLineInCurrUnits`（`ObjectVariables.h`），是**当前单位**。1:1 平面视图下与世界坐标等价；非 1:1 需 `CoordLengthToPageLengthN`（`ISDK.h` L983）换算（开放问题，见 §6）。

---

## 2. 成熟产品做法调研（问题 1）

### 2.1 方式 A：对话框（VWDialog / VWComplexDialog），从工具内弹出

**SDK 证据（均已确认）**：
- 对话框框架：`VWFC/VWUI/Dialog.h` —— `VWDialog`（L90）、`RunDialogLayout`（L104）、`CreateDialog(title, ok, cancel, ...)`（L360）、`OnInitializeContent`（L383）、`OnDDXInitialize`（L384，纯虚）、`OnDefaultButtonEvent`/`OnCancelButtonEvent`（L393-394）、`AddDDX_EditReal`（L232）、`AddDDX_EditInteger`（L231）、`AddDDX_CheckButton`（L225）；事件表 `DEFINE_EVENT_DISPATH_MAP`（官方示例用法见 §2.5）。
- 数值控件：`VWFC/VWUI/EditRealCtrl.h` `VWEditRealCtrl`（`kEditControlReal=1`、`kEditControlAngle=2`、**`kEditControlDimension=3`**「usually an offset this is a distance that is not tied to the origin」、`kEditControlCoordinateX/Y=4/5`）——距离字段用 `kEditControlDimension` 会自动按文档单位显示/换算；`EditIntegerCtrl.h` `VWEditIntegerCtrl`。
- 输入校验：`VWFC/VWUI/ComplexDialog.h` `CDDXValidator_RealInRange` / `CDDXValidator_NumberInRange`（可给比例、迭代上限做范围校验）。
- **工具没有菜单项，怎么触发？** 三种候选：
  1. **模式栏按钮（推荐）**：`IToolModeBarInitProvider::AddButtonModeGroup(iconSpec)`（`VWFC/PluginSupport/VWExtensions.h` L164；实现 `gSDK->AddButtonMode`，`ISDK.h` L2212）。按钮点击经 `VWTool_EventSink::DoModeEvent(modeGroupID, newButtonID, oldButtonID)`（`VWFC/PluginSupport/VWExtensionTool.h` L66）送达，官方按钮帮助类型 `eModeBarButtonType_PrefButtonMode`（`ISDK.h` L230）。**官方样例 TesterModule `ExtToolResourcePopupSimple.cpp` 就是「模式栏 Options 按钮 → DoModeEvent → RunDialogLayout」的 settings 模式**（L88-112：`AddButtonModeGroup("Vectorworks/Images/ModeViewBar/Options.png")`，L129-133 `DoModeEvent` 分派到 `OnPreferences()`/`OnSampleDialog()`）。
  2. 右键菜单：`VWTool_EventSink::RightMouseDown()`（`VWExtensionTool.h` L82，受 `ENABLE_RIGHT_MOUSE_DOWN_TOOL_MESSAGE` 保护；`kAction_RightMouseDown=135`，`MiniCadCallBacks.h` L6346，返回 `kToolOverrodeRightMouseDownContext` 才不弹系统菜单）。需要自己构建右键菜单 UI，工作量大于按钮。
  3. 首点弹窗：会污染单点/两点工具的点流程（`GetStatus` 已有确定状态机），**不推荐**。
- 对话框从工具内弹出是合法流程：官方样例在 `DoModeEvent` 内直接 `dlg.RunDialogLayout("")`（模态、阻塞），`VWDialog` 返回 `EDialogButton`（`kDialogButton_Ok` 等，`Dialog.h` L73-79）。

**优缺点**：
- 优点：Vectorworks 原生外观、自动处理键盘/焦点/单位编辑控件；与官方「Preferences 按钮」交互范式一致，用户无学习成本；DDX 双向绑定减少手写取值/赋值。
- 缺点：需要写一个对话框子类（CreateDialogLayout/OnInitializeContent/OnDDXInitialize + 事件表），约 100-150 行；模态对话框在工具激活期间弹出（官方示例已验证可行，但 2025/2026 各需冒烟一次）。

### 2.2 方式 B：文档级 vs 用户级偏好（preference API）

**SDK 证据（均已确认）**：
- **用户级键值：Saved Settings** —— `gSDK->GetSavedSetting(category, setting, value)`（`ISDK.h` L1710）、`gSDK->SetSavedSetting(category, setting, value)`（`ISDK.h` L1725）、`gSDK->DeleteSavedSetting(category[, setting])`（`ISDK.h` L2462-2463）；遗留函数 `GS_GetSavedSetting` / `GS_SetSavedSetting`（`APIBase.Legacy.Defs.h` L6771 / L6880，注释分别为「Read a value from the saved settings file」「Writes a value to the saved settings file」）。**值与 `VWDialog::SetSavedValue/GetSavedValue`（`Dialog.h` L127-129）同一存储**，用户级、跨文档、随 Vectorworks 用户偏好持久化，重启不丢。
- 文档级：SDK 没有面向插件的通用「文档键值偏好」；文档级持久化通常走 PIO 参数（见 2.4）或数据记录。
- `IUserPreferences`（`Interfaces/VectorWorks/Preferences/IUserPreferences.h` L17-24）只有 `Read(fileId)` / `WriteToDefaultLocation()`，是给 Vectorworks 自身偏好文件用的接口，**不是**插件任意键值存储，不采用。
- `SetPref/SetPrefInt/SetPrefReal/SetPrefString`（`vs.py` L37605+）是设置 Vectorworks **内置偏好对话框的预定义索引项**（index 为固定常量），插件不能注册新键，**不采用**。

**结论**：用户级设置用 Saved Settings；文档级设置（每图不同）用 PIO 参数。本工具设置是「用户习惯」而非「图纸属性」，选用户级。

### 2.3 方式 C：配置文件（插件目录 / 用户目录读写）

**SDK 证据（均已确认）**：
- 标准文件夹：`Interfaces/VectorWorks/Filing/IApplicationFolders.h` —— `ForEachFileInStandardFolder`（L32）、`FindFileInStandardFolder`（L35-36）、`GetStandardFolderInUserRoot`（L39）、`GetFilePathInStandardFolder`（L45）、`FindFileInPluginFolder`（L41）、`GetUserFolder`（L34）；遗留 `GS_GetFilePathInStandardFolder` / `GS_GetFilePathInStandardFolderN`（`APIBase.Legacy.Defs.h` L4384 / L4394）、`GS_ForEachFileInStandardFolder`（L4314）。
- 文件夹枚举：`Kernel/Core/FolderSpecifiers.h` —— `kSettingsFolder=15`、`kApplicationFolder=1` 等。
- 读写文件本身需 `IFileIdentifier` / `IFile` 系列或 std::ofstream（插件现有 trace 就是直接写 `TEMP` 下文件，说明 std 文件 IO 可用）。

**优缺点**：
- 优点：设置可见、可手工编辑/备份、可随插件分发默认配置。
- 缺点：跨平台路径解析 + 文件存在性 + 格式解析/容错代码量最大；与 Vectorworks 原生「设置应进用户偏好」习惯不符；对本插件 5 个标量参数收益为零。

### 2.4 方式 D：隐藏/可见「设置」参数对象（PIO）存参数

**SDK 证据（均已确认）**：
- 本插件已有 PIO 先例：`gParametricDef`（`kParametricSubType_Point`）+ `gArrParameters[]`（`{ "SourceUUID", kFieldText }, { "Enabled", kFieldBoolean }`，2025 L2120 / 2026 L2138），`IMPLEMENT_VWParametricExtension(... "KeeplAutoDimTestObj" ...)`（2025 L2136 / 2026 L2154）。
- 字段类型：`kFieldBoolean=2`、`kFieldReal=3`（`Kernel/API/MiniCadCallBacks.h` L1064-1065）。
- 读写：`VWParametricObj::GetParamReal/SetParamReal` 等（`VWFC/Tools/ParamProviders.h` L31-32 等）；`gSDK->DefineCustomObject`（`ISDK.h` L1057）。
- 官方先例：TesterModule `ExtToolResourcePopupSimple::OnPreferences()`（L136-148）用 `VWParametricObj::DefineCustomObject("ResourcePopupObject", true)` + `CGenericParamProvider::GetParamString("shape")` 把工具设置存在 PIO 参数里 —— 即「PIO 参数承载工具设置」是官方认可的路径。

**优缺点**：
- 优点：参数**有类型**（real/bool/int）、用户可在对象信息面板（OIP）直接编辑而**不需要自写对话框**；设置随文档保存，适合「每图不同」；对象可复制到其它图纸。
- 缺点：① 需要一个实例存在于图纸中——隐藏对象可能被用户误删/清理，删除即丢设置，工具必须容忍「找不到实例→用默认」；② 工具每次要 `ForEachObjectN` 遍历文档找实例（比 Saved Settings 字符串查询重）；③ 设置是**文档级**，用户切换图纸后行为不一致；④ 现有 `KeeplAutoDimTestObj` 同时是 auto-dim 扩展对象（`OnAutoDimMessage_*`），把工具设置混进它的参数会耦合两种职责。

### 2.5 官方样例索引（可开工参考）

- `Source/Samples/VWUI Dialogs Sample/Source/UI/SampleDlg1.h/.cpp`：`VWComplexDialog` 子类最小骨架（`EVENT_DISPATCH_MAP_BEGIN/ADD_DISPATCH_EVENT/EVENT_DISPATCH_MAP_END`、`CreateDialogLayout`、`OnInitializeContent`、`OnDDXInitialize`、`SetSavedSettingsTag`）。
- `Source/Samples/TesterModule/Source/ResourcePopup/ExtToolResourcePopupSimple.cpp`：**模式栏 Preferences 按钮 + DoModeEvent + RunDialogLayout + PIO 读设置**，与本方案 UI 触发方式一一对应。
- `Source/Samples/DefaultTools/Source/CustomDefaultTools/ExtObjThePointEx.cpp`（L167-211）：工具模式栏 `AddButtonModeGroup` 与 `DoModeEvent` 的标准写法。

---

## 3. 推荐方案（问题 1 结论）

### 3.1 选型：**Saved Settings 存储 + VWDialog UI + 模式栏「设置」按钮触发**

- **存储**：`gSDK->GetSavedSetting/SetSavedSetting("KeeplAutoDim", "<ParamName>", value)` —— 用户级、跨文档、随 Vectorworks 用户偏好持久化、无文件 IO、无文档依赖、无迁移。
- **UI**：`VWComplexDialog` 子类 `CAutoDimSettingsDlg`，5 个字段（4 个 `VWEditRealCtrl` + 1 个 `VWEditIntegerCtrl`），OK/Cancel，`RunDialogLayout("") == kDialogButton_Ok` 后 `SaveAutoDimSettings()`。
- **触发**：`DoSetUp`（2025 L2315 / 2026 L2333）里 `modeBarInitProvider->AddButtonModeGroup("Vectorworks/Images/ModeViewBar/Options.png")`（复用官方内置齿轮图标，MVP 不新增图片资源），帮助文案用 `SModeBarButtonHelp(..., eModeBarButtonType_PrefButtonMode)`（`ISDK.h` L230）；按钮组按添加顺序编号，本工具两个 radio 组之后追加，**groupID = 2**（常量 `kSettingsButtonGroup = 2`）。`DoModeEvent`（2025 L2379 / 2026 L2397）先判 `modeGroupID == kSettingsButtonGroup` → `OpenSettingsDialog()`，再走现有模式切换逻辑。

### 3.2 理由（最小可用 + 长期架构）

1. **最小可用**：改动集中在「1 个对话框类 + 1 个 settings 访问器 + 9 个 `25.0`/`0.15` 调用点 + 2 个避让常量（迭代/步长）+ 1 个去重阈值 + DoSetUp/DoModeEvent 各 5 行」；不动 `GetStatus` 点流程、不动 PIO、不动 SDKComplexGeometry。
2. **实时生效**：读取时机定为「每次 `HandleComplete` 现读」（§5.2），设置对话框 OK 后**下一次点击即生效**，无需重启工具。
3. **长期架构**：所有设置读写收敛到 `SAutoDimSettings` 结构 + `GetAutoDimSettings()/SaveAutoDimSettings()` 两个函数；未来若需要「每图独立设置」可无缝换到 PIO 参数或配置文件（只改这两个函数）；对话框与存储解耦，未来加参数只动结构体 + 对话框 + 默认常量三处。
4. **符合官方范式**：模式栏按钮 + 对话框 + Saved Settings 与官方 TesterModule 样例同构，后续维护者（含 Vectorworks 社区）容易理解。

### 3.3 明确不采用

- **配置文件**（§2.3）：成本最高、收益为零。
- **PIO 参数对象**（§2.4）：文档级、依赖实例存在、工具需遍历查找；作为「每图独立设置」的后续扩展保留（§6 开放问题 8）。
- **`SetPref` 系列**（§2.2）：只能写 Vectorworks 内置偏好项，不能注册插件自定义键。
- **右键菜单 / 首点弹窗**（§2.1）：交互侵入大，MVP 不做。

---

## 4. MVP 参数集与后续扩展（问题 2）

### 4.1 MVP 最小参数集（5 个）

> 原则：**默认值 = 当前硬编码值**（问题 3），键缺失时回退默认常量，无迁移逻辑。

| # | 参数名（Saved Setting key） | 中文名 | 类型（对话框控件） | 默认值 | 单位/说明 | 替换的硬编码（2025 行号） |
|---|---|---|---|---|---|---|
| 1 | `DimOffsetBase` | 尺寸偏移基数 | Real（`kEditControlDimension`） | **25.0** | 世界坐标/文档距离；`max(base, extent*ratio)` 的下限 | L500/522/571/636/701/726/1451/1583/2529 的 `25.0` |
| 2 | `DimOffsetRatio` | 通用偏移比例 | Real（`kEditControlReal`） | **0.15** | 无量纲；作用于线性/连续/交线/边界/封闭/整体/快速链 | 上表同位置 `0.15` |
| 3 | `AvoidStep` | 文字避让步长 | Real（`kEditControlDimension`） | **15.0** | 世界坐标/文档距离；`kAdjustmentStep` | 2025 L816（使用于 L874/885-886/931/942-943） |
| 4 | `AvoidIterations` | 文字避让迭代上限 | Integer（`VWEditIntegerCtrl`） | **12** | 整数 ≥1；W4 已合并（2025 L815 / 2026 L825） | `kMaximumIterations` |
| 5 | `IntersectionDedupTolerance` | 交线去重阈值 | Real（`kEditControlReal`） | **1e-4** | 世界坐标距离；`hypot(...) <= t` 判重 | L683 `1e-4` |

**注 1（迭代上限默认值，已解决）**：W4 避让增强已合并到两版工作树，`kMaximumIterations` 现为 12（2025 L815 / 2026 L825），因此默认值取 12，与代码一致，无漂移。一般原则仍为「默认 = 开工时工作树的实际硬编码值」。

**注 2（0.25 不纳入 MVP）**：L500/L522 的中心/半径偏移比例 0.25 与通用 0.15 语义不同（半径直径标注间距 vs 线性标注间距）。MVP 只暴露 0.15，0.25 保持硬编码，避免「一个参数改了中心标注行为」的默认漂移；需要时按扩展集加入。

**注 3（去重阈值单位）**：1e-4 是绝对世界坐标（code-review P2-1 已指出与比例相关）。MVP 先可配置（默认 1e-4 不变）；相对化（`max(1e-6*len, 1e-9)`）作为后续扩展。

### 4.2 后续扩展参数集（不在 MVP）

| 参数 | 默认值 | 说明 |
|---|---|---|
| `CenterOffsetRatio` 中心/半径偏移比例 | 0.25 | L500/522；半径直径/弧长标注 |
| `AvoidOverlapTolerance` 避让重叠容差 | 2.0 | 2025 L820 / 2026 L830 `kTolerance` |
| `ExtrapolateInitialFactor` 外推初值倍数 | 2（×避让步长） | 2025 L817 / 2026 L827 `kExtrapolateInitial`（W4 已存在） |
| `ExtrapolateStep` 外推步长 | = 避让步长 | 2025 L819 / 2026 L829 `kExtrapolateStep`（W4 已存在） |
| `MergeToleranceBase` 合并判定绝对容差 | 1e-4 | 2025 L1145/1148/1159 / 2026 L1163/1166/1177 |
| `MergeToleranceRelative` 合并判定相对系数 | 1e-6 | 同上 `*1e-6` |
| `AngleOffsetFactor` 角度偏移系数 | 1.25 | 2025 L1617/1683 / 2026 L1635/1701 |
| `GeometryTolerance` 几何容差 | 1e-6 | **不建议暴露**：约 40 处引用、全局影响，误改风险高；保持常量 |
| 单位模式（世界坐标 vs 当前单位） | — | 不是参数，是内部一致性修正，见 §6 风险 1 |
| 尺寸文字大小 | — | 当前代码已移除 `kDimensionTextSizePoints`，无需求 |

---

## 5. 实施路径（问题 4 + 可直接开工）

### 5.1 涉及文件（2025/2026 双份同步）

| 文件 | 改动 |
|---|---|
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimSettings.h`（新建） | `SAutoDimSettings` 结构 + `GetAutoDimSettings()` / `SaveAutoDimSettings()` 声明 + 默认常量 |
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimSettings.cpp`（新建） | Saved Settings 读写实现（`gSDK->GetSavedSetting/SetSavedSetting`） |
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimSettingsDlg.h/.cpp`（新建，或并入 AutoDimensionObj.cpp） | `CAutoDimSettingsDlg : VWComplexDialog`；5 个字段 |
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimensionObj.cpp` | ① 9 处 `25.0`/`0.15` → `settings.xxx`；② L683 → `settings.intersectionDedupTolerance`；③ `kMaximumIterations`/`kAdjustmentStep` → `settings.avoidIterations/avoidStep`；④ `DoSetUp` 加 `AddButtonModeGroup` + 帮助；⑤ `DoModeEvent` 加 `kSettingsButtonGroup` 分支；⑥ 新增 `OpenSettingsDialog()` |
| `sdk-projects/{2025,2026}/AutoDimensionPlugin/AutoDimensionPlugin.vcxproj` + `.xcodeproj` | 新增 4 个源文件（settings 2 个 + 对话框 2 个；若对话框并入 .cpp 则 2 个） |
| `KeeplAutoDimTest.vwr/Strings/KeeplAutoDim.vwstrings` | 对话框标题/标签中文字符串（如 `settings_title`、`param_dim_offset_base` 等） |

> 若想进一步缩小改动面：MVP 可把 `SAutoDimSettings`、对话框类**全部放进 `AutoDimensionObj.cpp`**（文件已有 2700+ 行，但省去工程文件改动）。推荐独立文件（便于测试与后续扩展），二选一由实现者定，本文按独立文件写。

### 5.2 读取时机：每次 `HandleComplete` 现读（结论）

- **做法**：静态助手函数（`CreateContinuousDimensions` 等）各自在函数开头调一次 `const SAutoDimSettings s = GetAutoDimSettings();`，签名不变、改动局部；`AvoidDimensionTextCollisions`（W4 后签名已带 `sourceBounds`，2025 L770 / 2026 L779）、`CreateVirtualLineIntersectionDimensions` 同理。工具 sink 的 `HandleComplete`（2025 L2415 / 2026 L2433）本身**不需要**读，因为所有调用都走这些助手。
- **为什么不 DoSetUp 缓存一次**：
  1. **实时生效**：用户点模式栏「设置」→ 改值 → OK，下一次点击（下一次 `HandleComplete`）即用新值；若 DoSetUp 缓存，则必须再实现「对话框 OK 后刷新工具成员缓存」的失效逻辑，且工具激活期间外部改动无法感知。
  2. **最小改动**：无新增成员变量、无缓存失效路径、无状态同步 bug 面。
  3. **成本可忽略**：Saved Settings 是 TXString 查找（单次 <1µs 量级），每次点击 5 次查找，与后续几何遍历相比可忽略。
- **权衡记录**：链式模式（W3）每步都走 `HandleComplete`，因此「中途改设置」也会在下一步生效——行为一致且符合直觉，无需特判。

### 5.3 UI 触发方式：模式栏按钮

- `DoSetUp`（2025 L2315-2370 / 2026 L2333-2388）在现有两个 `AddRadioModeGroup` 之后追加：
  ```cpp
  modeBarInitProvider->AddButtonModeGroup("Vectorworks/Images/ModeViewBar/Options.png");
  ```
  （按钮组 ID 按添加顺序编号，故 `kSettingsButtonGroup = 2`；与官方样例 `EModeBarGroupSimple::PrefsButton = 3` 的编号规则一致。）
- 帮助文案追加到 `SetModeBarButtonsText`（2025 L2367 / 2026 L2385）数组：`SModeBarButtonHelp("设置…", "调整标注偏移、文字避让与交线去重参数。", eModeBarButtonType_PrefButtonMode)`（`ISDK.h` L230）。
- `DoModeEvent`（2025 L2379-2395 / 2026 L2397-2413）开头加：
  ```cpp
  if (modeGroupID == kSettingsButtonGroup) { OpenSettingsDialog(); return; }
  ```
- `OpenSettingsDialog()`：
  ```cpp
  void CAutoDimensionObjDefTool_EventSink::OpenSettingsDialog()
  {
      CAutoDimSettingsDlg dlg;
      if (dlg.RunDialogLayout("") == kDialogButton_Ok) {
          SaveAutoDimSettings(dlg.GetSettings());
      }
  }
  ```

### 5.4 伪代码

```cpp
// ---- AutoDimSettings.h ----
namespace AutoDimensionPlugin {
struct SAutoDimSettings {
    WorldCoord dimOffsetBase            = 25.0;   // = 当前硬编码值
    double     dimOffsetRatio           = 0.15;
    WorldCoord avoidStep                = 15.0;
    size_t     avoidIterations          = 12;     // W4 已合并（2025 L815 / 2026 L825）
    WorldCoord intersectionDedupTolerance = 1e-4;
};
SAutoDimSettings GetAutoDimSettings();            // 键缺失 → 默认常量（天然无迁移）
void SaveAutoDimSettings(const SAutoDimSettings& s);
}

// ---- AutoDimSettings.cpp ----
static const TXString kSettingsCategory = "KeeplAutoDim";

SAutoDimSettings GetAutoDimSettings()
{
    SAutoDimSettings s;   // 全部默认 = 当前硬编码值
    TXString v;
    if (gSDK->GetSavedSetting(kSettingsCategory, "DimOffsetBase", v))
        s.dimOffsetBase = ParseWorldCoord(v);                 // 精度序列化，见 §6 风险2
    if (gSDK->GetSavedSetting(kSettingsCategory, "DimOffsetRatio", v))
        s.dimOffsetRatio = ParseDouble(v);
    if (gSDK->GetSavedSetting(kSettingsCategory, "AvoidStep", v))
        s.avoidStep = ParseWorldCoord(v);
    if (gSDK->GetSavedSetting(kSettingsCategory, "AvoidIterations", v))
        s.avoidIterations = static_cast<size_t>(ParseSint32(v));
    if (gSDK->GetSavedSetting(kSettingsCategory, "IntersectionDedupTolerance", v))
        s.intersectionDedupTolerance = ParseDouble(v);
    return s;
}

void SaveAutoDimSettings(const SAutoDimSettings& s)
{
    gSDK->SetSavedSetting(kSettingsCategory, "DimOffsetBase",   ToSettingString(s.dimOffsetBase));
    gSDK->SetSavedSetting(kSettingsCategory, "DimOffsetRatio",  ToSettingString(s.dimOffsetRatio));
    gSDK->SetSavedSetting(kSettingsCategory, "AvoidStep",       ToSettingString(s.avoidStep));
    gSDK->SetSavedSetting(kSettingsCategory, "AvoidIterations", ToSettingString((Sint32)s.avoidIterations));
    gSDK->SetSavedSetting(kSettingsCategory, "IntersectionDedupTolerance", ToSettingString(s.intersectionDedupTolerance));
}

// ---- 调用点替换（示例，AutoDimensionObj.cpp）----
static size_t CreateVirtualLineIntersectionDimensions(...)   // 2025 L642
{
    const SAutoDimSettings s = GetAutoDimSettings();
    ...
    if (std::hypot(...) <= s.intersectionDedupTolerance) { duplicate = true; break; }   // 原 L683 1e-4
    const WorldCoord offset = -std::max<WorldCoord>(s.dimOffsetBase, virtualLine.length * s.dimOffsetRatio); // 原 L701
    ...
}

static size_t AvoidDimensionTextCollisions(...)              // 2025 L770 / 2026 L779（W4 已带 sourceBounds 参数）
{
    const SAutoDimSettings s = GetAutoDimSettings();
    ...
    for (size_t iteration = 0; iteration < s.avoidIterations; ++iteration) { }  // 原 2025 L815
    const WorldCoord step = s.avoidStep;                                        // 原 2025 L816
    ...
}

// ---- AutoDimSettingsDlg.h ----
class CAutoDimSettingsDlg : public VWComplexDialog
{
public:
    CAutoDimSettingsDlg();
    const SAutoDimSettings& GetSettings() const { return fSettings; }
protected:
    bool CreateDialogLayout() override;      // CreateDialog + 5 控件 + 布局
    void OnInitializeContent() override;     // fSettings = GetAutoDimSettings(); 供 DDX 初始值
    void OnDDXInitialize() override;         // AddDDX_EditReal/EditInteger 绑定到 fSettings 成员
private:
    SAutoDimSettings   fSettings;
    VWEditRealCtrl     fBaseCtrl;   // kEditControlDimension
    VWEditRealCtrl     fRatioCtrl;  // kEditControlReal
    VWEditRealCtrl     fStepCtrl;   // kEditControlDimension
    VWEditIntegerCtrl  fIterCtrl;
    VWEditRealCtrl     fDedupCtrl;  // kEditControlReal
};
// CreateDialogLayout 关键片段
// fBaseCtrl.CreateControl(this, 25.0, 10, VWEditRealCtrl::kEditControlDimension);
// fIterCtrl.CreateControl(this, 12, 6);
// 校验：CDDXValidator_RealInRange(比例 > 0)、CDDXValidator_NumberInRange(迭代 >= 1)
```

### 5.5 冒烟清单（2025/2026 各一次）

1. **默认值回归锚点**：装新插件 → 打开设置 → 不改值点 OK → 各模式输出与当前版本**逐位一致**（这是「默认=硬编码」的验收标准）。
2. **持久化**：改 `DimOffsetBase=50` → OK → 连续标注尺寸线外移 → **重启 Vectorworks** → 设置仍在（Saved Settings 用户级持久化）。
3. **实时生效**：工具激活状态下改 `AvoidStep=30` → 不做任何重选，直接避让 → 位移变大。
4. **范围校验**：迭代上限输入 0 / 比例输入负数 → 对话框报错不关闭（`CDDXValidator_*`）。
5. **键缺失兜底**：手动删掉 Saved Settings 键（或用 `DeleteSavedSetting` 的测试钩子）→ 工具行为回退默认，不崩。
6. **模式栏按钮**：齿轮按钮出现、点击弹窗、帮助文字（中文）正确；切换 10+11 个模式按钮不受影响（groupID=2 不冲突）。
7. **Mac/Windows**：对话框布局与单位显示在双平台一致（`kEditControlDimension` 在 inch 文档下显示 inch）。

---

## 6. 风险与开放问题

1. **单位语义（需实测，最高风险）**：`DimOffsetBase/AvoidStep` 以世界坐标写入 `CreateLinearDimension` 的 `startOffset` 与 `ovDimTextOffsetInCurrUnits/ovDimTextAboveLineInCurrUnits`；后者是「当前单位」。1:1 平面视图下两者等价，非 1:1（或非 mm/inch 单位）下可能出现偏差。对话框用 `kEditControlDimension` 统一按文档单位显示/换算；若实测发现创建与避让尺度不一致，需在设置读取处用 `CoordLengthToPageLengthN`（`ISDK.h` L983）换算——**先冒烟再定**（对应 code-review §5.5.4 第 2 点）。
2. **精度序列化**：`IntersectionDedupTolerance=1e-4`、未来 `GeometryTolerance=1e-6` 属小数值；Saved Settings 存字符串，需足够精度（`gSDK->DoubleToString`（`ISDK.h` L1697）/ `StringToDouble`（L1728）或 `std::to_string`+`std::strtod`，注意 `std::to_string` 默认 6 位有效数字会丢 1e-6 精度，**必须用高精度格式**）。
3. **行号漂移（并发开发）**：本仓库是并发开发，W4 合并已导致行号两次整体右移（2025 2528→2745 行；2026 同步中从 2528→2763 行），且两版行号不再逐行一致。本文行号已按 2026-08-08 最终快照复核，但**开工前必须按 §1.0 关键字重新 grep 复核**，以关键字定位为准。
4. **0.25 中心比例不在 MVP**：用户无法单独调中心/半径偏移比例；若需求方明确要求，立即把 `CenterOffsetRatio` 提进 MVP（默认 0.25）。
5. **按钮组 ID 脆弱性**：`kSettingsButtonGroup = 2` 依赖「两个 radio 组之后追加」的添加顺序；未来若在按钮前再插模式组需重算。MVP 接受（代码内用命名常量 + 注释说明规则）。
6. **模态对话框与工具生命周期**：`DoModeEvent` 内 `RunDialogLayout` 是官方样例路径，但需在 2025/2026 实测：弹窗期间工具点状态、ESC、视图切换无异常；弹窗返回后 `DoModeEvent` 不应误改 `fAnnotationMode/fEditMode`（先 `return`）。
7. **设置只影响新生成尺寸**：已生成尺寸不追溯（避让是编辑模式动作，读取时点生效；创建类参数只影响新对象）。文档/帮助里需说明。
8. **未来每图独立设置**：若需求变成「每个图纸一套参数」（如按比例），迁移到 PIO 参数对象（`kFieldReal`，`MiniCadCallBacks.h` L1064-1065；复用本插件 `KeeplAutoDimTestObj` 先例或新建隐藏 PIO）——只需改 `GetAutoDimSettings/SaveAutoDimSettings` 两个函数，UI/调用点不动。这是本文「长期架构」预留的迁移面。

---

## 7. 自查清单

- [x] 问题 1（设置载体）：四类载体逐一调研，给出 SDK API 名 + 头文件行号 + 优缺点（§2），并给出唯一推荐（§3）。
- [x] 问题 2（哪些参数可配置）：MVP 5 个（§4.1）+ 后续扩展集（§4.2），来源为硬编码清单（§1）与既有文档的 W4/评审意见。
- [x] 问题 3（默认值/兼容）：默认 = 当前硬编码值，键缺失回退默认，无迁移（§4.1、§5.4）。
- [x] 问题 4（读取时机）：每次 `HandleComplete` 现读，理由充分（§5.2）。
- [x] 给出了可直接开工的 MVP：涉及文件、函数、UI 触发、伪代码、冒烟清单（§5）。
- [x] 每个结论有依据：SDK 头文件行号（2025 本地路径）或官方样例文件；待实测项明确标注「需实测」；行号已按 2026-08-08 最终快照（两版均 W4 合并后）复核并给出双版速查表。

## 附：SDK 依据索引（文件:行号）

- Saved Settings：`Interfaces/VectorWorks/ISDK.h` L1710（GetSavedSetting）、L1725（SetSavedSetting）、L2462-2463（DeleteSavedSetting）；`Kernel/API/APIBase.Legacy.Defs.h` L6771/L6880（GS_Get/SetSavedSetting）；`vs.py` L6364（DelSavedSettings）、L17619（GetSavedSetting）、L37917（SetSavedSetting）
- 对话框：`VWFC/VWUI/Dialog.h` L90（VWDialog）、L104-105（RunDialogLayout）、L360（CreateDialog）、L383-384（OnInitializeContent/OnDDXInitialize）、L393-394（OnDefault/CancelButtonEvent）、L231-232（AddDDX_EditInteger/EditReal）、L123-129（SetSavedSettingsTag/SetSavedValue/GetSavedValue）；`VWFC/VWUI/ComplexDialog.h`（VWComplexDialog + CDDXValidator_RealInRange/NumberInRange）；`VWFC/VWUI/EditRealCtrl.h`（kEditControlDimension=3）；`VWFC/VWUI/EditIntegerCtrl.h`
- 模式栏按钮：`VWFC/PluginSupport/VWExtensions.h` L159-175（IToolModeBarInitProvider，L164 AddButtonModeGroup）；`VWFC/PluginSupport/VWExtensionTool.h` L66（DoModeEvent）、L82（RightMouseDown）；`Interfaces/VectorWorks/ISDK.h` L2212（AddButtonMode）、L2290（SetModeBarButtonsText）、L228-233（EModeBarButtonType，L230 PrefButtonMode）、L243（SModeBarButtonHelp 默认构造）
- 标准文件夹/配置文件：`Interfaces/VectorWorks/Filing/IApplicationFolders.h` L32-41（ForEachFileInStandardFolder/FindFileInStandardFolder/GetStandardFolderInUserRoot/FindFileInPluginFolder/GetUserFolder 等）、L45（GetFilePathInStandardFolder）；`Kernel/API/APIBase.Legacy.Defs.h` L4314（GS_ForEachFileInStandardFolder）、L4384/L4394（GS_GetFilePathInStandardFolder/N）；`Kernel/Core/FolderSpecifiers.h` L17（kApplicationFolder=1）、L32（kSettingsFolder=15）
- 偏好 API 边界：`Interfaces/VectorWorks/Preferences/IUserPreferences.h` L17-24（Read/WriteToDefaultLocation）；`vs.py` L37605+（SetPref 系列为内置偏好项）
- PIO 参数：`Kernel/API/MiniCadCallBacks.h` L1064-1065（kFieldBoolean=2/kFieldReal=3）；`VWFC/Tools/ParamProviders.h` L31-32（GetParamReal/SetParamReal）；`Interfaces/VectorWorks/ISDK.h` L1057（DefineCustomObject）；`VWFC/PluginSupport/VWExtensionParametric.h` L39-40（Preference()）
- 单位：`Interfaces/VectorWorks/ISDK.h` L983（CoordLengthToPageLengthN）；`Kernel/API/APIBase.Legacy.Defs.h` L2093-2102（GS_CreateLinearDimension 注释，startOffset=WorldCoord）
- 数值转换：`Interfaces/VectorWorks/ISDK.h` L1697（DoubleToString）、L1728（StringToDouble）
- 官方样例：`Source/Samples/VWUI Dialogs Sample/Source/UI/SampleDlg1.h/.cpp`；`Source/Samples/TesterModule/Source/ResourcePopup/ExtToolResourcePopupSimple.cpp`（L88-133 模式栏按钮 + DoModeEvent + RunDialogLayout；L136-148 PIO 读设置）；`Source/Samples/DefaultTools/Source/CustomDefaultTools/ExtObjThePointEx.cpp`（L167-211）
- 插件源码：`sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp`（L55 常量；L500/522/571/636/701/726/1451/1583/2529 偏移；L683 去重；L815-820 避让常量；L1145/1148/1159 合并容差；L1617/1683 角度系数；L2120-2134 PIO 参数、L2136/2144 扩展注册；L2269 kMinimumDimension；L2315-2370 DoSetUp；L2379-2395 DoModeEvent；L2415 HandleComplete；L2606 ForEachObjectAtPoint；L2734 OnDefaultEvent）；`sdk-projects/2026/.../AutoDimensionObj.cpp`（行号见 §1.0 速查表，整体右移 +10~+18）；`AutoDimensionObj.h`（sink 成员）；`ModuleMain.cpp`（仅注册 Tool + Parametric 两种扩展）
- 复核命令（开工前必跑）：`Select-String -Path sdk-projects/{2025,2026}/AutoDimensionPlugin/Source/AutoDimensionObj.cpp -Pattern '25.0, len * 0.15','kMaximumIterations','kAdjustmentStep','1e-4) { duplicate','EventSink::DoModeEvent','EventSink::HandleComplete'`
