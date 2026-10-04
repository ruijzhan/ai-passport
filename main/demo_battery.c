// main/demo_battery.c —— CW2017 电量与电压,每秒刷新。
// I2C 读数会阻塞(单次超时最长 100ms,两次约 200ms),故放在独立 worker
// 任务里轮询;LVGL 侧只做短暂加锁的文本更新,避免卡住渲染任务触发看门狗。
#include "demo.h"
#include "bsp_battery.h"
#include "bsp_display.h"   // bsp_lvgl_lock / bsp_lvgl_unlock(worker 任务里操作 LVGL 要加锁)
#include "ui_pixel.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BATTERY_POLL_MS         1000
#define BATTERY_STOP_TIMEOUT_MS 2000

static lv_obj_t *s_scr, *s_soc, *s_mv;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_stopped;
static volatile bool s_cancel;

// 调用前必须已持有 LVGL 锁。
static void ui_update_locked(int soc, int mv) {
    if (s_soc) {
        if (soc < 0) lv_label_set_text(s_soc, "-- %");
        else         lv_label_set_text_fmt(s_soc, "%d %%", soc);

        // 低电量变红,便于一眼判断
        lv_obj_set_style_text_color(s_soc,
            (soc >= 0 && soc < 20) ? lv_color_hex(0xFF5A5A) : lv_color_hex(0x39FF88), 0);
    }
    if (s_mv) {
        if (mv < 0)  lv_label_set_text(s_mv, "-- mV");
        else         lv_label_set_text_fmt(s_mv, "%d mV", mv);
    }
}

static void battery_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_cancel) break;
        // I2C 阻塞读放在 worker 上下文,不占用 LVGL 任务。
        int soc = bsp_battery_soc();
        int mv  = bsp_battery_mv();
        if (s_cancel) break;
        if (bsp_lvgl_lock(250)) {
            ui_update_locked(soc, mv);
            bsp_lvgl_unlock();
        }
        // 切成 100ms 小段休眠,stop 置位后最多 100ms 即可响应退出。
        for (int waited = 0; waited < BATTERY_POLL_MS && !s_cancel; waited += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    // lifecycle owner 在收到该 ack 前一直持有 task 句柄;ack 之后不再触碰
    // 任何共享状态并挂起,超时的 stop 可安全重试,只有 owner 负责删除任务。
    xSemaphoreGive(s_stopped);
    for (;;) vTaskSuspend(NULL);
}

void demo_battery_enter(void) {
    s_scr = ui_pixel_screen_create("BATTERY");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 18, 50, 284, 140, UI_YELLOW);

    s_soc = lv_label_create(panel);
    lv_obj_set_style_text_font(s_soc, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_soc, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_soc, LV_ALIGN_TOP_MID, 0, 18);
    lv_label_set_text(s_soc, "-- %");

    s_mv = lv_label_create(panel);
    lv_obj_set_style_text_color(s_mv, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_mv, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_text(s_mv, "-- mV");

    lv_obj_t *battery = ui_pixel_panel_create(panel, 92, 84, 100, 34, UI_GRASS);
    lv_obj_set_style_border_width(battery, 4, 0);
    ui_pixel_mascot_create(s_scr, 272, 158);

    lv_screen_load(s_scr);   // 首帧先显示占位符,worker 读到 I2C 后再刷新
}

esp_err_t demo_battery_start(void) {
    if (s_task) return ESP_OK;
    if (s_stopped) {
        vSemaphoreDelete(s_stopped);
        s_stopped = NULL;
    }
    s_stopped = xSemaphoreCreateBinary();
    if (!s_stopped) return ESP_ERR_NO_MEM;
    s_cancel = false;
    if (xTaskCreate(battery_task, "demo_battery", 4096, NULL, 4, &s_task) != pdPASS) {
        vSemaphoreDelete(s_stopped);
        s_stopped = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t demo_battery_stop(void) {
    TaskHandle_t task = s_task;
    if (!task) {
        if (s_stopped) {
            vSemaphoreDelete(s_stopped);
            s_stopped = NULL;
        }
        return ESP_OK;
    }

    s_cancel = true;
    if (!s_stopped ||
        xSemaphoreTake(s_stopped, pdMS_TO_TICKS(BATTERY_STOP_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    vTaskDelete(task);
    s_task = NULL;
    vSemaphoreDelete(s_stopped);
    s_stopped = NULL;
    return ESP_OK;
}

void demo_battery_exit(void) {
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_soc = s_mv = NULL; }
}

void demo_battery_key(bsp_btn_t btn, bsp_btn_ev_t ev) { (void)btn; (void)ev; }
