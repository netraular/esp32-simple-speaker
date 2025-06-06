#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

// La estructura se mantiene igual, es útil para pasar los handles
typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    lv_display_t *lvgl_disp;
    lv_color_t *lvgl_buf1;
    lv_color_t *lvgl_buf2;
} screen_t;


/**
 * @brief Inicializa el hardware de la pantalla y LVGL.
 *
 * @return Puntero a la estructura screen_t con los handles necesarios, o NULL si falla.
 */
screen_t* screen_init();

/**
 * @brief Desinicializa la pantalla y libera recursos.
 *
 * @param screen Puntero a la estructura screen_t a desinicializar.
 */
void screen_deinit(screen_t* screen);

#endif