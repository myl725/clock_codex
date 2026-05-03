#include "app_ui.h"

#include "app_led_service.h"
#include "app_music_service.h"
#include "app_usb_audio_service.h"
#include "lvgl.h"

static lv_obj_t *s_led_status_label;
static lv_obj_t *s_led_preview_card;
static lv_obj_t *s_led_note_label;
static lv_obj_t *s_led_effect_dropdown;
static lv_obj_t *s_led_color_dropdown;
static lv_obj_t *s_usb_status_label;
static lv_obj_t *s_usb_note_label;
static lv_obj_t *s_usb_hint_label;
static lv_obj_t *s_usb_volume_label;
static lv_obj_t *s_usb_mute_btn_label;
static lv_timer_t *s_usb_refresh_timer;

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

static const char *app_ui_usb_link_text(const app_usb_audio_status_t *usb_status)
{
    if (usb_status->mounted) {
        return "Connected";
    }

    return "Waiting";
}

static const char *app_ui_usb_stream_text(const app_usb_audio_status_t *usb_status)
{
    if (usb_status->stream_active) {
        return "Streaming";
    }

    if (usb_status->mounted) {
        return "Idle";
    }

    return "Offline";
}

static void app_ui_usb_set_volume_text(const app_usb_audio_status_t *usb_status)
{
    if (s_usb_volume_label == NULL || usb_status == NULL) {
        return;
    }

    if (usb_status->master_mute) {
        lv_label_set_text(s_usb_volume_label, "Volume: Muted");
        return;
    }

    lv_label_set_text_fmt(
        s_usb_volume_label,
        "Volume: %.1f dB",
        (double)usb_status->master_volume_db_256 / 256.0
    );
}

static void app_ui_refresh_usb_state(void)
{
    if (s_usb_status_label == NULL || s_usb_note_label == NULL || s_usb_hint_label == NULL) {
        return;
    }

    app_music_snapshot_t music_snapshot;
    app_music_usb_audio_status_t usb_music_status;
    app_usb_audio_status_t usb_status;

    app_music_service_get_snapshot(&music_snapshot);
    app_music_service_usb_get_status(&usb_music_status);
    app_usb_audio_service_get_status(&usb_status);

    lv_label_set_text_fmt(
        s_usb_status_label,
        "Link: %s\nStream: %s\nRate: %lu Hz\nBuffer: %u/%u\nErrors: %lu",
        app_ui_usb_link_text(&usb_status),
        app_ui_usb_stream_text(&usb_status),
        (unsigned long)music_snapshot.sample_rate_hz,
        (unsigned)usb_music_status.buffered_frames,
        (unsigned)usb_music_status.capacity_frames,
        (unsigned long)usb_status.read_error_count
    );
    app_ui_usb_set_volume_text(&usb_status);
    if (s_usb_mute_btn_label != NULL) {
        lv_label_set_text(s_usb_mute_btn_label, usb_status.master_mute ? "Unmute" : "Mute");
    }

    lv_label_set_text_fmt(
        s_usb_note_label,
        "USB alt=%u packets=%lu accepted=%lu\nunderrun=%lu overflow=%lu",
        (unsigned)usb_status.stream_alt_setting,
        (unsigned long)usb_status.packet_count,
        (unsigned long)usb_status.accepted_frames,
        (unsigned long)usb_music_status.underrun_count,
        (unsigned long)usb_music_status.overflow_count
    );

    if (usb_status.stream_active) {
        lv_label_set_text(s_usb_hint_label, "Host audio is active. Adjust volume and mute from the computer.");
    } else if (usb_status.mounted) {
        lv_label_set_text(s_usb_hint_label, "OTG is connected. Start audio playback on the computer to begin streaming.");
    } else {
        lv_label_set_text(s_usb_hint_label, "Connect the OTG port to a computer. The TTL port stays available for logs and flashing.");
    }
}

static void app_ui_usb_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    app_ui_refresh_usb_state();
}

static void app_ui_usb_volume_down_cb(lv_event_t *event)
{
    (void)event;
    app_usb_audio_service_adjust_master_volume(-512);
    app_ui_refresh_usb_state();
}

static void app_ui_usb_volume_up_cb(lv_event_t *event)
{
    (void)event;
    app_usb_audio_service_adjust_master_volume(512);
    app_ui_refresh_usb_state();
}

static void app_ui_usb_mute_toggle_cb(lv_event_t *event)
{
    (void)event;
    app_usb_audio_service_toggle_master_mute();
    app_ui_refresh_usb_state();
}

