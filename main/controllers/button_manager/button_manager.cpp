#include "controllers/button_manager/button_manager.h"
#include "esp_log.h"
#include <cassert>

static const char *TAG = "BTN_MGR";

// Array para almacenar los handles de cada botón
static button_handle_t buttons[BUTTON_COUNT];

// Array para almacenar los punteros a las funciones handler
static button_handlers_t button_handlers[BUTTON_COUNT] = { {nullptr, nullptr} };

// --- Handlers por defecto ---
static void default_button_left_handler() { ESP_LOGI(TAG, "Botón pulsado: LEFT"); }
static void default_button_cancel_handler() { ESP_LOGI(TAG, "Botón pulsado: CANCEL"); }
static void default_button_ok_handler() { ESP_LOGI(TAG, "Botón pulsado: OK"); }
static void default_button_right_handler() { ESP_LOGI(TAG, "Botón pulsado: RIGHT"); }
static void default_button_on_off_handler() { ESP_LOGI(TAG, "Botón pulsado: ON/OFF"); }

// Callback genérico que se registra para todos los botones.
// Decide qué función (de vista o por defecto) ejecutar.
static void button_event_cb(void *arg, void *usr_data) {
    button_id_t button_id = (button_id_t)(uintptr_t)usr_data;

    if (button_id < BUTTON_COUNT) {
        if (button_handlers[button_id].view_handler) {
            button_handlers[button_id].view_handler();
        } else if (button_handlers[button_id].default_handler) {
            button_handlers[button_id].default_handler();
        }
    }
}

void button_manager_init() {
    ESP_LOGI(TAG, "Initializing button manager...");

    // Configuración general de tiempos (no usamos long/short press)
    button_config_t btn_config = {
        .long_press_time = 0,
        .short_press_time = 0,
    };
    
    // --- ESTA ES LA PARTE CORREGIDA ---
    // Definimos la configuración GPIO para cada botón usando la inicialización posicional,
    // que era la forma correcta de tu ejemplo original.
    // { gpio_num, active_level, enable_power_save, disable_pull }
    const button_gpio_config_t gpio_configs[BUTTON_COUNT] = {
        [BUTTON_LEFT]   = {BUTTON_LEFT_PIN, 0, false, false},
        [BUTTON_CANCEL] = {BUTTON_CANCEL_PIN, 0, false, false},
        [BUTTON_OK]     = {BUTTON_OK_PIN, 0, false, false},
        [BUTTON_RIGHT]  = {BUTTON_RIGHT_PIN, 0, false, false},
        [BUTTON_ON_OFF] = {BUTTON_ON_OFF_PIN, 0, false, false}
    };

    // Crear cada botón y registrar su callback
    for (int i = 0; i < BUTTON_COUNT; i++) {
        ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_config, &gpio_configs[i], &buttons[i]));
        assert(buttons[i]);

        iot_button_register_cb(buttons[i], BUTTON_SINGLE_CLICK, NULL, button_event_cb, (void*)(uintptr_t)i);
    }
    
    // Asignar los handlers por defecto
    button_manager_register_default_handler(BUTTON_LEFT, default_button_left_handler);
    button_manager_register_default_handler(BUTTON_CANCEL, default_button_cancel_handler);
    button_manager_register_default_handler(BUTTON_OK, default_button_ok_handler);
    button_manager_register_default_handler(BUTTON_RIGHT, default_button_right_handler);
    button_manager_register_default_handler(BUTTON_ON_OFF, default_button_on_off_handler);

    ESP_LOGI(TAG, "Button manager initialized successfully.");
}

// Las funciones restantes no necesitan cambios
void button_manager_register_default_handler(button_id_t button, button_handler_t handler) {
    if (button < BUTTON_COUNT) {
        button_handlers[button].default_handler = handler;
    }
}

void button_manager_register_view_handler(button_id_t button, button_handler_t handler) {
    if (button < BUTTON_COUNT) {
        button_handlers[button].view_handler = handler;
    }
}

void button_manager_unregister_view_handlers() {
    ESP_LOGI(TAG, "Unregistering all view-specific handlers.");
    for (int i = 0; i < BUTTON_COUNT; i++) {
        button_handlers[i].view_handler = nullptr;
    }
    ESP_LOGI(TAG, "Restored default button handlers.");
}