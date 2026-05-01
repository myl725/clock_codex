#include "app_ui.h"

#include "app_led_service.h"
#include "app_music_service.h"
#include "lvgl.h"

static lv_obj_t *s_led_status_label;
static lv_obj_t *s_led_preview_card;
static lv_obj_t *s_led_note_label;
static lv_obj_t *s_led_effect_dropdown;
static lv_obj_t *s_led_color_dropdown;
static lv_obj_t *s_music_status_label;
static lv_obj_t *s_music_note_label;
static lv_obj_t *s_music_source_dropdown;
static lv_obj_t *s_music_tone_dropdown;
static lv_obj_t *s_music_option_label;
static lv_obj_t *s_music_toggle_btn;
static lv_obj_t *s_music_toggle_btn_label;
static lv_timer_t *s_music_refresh_timer;
static app_music_source_t s_last_music_source = APP_MUSIC_SOURCE_COUNT;
static uint8_t s_last_wav_count = 0xFF;

static lv_color_t app_ui_make_lv_color(app_led_color_t color)
{
    app_led_rgb_t rgb = app_led_service_get_color_rgb(color);
    return lv_color_make(rgb.red, rgb.green, rgb.blue);
}

static void app_ui_refresh_led_state(void)
{
    if (s_led_status_label == NULL || s_led_preview_card == NULL || s_led_note_label == NULL ||
        s_led_effect_dropdown == NULL || s_led_color_dropdown == NULL) {
        return;
    }

    app_led_snapshot_t snapshot;
    app_led_service_get_snapshot(&snapshot);

    lv_dropdown_set_selected(s_led_effect_dropdown, snapshot.effect);
    lv_dropdown_set_selected(s_led_color_dropdown, snapshot.color);
    lv_label_set_text_fmt(
        s_led_status_label,
        "Effect: %s\nColor: %s",
        app_led_service_effect_to_text(snapshot.effect),
        app_led_service_color_to_text(snapshot.color)
    );
    lv_obj_set_style_bg_color(s_led_preview_card, app_ui_make_lv_color(snapshot.color), LV_PART_MAIN);

    if (snapshot.effect == APP_LED_EFFECT_RAINBOW) {
        lv_label_set_text(s_led_note_label, "Rainbow ignores the base color and cycles automatically.");
    } else {
        lv_label_set_text(s_led_note_label, "Touch the dropdowns to switch the effect and color live.");
    }
}

static void app_ui_effect_changed_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    app_led_service_set_effect((app_led_effect_t)lv_dropdown_get_selected(target));
    app_ui_refresh_led_state();
}

static void app_ui_color_changed_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    app_led_service_set_color((app_led_color_t)lv_dropdown_get_selected(target));
    app_ui_refresh_led_state();
}

