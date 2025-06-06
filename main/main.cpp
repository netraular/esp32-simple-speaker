#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "controllers/screen_manager/screen_manager.h"
#include "controllers/button_manager/button_manager.h"

static const char *TAG = "main";
static lv_obj_t *counter_label;

static void counter_timer_cb(lv_timer_t *timer) {
    static int count = 0;
    count++;
    lv_label_set_text_fmt(counter_label, "Contador: %d", count);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Iniciando aplicación");

    // 1. Inicialización de la pantalla
    screen_t* screen = screen_init();
    if (!screen) {
        ESP_LOGE(TAG, "Fallo al inicializar la pantalla, deteniendo.");
        return;
    }

    // 2. Inicialización de los botones
    button_manager_init();
    ESP_LOGI(TAG, "Button manager inicializado.");

    // 3. Crear la interfaz de usuario simple con LVGL
    ESP_LOGI(TAG, "Creando UI del contador");

    counter_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(counter_label, "Contador: 0");
    lv_obj_center(counter_label);

    lv_timer_create(counter_timer_cb, 1000, NULL);
    ESP_LOGI(TAG, "UI creada y timer iniciado.");

    // 4. Bucle principal para manejar LVGL
    ESP_LOGI(TAG, "Entrando en bucle principal");
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}