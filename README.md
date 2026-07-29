# Vectorworks Auto Dimension Plugin

一个独立的 Vectorworks 2025/2026 C++ SDK 自动尺寸标注小插件。

它和 LightA4 没有关系，不依赖外部 TCP 服务，也不需要任何桌面端程序长期保活。尺寸定义由 Vectorworks 插件对象在 Vectorworks 进程内通过官方 Auto-dimension 事件返回。

## 当前状态

- 已建立独立工程：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`
- 已接入 Vectorworks 2025 SDK 工程并通过 Release 构建。
- 已接入 Vectorworks 2026 SDK 工程并通过 Release 构建。
- 已生成插件产物：
  - `dist/2025/AutoDimensionPlugin.vlb`
  - `dist/2025/AutoDimensionPlugin.vwr`
  - `dist/2026/AutoDimensionPlugin.vlb`
  - `dist/2026/AutoDimensionPlugin.vwr`
- 插件注册了 `KeeplAutoDimTestObj` 参数对象和 `KeeplAutoDimSelectionTool` 普通工具。
- 工具对一个或多个选中的现有对象直接创建 Vectorworks 尺寸对象，不复制源图形。支持符号、灯具、线段、开放/封闭二维对象和三维对象的总体水平/垂直尺寸。
- 工具模式栏提供自动识别、连续、线对象、交线、选择对象、中心点、边界、封闭空间和增强标注模式；编辑组提供转换、剪齐、对齐、分割/延伸、文字方向、标注点、合并、避让和文字重置操作。
- 工具按当前视图选择测量平面。顶视图/俯视图和其它视图沿用地平面标注；前、后、左、右四个标准立面视图会临时把工作平面设到面向相机的一侧，用真实 Z 范围生成深度尺寸，尺寸对象带工作平面的 planar reference，运行后恢复原工作平面。
- 斜线会额外按真实端点生成斜向真长度和相对水平线的角度尺寸。例如水平投影约 `36`、垂直投影 `62` 的线段，斜长约为 `71.6`，角度约为 `60°`。
- 开放多段线会读取真实边，按长度排序后最多追加 3 条直边的对齐尺寸，并为主方向追加一个角度尺寸。
- 封闭多边形、矩形、椭圆、组、旋转符号和灯具参数对象会遍历内部几何；主方向偏离水平/垂直超过 `2°` 时，追加局部轴总体宽、高和主方向角度。
- 嵌套符号和灯具内部点会应用实例矩阵；遍历限制为 8 层、512 个对象、512 个点和 256 条边，避免复杂资源产生失控的尺寸数量。
- 曲线和不能提取内部边的三维实体继续使用水平/垂直总体投影尺寸，不会把所有网格边都标出来。
- 立面视图下每个对象只生成两条尺寸：`view-width` 是相机实际看到的横向范围，`view-depth` 是对象的真实 Z 高度。斜线角度、开放路径分段和局部轴尺寸属于平面规则，立面视图不生成。
- 立面视图的横向范围优先用遍历得到的二维几何（平面轴不含 Z 分量，所以收紧横向不会影响高度），Z 范围只能来自 `GetObjectCube`。旋转符号和灯具的包围盒本身就偏大，这类对象的 Z 高度会跟着偏大。
- 立面视图下中心点模式用投影后的中心点计算最近邻，符号和参数对象仍以插入点为中心。
- 参数对象仍保留官方 Auto-dimension 回调，用于验证 Graphic Legend 对参数化对象的支持；普通对象的主流程是直接创建尺寸。
- 标注工具模式栏现提供自动识别、连续标注、线对象、手动图块定位、交线角度、选择对象、中心点、块边界、封闭空间和增强标注模式；增强模式对圆弧生成原生半径、直径和弧长尺寸。
- 编辑模式栏现提供转换、剪齐、对齐、分割/延伸、文字方向、标注点、合并、文字避让、重置文字和重置文字位置操作；这些操作只处理选中的原生尺寸对象。
- 详细完成状态和剩余任务见 [TODO.md](TODO.md)。其中尺寸线真实剪切、基于碰撞检测的文字避让和图块第二点交互仍属于后续开发项。

## Vectorworks 验证

官方 Auto-dimension 回调只能由参数化对象实现，不能直接注入普通 Vectorworks 对象。因此普通对象使用 SDK 的 `CreateLinearDimension` 直接生成尺寸对象。

当前版本需要在 Vectorworks 里验证：

1. 安装插件到 Vectorworks 用户插件目录。
2. 重启 Vectorworks。
3. 在工作空间里添加或调用 `Keepl Auto Dimension`。
4. 预先选择一个或多个符号、灯具、线段、二维对象或三维对象，再激活工具；没有预选对象时，也可以在绘图区点击单个对象。
5. 在模式栏选择 `Auto recognize` 或 `Selection`，确认每个所选对象被独立标注，源对象没有被复制。
6. 选择至少两个对象并切换到 `Centers`，确认只生成连接相邻对象的中心到中心尺寸，不生成所有对象两两组合；使用 `Boundaries` 可检查对象边界尺寸。
7. 对斜线确认同时生成水平投影、垂直投影、斜向真长度和角度，共四个原生尺寸对象。
8. 对开放多段线确认保留总体水平/垂直尺寸，并最多追加 3 条最长直边尺寸；曲线段只参与总体范围，不当作直边。
9. 对旋转封闭对象、组、符号或灯具确认追加局部轴宽、高和一个角度；轴向对象不重复生成局部轴尺寸。
10. 确认尺寸文字、箭头、撤销和重做正常。
11. 分别确认顶、前、侧视图的尺寸轴向正确。
12. 切到 `Front` 视图选中一个有高度的三维对象，确认生成的两条尺寸贴在对象前方，`view-depth` 的读数等于对象真实高度，而不是顶视投影的 Y 尺寸。
13. 切到 `Left` 或 `Right` 视图重复上一步，确认横向读数换成 Y 方向范围，高度读数不变。
14. 立面视图标注结束后打开工作平面列表，确认当前工作平面回到运行前的状态。
15. 切到等轴测视图，确认工具退回地平面标注并在 `vw-autodim-runtime-*.txt` 里记录 `view-plane name=ground-plan`。
16. 选择圆弧并使用 `Enhanced`，确认生成半径、直径和弧长三类原生尺寸；选择两个相交线段使用 `Intersections`，确认生成交点角度。
17. 选择已生成的尺寸对象切换编辑模式，依次验证文字方向、尺寸点、文字重置、位置重置、对齐和合并，并确认撤销/重做。

按灯具字段选择专用测量点仍属于后续规则。等轴测和透视视图不做深度标注：投影后的长度不等于真实长度，这一版不把投影长度伪装成三维测量。

## 构建

本机已经验证过 MSBuild 路径：

```powershell
.\scripts\build-release.ps1
```

只构建单个版本：

```powershell
.\scripts\build-release.ps1 -Version 2025
.\scripts\build-release.ps1 -Version 2026
```

构建并生成 zip 包：

```powershell
.\scripts\build-release.ps1 -Package
```

## 安装

安装单个版本：

```powershell
.\install-built-plugin.ps1 -Version 2025
.\install-built-plugin.ps1 -Version 2026
```

两个版本都安装：

```powershell
.\install-built-plugin.ps1 -All
```

安装目标：

- `%APPDATA%\Nemetschek\Vectorworks\2025\Plug-ins\AutoDimensionPlugin`
- `%APPDATA%\Nemetschek\Vectorworks\2026\Plug-ins\AutoDimensionPlugin`

安装时会同时复制 `.vlb` 和 `.vwr`。

## 项目结构

- `sdk-projects/2025/AutoDimensionPlugin`: Vectorworks 2025 SDK 插件工程。
- `sdk-projects/2026/AutoDimensionPlugin`: Vectorworks 2026 SDK 插件工程。
- `src/AutoDimensionGeometry.cpp`: 脱离 SDK 的尺寸定义计算原型。
- `include/vwad/AutoDimensionGeometry.h`: 脱离 SDK 的几何/尺寸数据结构。
- `include/vwad/SDKComplexGeometry.h`: 复杂对象的二维几何遍历与主方向计算。
- `include/vwad/SDKViewPlane.h`: 按当前视图选择测量平面，立面视图下的工作平面与投影计算。
- `sdk/EventSinkIntegrationExample.cpp`: 官方 Auto-dimension 回调接入示例。
- `resources/AutoDimensionPlugin.credentials.json.template`: Vectorworks 2026 插件凭据模板。
- `scripts/build-release.ps1`: 一键构建 2025/2026 Release 并复制到 `dist`。

## 为什么用 C++

Vectorworks 官方 SDK 扩展使用 C++ 开发。官方 Auto-dimension 文档给出的入口也是 `VWParametric_EventSink` 的 C++ 事件函数：

- `OnAutoDimMessage_GetDisplayCategoryName`
- `OnAutoDimMessage_GetLocalizedTypeName`
- `OnAutoDimMessage_GetSupportedTypes`
- `OnAutoDimMessage_GetDimensionDefinitions`

Python/VectorScript 可以做验证，但不适合作为这个插件的正式版本。

## 官方资料

- Vectorworks SDK: https://github.com/Vectorworks/developer-sdk
- Auto-dimension support: https://github.com/Vectorworks/developer-scripting/blob/main/Common/Tasks/Info/Auto-dimension%20support.md
- Vectorworks 2025 development: https://github.com/Vectorworks/developer-sdk/blob/main/Versions/Vectorworks%202025%20Development.md
- Vectorworks 2026 development: https://github.com/Vectorworks/developer-sdk/blob/main/Versions/Vectorworks%202026%20Development.md
- Plugin credentials: https://github.com/Vectorworks/developer-scripting/blob/main/Common/Tasks/Info/PluginCredentials.md