static void app_ui_refresh_music_state(void)
{
    char wav_options[APP_MUSIC_WAV_OPTION_TEXT_LENGTH];
    uint8_t selected_wav_index = 0;
    uint8_t wav_count = 0;

    if (s_music_status_label == NULL || s_music_note_label == NULL ||
        s_music_tone_dropdown == NULL || s_music_toggle_btn_label == NULL ||
        s_music_source_dropdown == NULL || s_music_option_label == NULL) {
        return;
    }

    app_music_snapshot_t snapshot;
    app_music_diag_info_t diag;
    app_music_service_get_snapshot(&snapshot);
    app_music_service_get_diagnostics(&diag);

    lv_dropdown_set_selected(s_music_source_dropdown, snapshot.source);
    if (snapshot.source == APP_MUSIC_SOURCE_TONE) {
        if (s_last_music_source != APP_MUSIC_SOURCE_TONE) {
            lv_label_set_text(s_music_option_label, "Tone");
            lv_dropdown_set_options(s_music_tone_dropdown, "A4 440Hz\nC5 523Hz\nE5 659Hz");
        }
        lv_dropdown_set_selected(s_music_tone_dropdown, snapshot.tone);
        lv_obj_clear_state(s_music_tone_dropdown, LV_STATE_DISABLED);
    } else {
        app_music_service_get_wav_dropdown_options(
            wav_options,
            sizeof(wav_options),
            &selected_wav_index,
            &wav_count
        );
        if (s_last_music_source != APP_MUSIC_SOURCE_SD_WAV || s_last_wav_count != wav_count) {
            lv_label_set_text(s_music_option_label, "File");
            lv_dropdown_set_options(s_music_tone_dropdown, wav_options);
        }
        lv_dropdown_set_selected(s_music_tone_dropdown, selected_wav_index);
        if (wav_count == 0U) {
            lv_obj_add_state(s_music_tone_dropdown, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_music_tone_dropdown, LV_STATE_DISABLED);
        }
    }
    s_last_music_source = snapshot.source;
    s_last_wav_count = wav_count;
    if (snapshot.source == APP_MUSIC_SOURCE_TONE) {
        lv_label_set_text_fmt(
            s_music_status_label,
            "State: %s\nSource: %s\nTone: %s\nRate: %lu Hz\nStage: %s",
            app_music_service_state_to_text(snapshot.state),
            app_music_service_source_to_text(snapshot.source),
            app_music_service_tone_to_text(snapshot.tone),
            (unsigned long)snapshot.sample_rate_hz,
            app_music_service_diag_stage_to_text(diag.current_stage)
        );
    } else {
        lv_label_set_text_fmt(
            s_music_status_label,
            "State: %s\nSource: %s\nWAV: %s\nRate: %lu Hz\nStage: %s",
            app_music_service_state_to_text(snapshot.state),
            app_music_service_source_to_text(snapshot.source),
            snapshot.wav_path[0] == '\0' ? "-" : snapshot.wav_path,
            (unsigned long)snapshot.sample_rate_hz,
            app_music_service_diag_stage_to_text(diag.current_stage)
        );
    }
    lv_label_set_text(
        s_music_toggle_btn_label,
        snapshot.state == APP_MUSIC_STATE_PLAYING
            ? "Stop"
            : (snapshot.source == APP_MUSIC_SOURCE_TONE ? "Play Tone" : "Play WAV")
    );
    if (snapshot.source == APP_MUSIC_SOURCE_TONE) {
        lv_label_set_text_fmt(
            s_music_note_label,
            "Status: %s\nwrites=%lu marks=%lu current=%s fail=%s err=%s",
            snapshot.status_text,
            (unsigned long)diag.successful_writes,
            (unsigned long)diag.mark_count,
            app_music_service_diag_stage_to_text(diag.current_stage),
            app_music_service_diag_stage_to_text(diag.last_failure_stage),
            esp_err_to_name(diag.last_failure_error)
        );
    } else {
        lv_label_set_text_fmt(
            s_music_note_label,
            "Status: %s\nFile: %s\nwrites=%lu current=%s err=%s",
            snapshot.status_text,
            snapshot.wav_path,
            (unsigned long)diag.successful_writes,
            app_music_service_diag_stage_to_text(diag.current_stage),
            esp_err_to_name(diag.last_failure_error)
        );
    }
}

static void app_ui_music_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    app_ui_refresh_music_state();
}

static void app_ui_music_tone_changed_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    app_music_snapshot_t snapshot;
    app_music_service_get_snapshot(&snapshot);
    if (snapshot.source == APP_MUSIC_SOURCE_TONE) {
        app_music_service_set_tone((app_music_tone_t)lv_dropdown_get_selected(target));
    } else {
        app_music_service_select_wav_file((uint8_t)lv_dropdown_get_selected(target));
    }
    app_ui_refresh_music_state();
}

static void app_ui_music_source_changed_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    app_music_service_set_source((app_music_source_t)lv_dropdown_get_selected(target));
    app_ui_refresh_music_state();
}

static void app_ui_music_toggle_cb(lv_event_t *event)
{
    (void)event;
    app_music_service_toggle_playback();
    app_ui_refresh_music_state();
}

