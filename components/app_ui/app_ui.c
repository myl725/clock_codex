#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_speech_service.h"
#include "lvgl.h"

static lv_obj_t *s_state_label;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_text_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_level_bar;
static lv_obj_t *s_level_value_label;

static const char *app_ui_state_to_text(app_speech_state_t state)
{
    switch (state) {
        case APP_SPEECH_STATE_BOOTING:
            return "状态：启动中";
        case APP_SPEECH_STATE_LISTENING:
            return "状态：等待唤醒";
        case APP_SPEECH_STATE_AWAKE:
            return "状态：正在听命令";
        case APP_SPEECH_STATE_RECOGNIZED:
            return "状态：识别成功";
        case APP_SPEECH_STATE_TIMEOUT:
            return "状态：识别超时";
        case APP_SPEECH_STATE_ERROR:
            return "状态：识别异常";
        default:
            return "状态：未知";
    }
}

static void app_ui_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char detail_text[128];

    if (s_state_label == NULL || s_hint_label == NULL || s_text_label == NULL ||
        s_detail_label == NULL || s_level_bar == NULL || s_level_value_label == NULL) {
        return;
    }

    app_speech_snapshot_t snapshot;
    app_speech_service_get_snapshot(&snapshot);

    lv_label_set_text(s_state_label, app_ui_state_to_text(snapshot.state));
    lv_label_set_text_fmt(s_hint_label, "唤醒词：%s", snapshot.wake_hint[0] ? snapshot.wake_hint : "未配置");
    lv_label_set_text_fmt(s_text_label, "识别结果：%s", snapshot.last_text[0] ? snapshot.last_text : "暂无");
    if (snapshot.probability > 0.0f) {
        snprintf(
            detail_text,
            sizeof(detail_text),
            "详情：%s | 置信度 %.2f",
            snapshot.detail[0] ? snapshot.detail : "等待中",
            snapshot.probability
        );
    } else {
        snprintf(
            detail_text,
            sizeof(detail_text),
            "详情：%s",
            snapshot.detail[0] ? snapshot.detail : "等待中"
        );
    }
    lv_label_set_text(s_detail_label, detail_text);
    lv_bar_set_value(s_level_bar, snapshot.level_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_level_value_label, "输入电平：%u%%", snapshot.level_percent);
}

void app_ui_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0E1A24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "INMP441 Voice Demo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF6BD60), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "说“嗨乐鑫”后再说命令词");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xF7EDE2), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 42);

    s_state_label = lv_label_create(screen);
    lv_label_set_text(s_state_label, "状态：启动中");
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x84A59D), LV_PART_MAIN);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_LEFT, 18, 78);

    s_hint_label = lv_label_create(screen);
    lv_label_set_text(s_hint_label, "唤醒词：嗨乐鑫");
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x8ECAE6), LV_PART_MAIN);
    lv_obj_align(s_hint_label, LV_ALIGN_TOP_LEFT, 18, 102);

    s_text_label = lv_label_create(screen);
    lv_label_set_text(s_text_label, "识别结果：暂无");
    lv_obj_set_style_text_color(s_text_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_width(s_text_label, 284);
    lv_label_set_long_mode(s_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_text_label, LV_ALIGN_TOP_LEFT, 18, 132);

    s_detail_label = lv_label_create(screen);
    lv_label_set_text(s_detail_label, "详情：等待中");
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(0xB8C0C8), LV_PART_MAIN);
    lv_obj_set_width(s_detail_label, 284);
    lv_label_set_long_mode(s_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_detail_label, LV_ALIGN_TOP_LEFT, 18, 182);

    s_level_value_label = lv_label_create(screen);
    lv_label_set_text(s_level_value_label, "输入电平：0%");
    lv_obj_set_style_text_color(s_level_value_label, lv_color_hex(0xF28482), LV_PART_MAIN);
    lv_obj_align(s_level_value_label, LV_ALIGN_BOTTOM_LEFT, 18, -44);

    s_level_bar = lv_bar_create(screen);
    lv_obj_set_size(s_level_bar, 284, 14);
    lv_obj_align(s_level_bar, LV_ALIGN_BOTTOM_LEFT, 18, -20);
    lv_bar_set_range(s_level_bar, 0, 100);
    lv_obj_set_style_bg_color(s_level_bar, lv_color_hex(0x24323E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_level_bar, lv_color_hex(0xF28482), LV_PART_INDICATOR);

    lv_timer_create(app_ui_refresh_timer_cb, 120, NULL);
}
