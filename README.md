# Vectorworks Auto Dimension Plugin

一个独立的 Vectorworks 2025/2026 C++ SDK 自动尺寸标注小插件。

它和 LightA4 没有关系，不依赖外部 TCP 服务，也不需要任何桌面端程序长期保活。尺寸定义由 Vectorworks 插件对象在 Vectorworks 进程内通过官方 Auto-dimension 事件返回。

## 当前状态

- 已建立独立工程：`C:\Users\keepl\Downloads\VectorworksAutoDimensionPlugin`
- 已接入 Vectorworks 2025 SDK 工程并通过 Release 构建。
- 已接入 Vectorworks 2026 SDK 工程并通过 Release 构建。
- 已生成插件产物：
  - `dist/2025/KeeplAutoDimTest.vlb`
  - `dist/2025/KeeplAutoDimTest.vwr`
  - `dist/2026/KeeplAutoDimTest.vlb`
  - `dist/2026/KeeplAutoDimTest.vwr`
- 插件注册了 `KeeplAutoDimTestObj` 参数对象和 `KeeplAutoDimSelectionTool` 普通工具。
- 工具对一个或多个选中的现有对象直接创建 Vectorworks 尺寸对象，不复制源图形。支持符号、灯具、线段、开放/封闭二维对象和三维对象的总体水平/垂直尺寸。
- 工具模式栏提供自动识别、连续、线对象、交线、选择对象、中心点、边界、封闭空间和增强标注、快速连续标注、任意角度链模式；编辑组提供转换、剪齐、对齐、分割/延伸、文字方向、标注点、合并、避让和文字重置操作。模式栏另提供「设置」按钮，可调整标注偏移、文字避让与交线去重参数。
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
- 算法核心 V2 已接入：尺度感知容差、四角度圆统计主方向、凸包旋转包围盒、路径拓扑排序、区间车道、圆/椭圆/圆弧解析交线、正交偏好中心生成树、加权代表边选择和两阶段文字布局。详细设计见 [docs/algorithm-core-v2.md](docs/algorithm-core-v2.md)。
- 标注工具模式栏现提供自动识别、连续标注、线对象、符号定位、交线标注、选择对象、中心点、块边界、封闭空间和增强标注、快速连续标注、任意角度链模式；增强模式对圆弧生成原生半径、直径和弧长尺寸。
- 编辑模式栏现提供转换、剪齐、对齐、分割/延伸、文字方向、标注点、合并、文字避让、重置文字和重置文字位置操作；这些操作只处理选中的原生尺寸对象。
- 详细完成状态和剩余任务见 [TODO.md](TODO.md)。符号插入点定位、尺寸线剪齐和基于碰撞检测的文字避让（选中源图形时同时避让源图形）已经完成代码与构建验证；仍需在 Vectorworks 2025/2026 中完成完整交互回归测试。

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
15. 切到等轴测视图，确认工具退回地平面标注，并在调试日志中记录 `view-plane name=ground-plan`。Windows Debug 构建日志写入 `%TEMP%\vw-autodim-runtime-*.txt` 和 `%TEMP%\vw-load-trace-*.txt`；macOS 写入 `/tmp`。
16. 使用 `Intersections` 交线模式，点击两点，确认生成两点连线与图元交点的线性尺寸；选择圆弧并使用 `Enhanced`，确认生成半径、直径和弧长三类原生尺寸。
17. 选择已生成的尺寸对象切换编辑模式，依次验证文字方向、尺寸点、文字重置、位置重置、对齐和合并，并确认撤销/重做；再选中重叠尺寸与一个源图形，使用「文字避让」，确认文字同时避开源图形。
18. 使用 `QuickChain` 模式，点击首点后逐点续接，确认每段生成水平/垂直投影尺寸，ESC 结束。
19. 使用 `QuickChainAngle` 模式，前两点确定链方向后沿该方向投影续接，确认各段共线、ESC 结束。
20. 使用模式栏「设置」按钮打开对话框，修改偏移基数为 50 后确定，再创建标注确认尺寸线偏移变化；重启 Vectorworks 确认设置仍在（Saved Settings 持久化）。

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

## 自动化测试

所有不依赖 Vectorworks 运行时的门禁可一键运行（CTest、Python 数值回归、双版本源一致性、g++ 编译检查、可选真实插件 MSBuild）：

```powershell
.\scripts\run-tests.ps1            # 全部门禁（含真实 MSBuild）
.\scripts\run-tests.ps1 -SkipBuild # 快速门禁（跳过真实 MSBuild）
```

独立几何层使用 CTest，交线与转换算法另有纯 Python 数值回归，算法核心另加随机属性/差分测试：

```powershell
cmake -S . -B build/test -DBUILD_TESTING=ON
cmake --build build/test --config Release
ctest --test-dir build/test -C Release --output-on-failure
python tools/test_autodim_geometry.py
```

覆盖内容包括：

- CTest：几何定义、命名/退化边界；算法 V2 的尺度容差、主方向、凸包、包围盒、空间哈希、路径拓扑、稳健求交、椭圆/圆弧交线、区间车道、中心生成树、文字布局、集合覆盖和代表边选择。
- 随机属性测试（`tests/AutoDimensionAlgorithmsPropertyTests.cpp`，固定种子可复现）：凸包包含性与凸性、包围盒包含性与面积下界、旋转/平移/缩放变形不变性、主方向旋转等变性、交点落在两条线段上且顺序无关、椭圆方程精确满足、同车道区间互不相交、链边端点连续且无遗漏、集合覆盖必全覆盖、文字布局平移等变性、退化/极端输入不崩溃不产 NaN。
- Python 数值回归：独立线性系统差分对拍、去重后交点集合与暴力参考一致、平移/缩放不变性、退化输入健壮性。

