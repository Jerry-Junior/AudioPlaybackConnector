# AudioPlaybackConnector
[English](README.md) | **简体中文**

Windows 10 2004+ 蓝牙音频接收 (A2DP Sink) 连接工具。

微软在 Windows 10 2004 加入了蓝牙 A2DP Sink 支持，但是仍需要第三方软件来管理连接。\
已经有一些应用可以实现这个功能，但它们不能隐藏到通知区域，而且不开源。\
所以我写了这个应用：一个简单、现代且开源的替代品。

# 预览
![预览](https://cdn.jsdelivr.net/gh/ysc3839/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

# 使用方法
* 从 [releases](https://github.com/Jerry-Junior/AudioPlaybackConnector/releases) 下载并运行 AudioPlaybackConnector。
* 在系统蓝牙设置中配对蓝牙设备。你可以右键点击通知区域的 AudioPlaybackConnector 图标，选择“蓝牙设置”。
* 点击 AudioPlaybackConnector 图标，选择想要连接的设备。
* 尽情享受吧！

# 系统要求
* Windows 10 2004（10.0.19041）或更高版本。
* 蓝牙设备需先在 Windows 蓝牙设置中完成配对。
* 提供 x86、x64、ARM 和 ARM64 构建。

# 配置文件
配置保存在可执行文件同目录的 `AudioPlaybackConnector.json`：

| 键 | 类型 | 说明 |
| --- | --- | --- |
| `reconnect` | 布尔 | 下次启动时重连 `lastDevices` 中的设备。 |
| `lastDevices` | 字符串数组 | 退出时仍处于连接状态的设备 Id。 |

* 文件在应用退出时写入。
* 可执行文件所在目录必须可写，否则配置会静默保存失败（应用仍可正常使用）。
* 删除该文件即恢复默认值（`reconnect: false`，无已保存设备）。

# 单实例
每个登录会话只运行一个实例。再次启动会激活正在运行的实例并打开其设备选择器，而不是创建第二个托盘图标。不同用户会话（包括远程桌面会话）各自允许一个实例。

# 构建
依赖：

* Visual Studio 2022，安装“使用 C++ 的桌面开发”工作负载。
* Windows 10 SDK 10.0.19041 或更高版本。
* NuGet（`nuget.exe` 或 Visual Studio 自带的 NuGet 支持）。

```powershell
nuget restore AudioPlaybackConnector.sln
msbuild AudioPlaybackConnector.sln /p:Configuration=Release /p:Platform=x64
```

`Platform` 也可以取 `x86`、`ARM`、`ARM64`；ARM/ARM64 还需要安装对应的 MSVC ARM 生成工具组件。产物位于 `\<Platform>\Release\AudioPlaybackConnector<架构>.exe`（x64 为 `AudioPlaybackConnector64.exe`，x86 为 `AudioPlaybackConnector32.exe`）。

# 已知限制
* 界面文案仅英文。此前基于 gettext 的翻译管线从未接入运行时，现已移除。
* 可执行文件未签名，从网络下载后 Windows SmartScreen 可能给出警告。
* A2DP Sink 能力由 Windows 提供，本应用只负责管理连接，不实现音频流或编解码。

# 反馈
请提供 Windows 版本与内部版本号、架构（x86/x64/ARM/ARM64）以及复现步骤。WIL 诊断信息可通过 [DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview) 查看，对定位问题很有帮助。

# 许可证
MIT，见 [LICENSE](LICENSE)。
