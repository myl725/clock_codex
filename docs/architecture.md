# clock_codex 架构规则

## 目的

本文定义了 `clock_codex` 这个 ESP-IDF 项目的稳定架构边界。
在新增模块、从 `weather_clock_esp_idf` 迁移旧代码，或评审某个改动是否位于正确层级时，应将本文作为事实依据。

## 分层模型

项目围绕六类职责组织：

- `src/`
  仅负责启动流程编排。保持 `app_main()` 足够轻量，只承载初始化顺序、任务引导与顶层生命周期控制。
- `components/bsp`
  负责板级专用配置与底层共享硬件定义，例如 GPIO 映射、SPI host 选择、显示尺寸与默认触摸校准值。
- `components/display`
  负责显示驱动接入、LVGL 显示注册、frame flush 与显示总线细节。
- `components/touch`
  负责触摸控制器接入、坐标转换、校准处理与 LVGL indev 注册。
- `components/app_services`
  负责面向服务的 API，对 UI 暴露能力，并在未来协调 Wi-Fi、SNTP、天气、DHT11、WS2812、SD 与 MP3 模块。
- `components/app_ui`
  负责 LVGL 页面、资源、事件回调与 UI 状态更新。

## 依赖方向

只允许以下依赖方向：

- `src` -> `bsp`、`display`、`touch`、`app_services`、`app_ui`
- `app_ui` -> `app_services`
- `app_services` -> `bsp`、面向硬件的驱动模块、ESP-IDF 服务
- `display` -> `bsp`、LVGL、ESP-IDF 驱动
- `touch` -> `bsp`、LVGL、ESP-IDF 驱动

禁止以下反向依赖：

- `app_services` -> `app_ui`
- `display` -> `app_ui`
- `touch` -> `app_ui`
- 硬件 / 驱动模块 -> `app_ui`

## 所有权规则

每项关注点都应归属于一个清晰的层：

- 启动顺序、任务创建策略与全局初始化序列：`src`
- 板级引脚、总线标识、校准默认值、硬件常量：`bsp`
- 显示 flush 与显示总线事务：`display`
- 触摸采样与坐标映射：`touch`
- 外部能力与模块编排：`app_services`
- 页面控件、转场、标签、与页面表现绑定的定时器：`app_ui`

如果一项改动跨越多个层，请拆分它，使每一层都保持各自职责。

## 初始化规则

在第一阶段，保持初始化顺序固定：

1. `nvs_flash_init()`
2. `lv_init()`
3. `bsp_display_init()` 或 `display_init()` 封装
4. `bsp_touch_init()` 或 `touch_init()` 封装
5. `app_ui_init()`
6. 创建 `lvgl_task`

除非某个里程碑明确要求加入，否则在第一阶段不要启动 Wi-Fi、DHT11、WS2812、SD 或音频任务。

## UI 与 Service 边界

将 UI 视为 services 的客户端：

- UI 可以通过 `app_services` 请求时间、天气、温度、音乐控制或 LED 更新。
- UI 不得直接访问 SPI、I2S、RMT、GPIO 或原始 ESP-IDF 驱动对象。
- UI 不得读写驱动层持有的全局变量，例如原始队列、总线句柄或传感器结构体。

在早期迁移阶段，相比直接耦合，更推荐使用 service stub。

## 配置边界

按意图拆分配置：

- `board_config.h`
  板级引脚、SPI host 选择、显示几何参数、校准默认值。
- `app_config.h`
  项目级应用配置，例如 Wi-Fi 凭据、天气位置、API Key、功能开关。
- `sdkconfig.defaults`
  ESP-IDF 能力开关与框架级选项。

除非是合并前会移除的短期调试实验，否则不要把这些值硬编码到功能实现 `.c` 文件中。

## 迁移落位指南

从旧项目迁移代码时，应按行为落位，而不是按旧文件夹名照搬：

- `lv_port_disp*` -> `components/display`
- `touch_indev*` -> `components/touch`
- SquareLine / LVGL 页面、字体、图片 -> `components/app_ui`
- Wi-Fi、SNTP、天气、DHT11、WS2812、SD / MP3 控制外观 API -> `components/app_services`
- 板级常量与共享硬件映射 -> `components/bsp`

如果旧的相对 include 结构与这些所有权规则冲突，不要保留它。