这些测试会在 GitHub Actions 中独立运行，不需要 Vectorworks SDK。完整插件仍需使用上面的双版本 Release 构建验证。

### Windows 本机构建

Windows 构建需要 Vectorworks 的受许可 SDK，不是只安装 Visual Studio 或 Microsoft Windows Kits 就够了。脚本会检查以下三个文件是否存在：

- `SDKLib/Include/OnlyWin/NNA_PluginBuild_RELEASE.props`
- `SDKLib/LibWin/Release/VWSDK.lib`
- `SDKLib/ToolsWin/BuildVWR/buildvwr.exe`

本机未传 `-SdkRoot` 时，脚本会自动查找 `VECTORWORKS_SDK_ROOT`、仓库外的 `VectorworksSDK/<版本>` 目录，并自动选择对应版本。例如本机当前可用目录是：

- `C:\Users\keepl\Downloads\VectorworksSDK\2025\SDK\SDKVW(784374)`
- `C:\Users\keepl\Downloads\VectorworksSDK\2026\SDK\SDKVW(832364)`

仓库中的 `.sdk-stage-20260729*` 目录是 macOS SDK staging，不包含 Windows 的 `LibWin` 和 `ToolsWin`，不能作为 Windows 构建的 `-SdkRoot`。Windows SDK 不提交到仓库。

MSBuild 不在当前 `PATH` 时，脚本会自动查找 Visual Studio 2022 Build Tools、Community 安装目录和 `vswhere.exe`。也可以显式传入 `-MsBuildPath`。

### macOS / GitHub Actions

Mac 版本使用 Xcode 工程构建，产物为 `KeeplAutoDimTest.vwlibrary`。Vectorworks SDK 受许可保护，不提交到仓库；本机执行时把 SDK 的 `SDKLib` 目录传给脚本：

```bash
./scripts/build-release-mac.sh --sdk-root "/path/to/SDKLib"
```

也可以把 SDK 压缩包放在私有下载地址，然后在 GitHub 仓库的 Actions secrets 配置：

- `CLOUDFLARE_R2_ENDPOINT`、`CLOUDFLARE_R2_BUCKET`：私有 R2 SDK bucket 的 S3 endpoint 与 bucket 名称
- `VECTORWORKS_SDK_R2_ACCESS_KEY_ID`、`VECTORWORKS_SDK_R2_SECRET_ACCESS_KEY`：只读 SDK 对象的 R2 凭据
- `VECTORWORKS_WIN_SDK_2025_SHA256` / `VECTORWORKS_WIN_SDK_2026_SHA256`：Windows SDK ZIP 的 SHA-256
- `VECTORWORKS_MAC_SDK_2025_SHA256` / `VECTORWORKS_MAC_SDK_2026_SHA256`：对应 ZIP 的 SHA-256

推送和拉取请求只构建并上传 GitHub artifact，绝不会发布到网站。发布必须在 `main` 分支手动运行 Windows 或 Mac 工作流，并将 `publish` 设为 `true`。两个工作流的发布 job 都绑定 GitHub `production` Environment；应在该 Environment 配置审批规则，并配置 `CLOUDFLARE_R2_ENDPOINT`、`CLOUDFLARE_R2_BUCKET`、`CLOUDFLARE_R2_ACCESS_KEY_ID` 和 `CLOUDFLARE_R2_SECRET_ACCESS_KEY`。获批后工作流才会把 ZIP 上传到 R2 的 `vectorworks/` 目录。

GitHub Actions 使用 `macos-14`，需要在 SDK ZIP 中保留完整的 `SDKLib/Include`、`SDKLib/LibMac` 和 `SDKLib/ToolsMac/BuildVWR/BuildVWR`。

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

- `%APPDATA%\Nemetschek\Vectorworks\2025\Plug-ins\KeeplAutoDimTest.vlb`
- `%APPDATA%\Nemetschek\Vectorworks\2025\Plug-ins\KeeplAutoDimTest.vwr`
- `%APPDATA%\Nemetschek\Vectorworks\2026\Plug-ins\KeeplAutoDimTest.vlb`
- `%APPDATA%\Nemetschek\Vectorworks\2026\Plug-ins\KeeplAutoDimTest.vwr`

安装时会同时复制 `.vlb` 和 `.vwr`。旧的 `AutoDimensionPlugin.vlb/.vwr` 会移动到同版本目录下的 `KeeplAutoDimTest-quarantine`，可手动恢复；构建时发现的旧发布文件保留在仓库中，但会被临时白名单打包流程拒绝，绝不会进入 ZIP。

## 项目结构

- `sdk-projects/2025/AutoDimensionPlugin`: Vectorworks 2025 SDK 插件工程。
- `sdk-projects/2026/AutoDimensionPlugin`: Vectorworks 2026 SDK 插件工程。
- `src/AutoDimensionGeometry.cpp`: 脱离 SDK 的尺寸定义计算原型。
- `include/vwad/AutoDimensionGeometry.h`: 脱离 SDK 的几何/尺寸数据结构。
- `include/vwad/AutoDimensionAlgorithms.h`: V2 无 SDK 算法核心；包含稳健几何、方向/包围盒、拓扑链、解析求交、车道、中心图、布局和集合覆盖。
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
