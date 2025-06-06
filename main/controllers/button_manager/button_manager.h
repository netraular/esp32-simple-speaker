#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include "iot_button.h"
#include "button_gpio.h"
#include "config.h"

// Enum para identificar cada botón de forma única
typedef enum {
    BUTTON_LEFT = 0,
    BUTTON_CANCEL,    
    BUTTON_OK,
    BUTTON_RIGHT,
    BUTTON_ON_OFF,
    BUTTON_COUNT // Número total de botones, útil para bucles
} button_id_t;

// Tipo de función para los manejadores de eventos de botón (callbacks)
typedef void (*button_handler_t)(void);

// Estructura para gestionar los handlers por defecto y los específicos de una vista
typedef struct {
    button_handler_t default_handler;
    button_handler_t view_handler;
} button_handlers_t;

/**
 * @brief Inicializa el gestor de botones, configura los pines GPIO y registra los handlers por defecto.
 */
void button_manager_init();

/**
 * @brief Registra un handler por defecto para un botón específico.
 * Este handler se usa cuando no hay un handler de vista específico activo.
 * @param button El ID del botón.
 * @param handler La función callback a ejecutar.
 */
void button_manager_register_default_handler(button_id_t button, button_handler_t handler);

/**
 * @brief Registra un handler específico de una "vista" o pantalla.
 * Este handler tiene prioridad sobre el handler por defecto.
 * @param button El ID del botón.
 * @param handler La función callback a ejecutar.
 */
void button_manager_register_view_handler(button_id_t button, button_handler_t handler);

/**
 * @brief Desregistra todos los handlers de vista y restaura los handlers por defecto.
 * Útil al cambiar de pantalla en la UI.
 */
void button_manager_unregister_view_handlers();

#endif // BUTTON_MANAGER_H