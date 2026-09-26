# 安全审查与验证记录

## 范围与设计

- 目标是普通用户权限运行的 Windows 10 x64 小程序。清单指定 `asInvoker` 与 `uiAccess=false`。
- F8 或 F9 使用 `RegisterHotKey`，仅在专注阶段注册；窗口内空格仅处理该窗口消息。没有键盘钩子、扫描按键或记录其他软件的键入内容。
- 没有网络调用、自动更新、遥测、开机自启、驱动、浏览器组件或第三方音频文件。声音通过 Windows 系统音频接口播放；一个约 0.55 MiB 的缓冲区重复使用。
- 只在当前用户的 `%LOCALAPPDATA%\TinyLoop\` 读写设置和事件记录。输入时长与轮数限制范围；读取异常记录时跳过超长行和越界字段。

## 已执行

- Linux GCC `-Wall -Wextra -Werror` 编译核心测试，通过 30 轮边界、休息、暂停、提前完成、撤销、336 组三音排列及四段不同轻音乐的波形检查。
- 核心测试在 AddressSanitizer 与 UndefinedBehaviorSanitizer 下通过（此环境 LeakSanitizer 无法访问进程任务目录，已禁用 LeakSanitizer）。
- Windows x64 交叉编译 `-Wall -Wextra -Werror -fstack-protector-strong` 成功；GCC `-fanalyzer` 扫描没有警告。
- 检查 PE 导入表：仅 ADVAPI32、GDI32、KERNEL32、msvcrt、SHELL32、USER32、WINMM，均为 Windows 系统组件；无网络运行库或第三方 DLL 导入。

## 尚需 Windows 10 实机确认

- 图形布局、高 DPI、托盘、全局 F8/F9 注册冲突、真实声卡输出、睡眠/唤醒时序、持续运行工作集和空闲 CPU。
- Windows Defender 对最终 `.exe` 的本机扫描。当前执行环境无法建立 Wine 的进程通信套接字，未能运行 Windows 图形程序；不能把交叉编译或静态分析说成完整的 Win10 实机验收。
- 未使用商业代码签名证书；Windows 对新下载的未签名程序可能显示信誉提示。

## 发布检查

最终交付的 SHA-256 见同包 `SHA256.txt`。安全审查确认源码中没有联网、管理员提权、键盘钩子和自启实现；未知的系统配置与真实声卡行为仍以实机验收为准。
