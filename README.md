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
- 工具对选中的现有对象直接创建 Vectorworks 尺寸对象，不复制源图形。支持符号、灯具、线段、开放/封闭二维对象和三维对象的总体水平/垂直尺寸。
- 斜线会额外按真实端点生成斜向真长度和相对水平线的角度尺寸。例如水平投影约 `36`、垂直投影 `62` 的线段，斜长约为 `71.6`，角度约为 `60°`。
- 参数对象仍保留官方 Auto-dimension 回调，用于验证 Graphic Legend 对参数化对象的支持；普通对象的主流程是直接创建尺寸。

## Vectorworks 验证

官方 Auto-dimension 回调只能由参数化对象实现，不能直接注入普通 Vectorworks 对象。因此普通对象使用 SDK 的 `CreateLinearDimension` 直接生成尺寸对象。

当前版本需要在 Vectorworks 里验证：

1. 安装插件到 Vectorworks 用户插件目录。
2. 重启 Vectorworks。
3. 在工作空间里添加或调用 `Keepl Auto Dimension`。
4. 推荐先选中一个符号、灯具、线段、二维对象或三维对象，再激活工具；插件会立即生成尺寸。没有预选对象时，也可以在绘图区点击对象。
5. 确认源对象没有被复制，只新增尺寸对象。
6. 对斜线确认同时生成水平投影、垂直投影、斜向真长度和角度，共四个原生尺寸对象。
7. 确认尺寸文字、箭头、撤销和重做正常。
8. 分别确认顶、前、侧视图的尺寸轴向正确。

三维深度尺寸、多段线节点逐段尺寸、旋转符号局部轴尺寸和灯具专用尺寸规则将在当前尺寸创建流程验证通过后继续添加。

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