void app_ui_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Clock Codex Control Lab");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7B267), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "WS2812 lighting plus MAX98357A SD audio bring-up");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xD7E3F4), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *tabview = lv_tabview_create(screen, LV_DIR_TOP, 32);
    lv_obj_set_size(tabview, 320, 170);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x0D1B2A), LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);

    lv_obj_t *led_tab = lv_tabview_add_tab(tabview, "Lighting");
    lv_obj_t *music_tab = lv_tabview_add_tab(tabview, "Audio");

    lv_obj_set_style_bg_color(led_tab, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(led_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_tab, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(music_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scroll_dir(music_tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(music_tab, LV_SCROLLBAR_MODE_ACTIVE);

    lv_obj_t *control_panel = lv_obj_create(led_tab);
    lv_obj_set_size(control_panel, 274, 110);
    lv_obj_align(control_panel, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(control_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(control_panel, lv_color_hex(0x122235), LV_PART_MAIN);
    lv_obj_set_style_border_width(control_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(control_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(control_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *effect_label = lv_label_create(control_panel);
    lv_label_set_text(effect_label, "Effect");
    lv_obj_set_style_text_color(effect_label, lv_color_hex(0xB5C7D3), LV_PART_MAIN);
    lv_obj_align(effect_label, LV_ALIGN_TOP_LEFT, 16, 14);

    s_led_effect_dropdown = lv_dropdown_create(control_panel);
    lv_dropdown_set_options(s_led_effect_dropdown, "Off\nSolid\nBlink\nBreathe\nRainbow");
    lv_obj_set_width(s_led_effect_dropdown, 112);
    lv_obj_align(s_led_effect_dropdown, LV_ALIGN_TOP_LEFT, 16, 38);
    lv_obj_add_event_cb(s_led_effect_dropdown, app_ui_effect_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *color_label = lv_label_create(control_panel);
    lv_label_set_text(color_label, "Color");
    lv_obj_set_style_text_color(color_label, lv_color_hex(0xB5C7D3), LV_PART_MAIN);
    lv_obj_align(color_label, LV_ALIGN_TOP_LEFT, 140, 14);

    s_led_color_dropdown = lv_dropdown_create(control_panel);
    lv_dropdown_set_options(s_led_color_dropdown, "Sunset\nRed\nGreen\nBlue\nCyan\nMagenta\nWhite");
    lv_obj_set_width(s_led_color_dropdown, 118);
    lv_obj_align(s_led_color_dropdown, LV_ALIGN_TOP_LEFT, 140, 38);
    lv_obj_add_event_cb(s_led_color_dropdown, app_ui_color_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_led_status_label = lv_label_create(control_panel);
    lv_label_set_text(s_led_status_label, "Effect: Solid\nColor: Sunset");
    lv_obj_set_style_text_color(s_led_status_label, lv_color_hex(0xF4F7FA), LV_PART_MAIN);
    lv_obj_align(s_led_status_label, LV_ALIGN_TOP_LEFT, 16, 78);

    s_led_preview_card = lv_obj_create(led_tab);
    lv_obj_set_size(s_led_preview_card, 50, 50);
    lv_obj_align(s_led_preview_card, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    lv_obj_set_style_radius(s_led_preview_card, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_led_preview_card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_led_preview_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_led_preview_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *preview_text = lv_label_create(led_tab);
    lv_label_set_text(preview_text, "Preview");
    lv_obj_set_style_text_color(preview_text, lv_color_hex(0xD7E3F4), LV_PART_MAIN);
    lv_obj_align(preview_text, LV_ALIGN_BOTTOM_LEFT, 72, -42);

    s_led_note_label = lv_label_create(led_tab);
    lv_label_set_text(s_led_note_label, "Touch the dropdowns to switch the effect and color live.");
    lv_obj_set_width(s_led_note_label, 180);
    lv_label_set_long_mode(s_led_note_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_led_note_label, lv_color_hex(0xAFC2D5), LV_PART_MAIN);
    lv_obj_align(s_led_note_label, LV_ALIGN_BOTTOM_LEFT, 72, -18);

    lv_obj_t *music_panel = lv_obj_create(music_tab);
    lv_obj_set_size(music_panel, 274, 118);
    lv_obj_align(music_panel, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(music_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_panel, lv_color_hex(0x162233), LV_PART_MAIN);
    lv_obj_set_style_border_width(music_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(music_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(music_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *music_tone_label = lv_label_create(music_panel);
    lv_label_set_text(music_tone_label, "Source");
    lv_obj_set_style_text_color(music_tone_label, lv_color_hex(0xB5C7D3), LV_PART_MAIN);
    lv_obj_align(music_tone_label, LV_ALIGN_TOP_LEFT, 16, 14);

    s_music_source_dropdown = lv_dropdown_create(music_panel);
    lv_dropdown_set_options(s_music_source_dropdown, "Tone\nSD Audio");
    lv_obj_set_width(s_music_source_dropdown, 116);
    lv_obj_align(s_music_source_dropdown, LV_ALIGN_TOP_LEFT, 16, 36);
    lv_obj_add_event_cb(s_music_source_dropdown, app_ui_music_source_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *music_tone_label_2 = lv_label_create(music_panel);
    lv_label_set_text(music_tone_label_2, "Tone");
    lv_obj_set_style_text_color(music_tone_label_2, lv_color_hex(0xB5C7D3), LV_PART_MAIN);
    lv_obj_align(music_tone_label_2, LV_ALIGN_TOP_LEFT, 140, 14);
    s_music_option_label = music_tone_label_2;

    s_music_tone_dropdown = lv_dropdown_create(music_panel);
    lv_dropdown_set_options(s_music_tone_dropdown, "A4 440Hz\nC5 523Hz\nE5 659Hz");
    lv_obj_set_width(s_music_tone_dropdown, 116);
    lv_obj_align(s_music_tone_dropdown, LV_ALIGN_TOP_LEFT, 140, 36);
    lv_obj_add_event_cb(s_music_tone_dropdown, app_ui_music_tone_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_music_toggle_btn = lv_btn_create(music_panel);
    lv_obj_set_size(s_music_toggle_btn, 242, 30);
    lv_obj_align(s_music_toggle_btn, LV_ALIGN_TOP_LEFT, 16, 78);
    lv_obj_add_event_cb(s_music_toggle_btn, app_ui_music_toggle_cb, LV_EVENT_CLICKED, NULL);

    s_music_toggle_btn_label = lv_label_create(s_music_toggle_btn);
    lv_label_set_text(s_music_toggle_btn_label, "Play Tone");
    lv_obj_center(s_music_toggle_btn_label);

    lv_obj_t *music_status_panel = lv_obj_create(music_tab);
    lv_obj_set_size(music_status_panel, 274, 106);
    lv_obj_align(music_status_panel, LV_ALIGN_TOP_MID, 0, 136);
    lv_obj_set_style_radius(music_status_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_status_panel, lv_color_hex(0x102030), LV_PART_MAIN);
    lv_obj_set_style_border_width(music_status_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(music_status_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(music_status_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_music_status_label = lv_label_create(music_status_panel);
    lv_label_set_text(s_music_status_label, "State: Stopped\nSource: Tone\nTone: A4 440Hz\nRate: 44100 Hz\nStage: idle");
    lv_obj_set_width(s_music_status_label, 242);
    lv_label_set_long_mode(s_music_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_music_status_label, lv_color_hex(0xF4F7FA), LV_PART_MAIN);
    lv_obj_align(s_music_status_label, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t *music_diag_panel = lv_obj_create(music_tab);
    lv_obj_set_size(music_diag_panel, 274, 122);
    lv_obj_align(music_diag_panel, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_radius(music_diag_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_diag_panel, lv_color_hex(0x0E1824), LV_PART_MAIN);
    lv_obj_set_style_border_width(music_diag_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(music_diag_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(music_diag_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_music_note_label = lv_label_create(music_diag_panel);
    lv_label_set_text(s_music_note_label, "Status: Tone ready\nwrites=0 marks=0 current=idle fail=unknown err=ESP_OK");
    lv_obj_set_width(s_music_note_label, 242);
    lv_label_set_long_mode(s_music_note_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_music_note_label, lv_color_hex(0xAFC2D5), LV_PART_MAIN);
    lv_obj_align(s_music_note_label, LV_ALIGN_TOP_LEFT, 16, 12);

    app_ui_refresh_led_state();
    app_ui_refresh_music_state();
    s_music_refresh_timer = lv_timer_create(app_ui_music_refresh_timer_cb, 180, NULL);
}
