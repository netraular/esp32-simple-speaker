# Button Manager

## Descripción
Este componente gestiona los botones físicos del sistema utilizando la librería `espressif/button`. Proporciona una capa de abstracción para registrar eventos de pulsación simple y manejar callbacks de dos niveles: por defecto y específicos de una vista.

## Uso
1.  **Inicializar el gestor de botones:**
    Llamar a esta función una vez al inicio de la aplicación.
    ```cpp
    #include "controllers/button_manager/button_manager.h"
    
    // ...
    
    button_manager_init();
    ```
    Esto configura los pines y asigna los handlers por defecto que simplemente imprimen un mensaje en el log.

2.  **Registrar Handlers Específicos (Opcional):**
    Si una pantalla o estado particular de la aplicación necesita que un botón haga algo diferente, se puede registrar un `view_handler`.
    ```cpp
    void my_custom_ok_action() {
        // Hacer algo especial
    }
    
    button_manager_register_view_handler(BUTTON_OK, my_custom_ok_action);
    ```

3.  **Restaurar Handlers por Defecto:**
    Cuando se sale de esa pantalla o estado especial, se deben restaurar los comportamientos por defecto.
    ```cpp
    button_manager_unregister_view_handlers();
    ```