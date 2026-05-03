#include "app_usb_audio_descriptors.h"

#include "tusb.h"

#define APP_USB_AUDIO_VID TINYUSB_ESPRESSIF_VID
#define APP_USB_AUDIO_PID 0x4053
#define APP_USB_AUDIO_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + APP_USB_AUDIO_SPEAKER_STEREO_DESC_LEN)

#define APP_USB_AUDIO_EP_OUT 0x02

static CFG_TUD_MEM_SECTION tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = APP_USB_AUDIO_VID,
    .idProduct = APP_USB_AUDIO_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static CFG_TUD_MEM_SECTION uint8_t s_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, APP_USB_AUDIO_ITF_TOTAL, 0, APP_USB_AUDIO_CONFIG_TOTAL_LEN, 0x00, 100),
    APP_USB_AUDIO_SPEAKER_STEREO_DESCRIPTOR(
        APP_USB_AUDIO_ITF_AUDIO_CONTROL,
        0x04,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX,
        CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
        APP_USB_AUDIO_EP_OUT,
        CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
    ),
};

static const char *s_string_descriptors[] = {
    (const char[]) {0x09, 0x04},
    "Clock Codex",
    "Clock Codex USB Speaker",
    "CCX-USB-SPK-03",
    "USB Audio",
};

static const tinyusb_desc_config_t s_descriptor_config = {
    .device = &s_device_descriptor,
    .qualifier = NULL,
    .string = s_string_descriptors,
    .string_count = (int)(sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0])),
    .full_speed_config = s_config_descriptor,
    .high_speed_config = NULL,
};

const tinyusb_desc_config_t *app_usb_audio_descriptors_get_config(void)
{
    return &s_descriptor_config;
}
