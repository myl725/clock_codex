# clock_codex 版本管理规则

## 目的

本文定义了 `clock_codex` 应如何借助版本控制持续演进。
目标是让硬件 bring-up、驱动迁移、UI 修改以及语音实验都具备可追踪、可回退、可在另一台机器上继续推进的能力。

## 默认工具

本项目默认使用 `git` 作为版本管理系统。

将 `clock_codex` 视为未来所有工作的主仓库。
将 `weather_clock_esp_idf` 视为参考项目，而不是当前迭代的活动目标。

## 仓库范围

- 在 `clock_codex` 中创建并维护独立的 git 仓库。
- 不要把新仓库的历史与旧参考项目混在一起，除非出于明确的归档目的。
- 将项目文档、技能和受跟踪的配置文件保留在仓库中，以便其他机器能够继续工作。

## 必须纳入跟踪的内容

在 git 中跟踪以下类型的文件：

- `src/` 和 `components/` 中的源代码
- `docs/` 中的项目文档
- `.codex/skills/` 中的本地项目技能
- 受跟踪的构建配置，例如：
  - `platformio.ini`
  - `sdkconfig.defaults`
  - `partitions.csv`
  - `idf_component.yml`
  - `tools/` 下的辅助脚本

## 不应纳入跟踪的内容

不要提交生成物或机器本地产物，例如：

- `.pio/`
- `build/`
- 生成的 `sdkconfig.*` 快照，除非有意将其作为参考保留
- 串口监视日志
- 临时测试录音或本地 dump 文件，除非它们被明确整理为固定测试样本

如果某个生成文件必须为了诊断而保留，请将其放在一个命名清晰的临时目录中，并在问题解决后移除。

## 提交粒度

每个可验证里程碑对应一个提交。

一个好的提交通常意味着以下之一：

- 一个模块完成迁移并通过验证
- 一个驱动 bug 被修复并测试
- 一组项目规则或文档被加入
- 一个传输层或服务功能被集成到可用检查点

如果会让回滚或 blame 更困难，就不要把无关工作混在同一个提交里。

## 提交时机

当修改达到稳定检查点后再创建提交。

稳定检查点示例：

- `LVGL` 可以编译并启动
- 显示屏能显示有效画面
- 触摸事件能进入 LVGL
- 语音模型打包可用
- WebSocket 传输已连通并返回文本

避免提交那些“迁移到一半、导致无法启动”的代码；除非提交信息明确标注它是实验分支检查点。

## 提交信息风格

使用简短、面向能力变化的提交信息。

推荐前缀：

- `feat:` 新功能或模块里程碑
- `fix:` bug 修复或回归修复
- `refactor:` 结构清理，不预期改行为
- `docs:` 文档更新
- `build:` 工具链、分区或构建系统改动
- `test:` 测试或验证辅助

示例：

- `feat: integrate lvgl runtime scaffold`
- `feat: migrate ili9341 display driver`
- `feat: add xpt2046 lvgl input driver`
- `feat: add inmp441 speech demo skeleton`
- `fix: clamp touch coordinates before lvgl dispatch`
- `build: add custom upload flow based on flash_args`
- `docs: record current speech integration status`

## 分支规则

保持简单的分支模型。

- `main`
  - 始终表示最新、相对稳定的主线
  - 应当能够编译，理想情况下也应能启动
- `feature/<name>`
  - 用于较大的实验或可能让项目暂时不稳定的里程碑

推荐的 feature 分支命名：

- `feature/inmp441-input`
- `feature/speech-websocket`
- `feature/xiaozhi-style-transport`
- `feature/app-ui-migration`

如果改动很小且风险低，可以直接进入 `main`。
如果改动涉及多个子系统或行为不确定，优先使用 feature 分支。

## Tag 规则

给重要且稳定的里程碑打 tag，方便后续恢复。

建议使用轻量里程碑标签，例如：

- `v0.1-lvgl-base`
- `v0.2-display-touch`
- `v0.3-speech-skeleton`
- `v0.4-websocket-loopback`

只有在对应状态已知可复现时才创建 tag。

## 硬件 bring-up 规则

对于硬件相关改动，优先采用以下顺序：

1. 修改代码
2. 成功编译
3. 成功上传
4. 验证真实硬件行为
5. 然后再提交

如果因为没有硬件而暂时无法完成第 4 步，提交信息或任务记录中应明确写出这一点。

## 跨机器连续性

为了可靠地在另一台机器上继续工作：

- 提交最新稳定状态
- 条件允许时推送到远程仓库
- 持续更新 `docs/current-status.md`，记录那些不明显的阻塞点

不要把聊天记录当成唯一的项目上下文来源。

## 文档更新规则

在以下情况中，应与代码里程碑同步更新文档：

- 架构决策发生变化
- 硬件引脚假设发生变化
- 构建或上传绕过方案发生变化
- 发现新的重大阻塞
- 出现新的首选集成方向

至少要确保以下文件与实际情况一致：

- `docs/current-status.md`
- `docs/architecture.md`
- `docs/versioning.md`

## 恢复规则

如果某次实验走偏了：

- 使用 git 历史回到上一个稳定里程碑
- 不要靠记忆手工重建已知良好状态

如果某个 feature 分支变得过于混乱：

- 从最近的稳定提交重新拉出一个新分支
- 只重新应用那些仍然有效的部分

## 最小工作流

本项目默认工作流应为：

1. 在 `clock_codex` 中修改代码
2. 编译
3. 涉及硬件时上传并验证
4. 如果项目状态变化，更新文档
5. 使用聚焦的提交信息提交
6. 如果该检查点是里程碑，则打 tag
7. 如果跨机器连续性重要，则推送
