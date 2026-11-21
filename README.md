![SDL Agent](assets/icon/icon_bar.png)

# SDL Agent

一个基于 SDL3 / SDLGui / libllmapi 的本地 AI Agent 客户端，支持多会话、流式回复、Markdown 渲染、工具调用和多套 API 配置。

## 依赖

- SDL3、SDL3_ttf
- SDLGui（third_party/SDLGui）
- libllmapi（third_party/libllmapi）
- md4c
- cJSON
- libcurl
- nlohmann-json 不再使用；JSON 由内置的 cJSON RAII 包装处理

## 构建

```sh
make          # 构建
make run      # 构建并运行
make clean
```

需要 C++20 编译器、`make` 和 `pkg-config`。

## 配置文件

默认位于 SDL 的 pref 目录：

- Windows: `%APPDATA%\sdlagent\`
- macOS: `~/Library/Application Support/sdlagent/`
- Linux: `$XDG_DATA_HOME/sdlagent/`（默认 `~/.local/share/sdlagent/`）

包含 `config.json`、`sys_prompt.md`、`tool_prompt.md`、`title_prompt.md`、
`api.json` 和 `sessions/`。首次启动会自动生成基础默认配置。

默认提示词的源文件在 `assets/prompts/`：

- `sys.md` → `sys_prompt.md`
- `tool.md` → `tool_prompt.md`
- `title.md` → `title_prompt.md`

构建时会把它们嵌入可执行文件，因此发布仍然只需要一个 exe。修改这些
Markdown 后重新 `make` 即可；用户实际编辑的仍然是配置目录里的副本。

`config.json` 始终保存在默认目录；`sys_prompt.md`、`tool_prompt.md` 和
`api.json` 所在的“配置目录”可以在 设置 → 定制设置 中单独修改。修改时会
把已有文件复制到新目录，目标目录已存在的文件不会被覆盖。

## 模型目录

把 [models.dev](https://models.dev) 的 `api.json` 放到配置目录，即可在
设置 → 配置 → “从 api.json 选择…” 中按供应商浏览模型，并自动填入：

- 模型 ID
- 上下文 tokens（`limit.context`）
- 最大输出 tokens（`limit.output`）
- API 地址（供应商提供 `api` 字段时）

没有 `api` 字段的供应商会留空，需要手动填写 API 地址。程序不会自动下载
`api.json`，由用户自行更新；选择框里会显示当前读取的文件路径，并可重新加载。

## 自动标题

首轮对话完成后，程序会用当前 API 配置额外发起一次很小的非流式请求，
根据 `title_prompt.md` 生成会话标题。该提示词可以在 定制设置 中打开编辑。

- 默认开启，可在 定制设置 → 自动生成会话标题 中关闭
- 只生成一次；手动重命名优先，不会被覆盖
- 失败时保留原来的首行标题，不弹错误
- 关闭后标题仍由第一条消息的第一行本地生成

## 移动端

Android / iOS 上路径属于应用沙箱，设置中不显示“定制设置”页：

- 不提供工作区、会话、配置目录和提示词路径设置
- 启动时忽略 `config.json` 里来自桌面端的自定义路径，统一使用默认目录
- 仍可修改 API 配置、主题和字体

## 许可证

本程序以 **GPL-3.0-only** 协议开源，完整文本见 [LICENSE](LICENSE)。

`third_party/` 下的 SDLGui 与 libllmapi 保留各自的原许可证（BSD 2-Clause）。SDL3、SDL3_ttf、md4c、cJSON、libcurl 等依赖遵循其各自许可证。
