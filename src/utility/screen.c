/* SPDX-License-Identifier: MIT */
#include <lvgl.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zmk/battery.h>
#include <zmk/usb.h>
#include <corne_utility/render.h>

static lv_obj_t *canvas;
static uint8_t frame[CORNE_FRAME_BYTES];
static uint8_t canvas_pixels[LV_CANVAS_BUF_SIZE(160, 68, 8, LV_DRAW_BUF_STRIDE_ALIGN)]
    __aligned(LV_DRAW_BUF_ALIGN);
static struct corne_render_state last_state;
static bool drawn;

/* Called only from LVGL's display work queue, including the first draw. */
static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    struct corne_render_state state = {0};
    corne_utility_get_state(&state.snapshot);
    state.left = IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL);
    state.battery = zmk_battery_state_of_charge();
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    state.usb_powered = zmk_usb_is_powered();
#endif
    if (drawn && !memcmp(&state, &last_state, sizeof(state))) {
        return;
    }
    corne_utility_render(frame, &state);
    const uint32_t stride = lv_draw_buf_width_to_stride(160, LV_COLOR_FORMAT_L8);
    /* Match the stock nice!view 270-degree canvas orientation. */
    for (int y = 0; y < CORNE_DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < CORNE_DISPLAY_WIDTH; ++x) {
            unsigned int bit = y * CORNE_DISPLAY_WIDTH + x;
            canvas_pixels[x * stride + (CORNE_DISPLAY_HEIGHT - 1 - y)] =
                (frame[bit / 8] & (0x80U >> (bit % 8))) ? 0 : 255;
        }
    }
    last_state = state;
    drawn = true;
    lv_obj_invalidate(canvas);
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, 160, 68);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    canvas = lv_canvas_create(screen);
    lv_obj_remove_style_all(canvas);
    lv_canvas_set_buffer(canvas, canvas_pixels, 160, 68, LV_COLOR_FORMAT_L8);
    lv_obj_set_pos(canvas, 0, 0);
    refresh(NULL);
    /* State changes redraw at most four times per second; unchanged state does
     * no rendering or SPI work. The existing display queue owns all LVGL calls.
     */
    lv_timer_create(refresh, 250, NULL);
    return screen;
}
