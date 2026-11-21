我们来写一个 Agent 项目，现代 C++ 风格。

# 项目
- 桌面 AI Agent，界面基于 SDL3 + SDLGui。
- 目标平台：Windows / macOS / Linux；Android / iOS 暂未跑通。
- 许可证 GPL-3.0-only，新增源文件保留 SPDX 头。

# 第三方库
- third_party/SDLGui、third_party/libllmapi、third_party/base64。
- 不要直接修改 third_party 里的代码。需要改进时先汇报，由我反馈给上游。
- md4c、cJSON、libcurl、SDL3_ttf 由系统环境提供。

# 构建与命令
- 优先使用 bash 和 make，例如 `make -j4`。
- pwsh 可以用于普通命令行操作和构建辅助。
- 不要用 pwsh 或任何自动化方式做 GUI 测试：
  启动程序、移动鼠标、点击、截图、发送按键都不要做。
- 需要运行时 / GUI 行为验证时，先询问用户，由用户手动操作。
- 构建失败时看完整错误，不要用 grep 把链接错误吞掉。

# 数据与配置
- config.json 固定在 `%APPDATA%/sdlagent/config.json`。
- 数据目录可配置，存放 sys_prompt.md、tool_prompt.md、title_prompt.md、
  api.json、attachments.json。
- 会话目录可配置：
  - `sessions/<id>.json`：完整会话
  - `sessions/index.json`：侧栏元数据缓存
  - `sessions/attachments/<sessionId>/`：附件副本
- index.json 是缓存，会话 JSON 仍是完整数据源。
- 启动时只读 index 和对账文件名；消息按需加载。
- 内存中只保留当前会话和正在运行的会话。
- api.json 由用户维护，从 models.dev 手动更新，不自动联网下载。

# 附件
- 类型由 attachments.json 的扩展名列表决定，不在列表里的直接拒绝。
- text：按 UTF-8 读取，注入消息。
- image：复制到会话附件目录，以 base64 content parts 发送。
- 限制：文本单个 1 MB、总计 2 MB；图片单张 4 MB、总计 12 MB；最多 8 个。
- 模型是否支持图片由 api.json 的 modalities.input 判断。

# 工具行为
- 用户拒绝工具调用时，整轮立即中断，不再让模型继续。
- 用户停止正在运行的 shell 时，终止进程并中断整轮。
- 中断时，同一条 assistant 消息里未执行的 tool_call 要补上
  “已中断，未执行。”，保证下一轮 tool_call / tool 消息配对完整。
- 文件工具路径不限制，确认是唯一安全边界。
- read_file 支持读取图片：按 attachments.json 的 image 扩展名识别，
  单张 4 MB，单轮最多 9 张。图片以 base64 内联保存，不保留源路径。
- read_file 上限 2000 行 / 50 KiB；shell 输出上限 50 KiB。
- 截断必须明确告知，不要静默丢内容。

# UI
- 中文界面，文案统一放 i18n.cc，中英文都要填。
- 不要圆角、渐变、阴影、动画；颜色克制。
- 按钮等宽；设置菜单项占满宽度。
- 不擅自重做布局或配色，先商量。

# 工作方式
- 有不懂的先商量，尤其是会改数据格式、配置结构、UI 结构的需求。
- 先读相关代码再改，保持现有结构和命名。
- 不覆盖用户已有的改动，不删除运行时数据。
- 一次只做必要改动，完成后说明改了什么、怎么验证。
- 不为了“更完整”擅自扩大需求范围。

# 已知未完成
- PDF / Office / 音频 / 视频附件。
- Android / iOS 适配。
