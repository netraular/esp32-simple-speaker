#include "screen_manager.h"
#include "config.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include <cassert>

static const char* TAG = "SCREEN_MGR";

// Timer para el 'tick' de LVGL
static esp_timer_handle_t lv_tick_timer = nullptr;

// Función de callback para el refresco de pantalla de LVGL
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    screen_t* s = (screen_t*)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(s->panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

// Inicializa el timer periódico para LVGL
static void screen_init_lvgl_tick() {
    // CORRECCIÓN: Usar el método de inicialización en dos pasos.
    esp_timer_create_args_t lv_tick_timer_args = {};
    lv_tick_timer_args.callback = [](void* arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); };
    lv_tick_timer_args.name = "lv_tick";

    ESP_ERROR_CHECK(esp_timer_create(&lv_tick_timer_args, &lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lv_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    ESP_LOGI(TAG, "LVGL tick timer initialized with %dms period", LVGL_TICK_PERIOD_MS);
}

screen_t* screen_init() {
    ESP_LOGI(TAG, "Initializing screen hardware");
    screen_t* screen = new screen_t;
    if (!screen) {
        ESP_LOGE(TAG, "Memory allocation failed for screen");
        return nullptr;
    }

    // 1. Configuración del Bus SPI
    // CORRECCIÓN: Usar el método de inicialización en dos pasos.
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = SPI_MOSI_PIN;
    buscfg.miso_io_num = SPI_MISO_PIN;
    buscfg.sclk_io_num = SPI_SCLK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SCREEN_WIDTH * 40 * sizeof(lv_color_t);
    
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    // 2. Configuración del panel IO (SPI)
    // CORRECCIÓN: Usar el método de inicialización en dos pasos.
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = TFT_CS;
    io_config.dc_gpio_num = TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &screen->io_handle));
    ESP_LOGI(TAG, "LCD panel IO initialized");

    // 3. Configuración del driver del panel (ST7789)
    // CORRECCIÓN: Usar el método de inicialización en dos pasos.
      esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = nullptr,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(screen->io_handle, &panel_config, &screen->panel_handle));
    ESP_LOGI(TAG, "ST7789 panel driver initialized");

    // 4. Inicialización del panel
    esp_lcd_panel_reset(screen->panel_handle);
    esp_lcd_panel_init(screen->panel_handle);
    esp_lcd_panel_invert_color(screen->panel_handle, true); // Crucial para el color correcto en muchos ST7789
    esp_lcd_panel_disp_on_off(screen->panel_handle, true);

    // 5. Configuración del Backlight (Luz de fondo)
    // CORRECCIÓN: Usar el método de inicialización en dos pasos.
    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.pin_bit_mask = 1ULL << TFT_BL;
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;

    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(TFT_BL, 1); // Encender backlight
    ESP_LOGI(TAG, "Backlight enabled");

    // 6. Inicialización de LVGL
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();
    screen_init_lvgl_tick();

    // Asignar buffers para LVGL
    screen->lvgl_buf1 = (lv_color_t*)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    screen->lvgl_buf2 = (lv_color_t*)heap_caps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(screen->lvgl_buf1 && screen->lvgl_buf2);

    // Crear y configurar el display de LVGL
    screen->lvgl_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_buffers(screen->lvgl_disp, screen->lvgl_buf1, screen->lvgl_buf2,
                          SCREEN_WIDTH * 40 * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(screen->lvgl_disp, lvgl_flush_cb);
    lv_display_set_user_data(screen->lvgl_disp, screen);
    ESP_LOGI(TAG, "LVGL initialized successfully");

    return screen;
}

void screen_deinit(screen_t* screen) {
    if (lv_tick_timer) {
        esp_timer_stop(lv_tick_timer);
        esp_timer_delete(lv_tick_timer);
        lv_tick_timer = nullptr;
    }
    if (screen) {
        if (screen->panel_handle) esp_lcd_panel_del(screen->panel_handle);
        if (screen->io_handle) esp_lcd_panel_io_del(screen->io_handle);
        if (screen->lvgl_buf1) heap_caps_free(screen->lvgl_buf1);
        if (screen->lvgl_buf2) heap_caps_free(screen->lvgl_buf2);
        delete screen;
        ESP_LOGI(TAG, "Screen deinitialized");
    }
}