void app_ui_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Clock Codex USB Speaker");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7B267), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "USB speaker status plus WS2812 control");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xD7E3F4), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *tabview = lv_tabview_create(screen, LV_DIR_TOP, 32);
    lv_obj_set_size(tabview, 320, 170);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x0D1B2A), LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_anim_time(tabview, 0, LV_PART_MAIN);
    lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *speaker_tab = lv_tabview_add_tab(tabview, "Speaker");
    lv_obj_t *led_tab = lv_tabview_add_tab(tabview, "Lighting");

    lv_obj_set_style_bg_color(speaker_tab, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speaker_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(led_tab, lv_color_hex(0x08131F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(led_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scroll_dir(speaker_tab, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(speaker_tab, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *speaker_panel = lv_obj_create(speaker_tab);
    lv_obj_set_size(speaker_panel, 274, 100);
    lv_obj_align(speaker_panel, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(speaker_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(speaker_panel, lv_color_hex(0x162233), LV_PART_MAIN);
    lv_obj_set_style_border_width(speaker_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(speaker_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(speaker_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *speaker_label = lv_label_create(speaker_panel);
    lv_label_set_text(speaker_label, "USB Speaker");
    lv_obj_set_style_text_color(speaker_label, lv_color_hex(0xB5C7D3), LV_PART_MAIN);
    lv_obj_align(speaker_label, LV_ALIGN_TOP_LEFT, 16, 14);

    s_usb_status_label = lv_label_create(speaker_panel);
    lv_label_set_text(s_usb_status_label, "Link: Waiting\nStream: Offline\nRate: 0 Hz\nBuffer: 0/0\nErrors: 0");
    lv_obj_set_width(s_usb_status_label, 242);
    lv_label_set_long_mode(s_usb_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_usb_status_label, lv_color_hex(0xF4F7FA), LV_PART_MAIN);
    lv_obj_align(s_usb_status_label, LV_ALIGN_TOP_LEFT, 16, 34);

    lv_obj_t *speaker_control_panel = lv_obj_create(speaker_tab);
    lv_obj_set_size(speaker_control_panel, 274, 86);
    lv_obj_align(speaker_control_panel, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_radius(speaker_control_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(speaker_control_panel, lv_color_hex(0x102030), LV_PART_MAIN);
    lv_obj_set_style_border_width(speaker_control_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(speaker_control_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(speaker_control_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_usb_volume_label = lv_label_create(speaker_control_panel);
    lv_label_set_text(s_usb_volume_label, "Volume: 0.0 dB");
    lv_obj_set_style_text_color(s_usb_volume_label, lv_color_hex(0xF4F7FA), LV_PART_MAIN);
    lv_obj_align(s_usb_volume_label, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t *volume_down_btn = lv_btn_create(speaker_control_panel);
    lv_obj_set_size(volume_down_btn, 54, 30);
    lv_obj_align(volume_down_btn, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    lv_obj_add_event_cb(volume_down_btn, app_ui_usb_volume_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *volume_down_label = lv_label_create(volume_down_btn);
    lv_label_set_text(volume_down_label, "Vol-");
    lv_obj_center(volume_down_label);

    lv_obj_t *mute_btn = lv_btn_create(speaker_control_panel);
    lv_obj_set_size(mute_btn, 74, 30);
    lv_obj_align(mute_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(mute_btn, app_ui_usb_mute_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_usb_mute_btn_label = lv_label_create(mute_btn);
    lv_label_set_text(s_usb_mute_btn_label, "Mute");
    lv_obj_center(s_usb_mute_btn_label);

    lv_obj_t *volume_up_btn = lv_btn_create(speaker_control_panel);
    lv_obj_set_size(volume_up_btn, 54, 30);
    lv_obj_align(volume_up_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_add_event_cb(volume_up_btn, app_ui_usb_volume_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *volume_up_label = lv_label_create(volume_up_btn);
    lv_label_set_text(volume_up_label, "Vol+");
    lv_obj_center(volume_up_label);

    lv_obj_t *speaker_diag_panel = lv_obj_create(speaker_tab);
    lv_obj_set_size(speaker_diag_panel, 274, 84);
    lv_obj_align(speaker_diag_panel, LV_ALIGN_TOP_MID, 0, 214);
    lv_obj_set_style_radius(speaker_diag_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(speaker_diag_panel, lv_color_hex(0x102030), LV_PART_MAIN);
    lv_obj_set_style_border_width(speaker_diag_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(speaker_diag_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(speaker_diag_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_usb_note_label = lv_label_create(speaker_diag_panel);
    lv_label_set_text(s_usb_note_label, "USB alt=0 packets=0 accepted=0\nunderrun=0 overflow=0");
    lv_obj_set_width(s_usb_note_label, 242);
    lv_label_set_long_mode(s_usb_note_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_usb_note_label, lv_color_hex(0xD7E3F4), LV_PART_MAIN);
    lv_obj_align(s_usb_note_label, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t *speaker_hint_panel = lv_obj_create(speaker_tab);
    lv_obj_set_size(speaker_hint_panel, 274, 96);
    lv_obj_align(speaker_hint_panel, LV_ALIGN_TOP_MID, 0, 310);
    lv_obj_set_style_radius(speaker_hint_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(speaker_hint_panel, lv_color_hex(0x0E1824), LV_PART_MAIN);
    lv_obj_set_style_border_width(speaker_hint_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(speaker_hint_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(speaker_hint_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_usb_hint_label = lv_label_create(speaker_hint_panel);
    lv_label_set_text(
        s_usb_hint_label,
        "Connect the OTG port to a computer. The TTL port stays available for logs and flashing."
    );
    lv_obj_set_width(s_usb_hint_label, 242);
    lv_label_set_long_mode(s_usb_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_usb_hint_label, lv_color_hex(0xAFC2D5), LV_PART_MAIN);
    lv_obj_align(s_usb_hint_label, LV_ALIGN_TOP_LEFT, 16, 12);

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

    app_music_service_set_source(APP_MUSIC_SOURCE_USB_AUDIO);
    app_ui_refresh_led_state();
    app_ui_refresh_usb_state();
    s_usb_refresh_timer = lv_timer_create(app_ui_usb_refresh_timer_cb, 180, NULL);
}
