#include <stdio.h>
#include <string.h>
#include <sys/unistd.h> // Para opendir, readdir, closedir
#include <sys/stat.h>   // Para stat
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h" // Para SPI_DMA_CH_AUTO
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>     // Para funciones de directorio
#include <errno.h>      // <<--- AÑADIDO: Para errno y strerror
#include "nvs_flash.h"  // <<--- AÑADIDO: Para funciones NVS

static const char *TAG = "SD_CARD_TEST";

// ----- Configuración de la Tarjeta SD -----
#define MOUNT_POINT "/sdcard"
// Asegúrate de que estos pines coincidan con tu conexión física
// Los mismos que usamos en el ejemplo anterior
#define SPI_BUS_HOST SPI2_HOST // VSPI en ESP32, SPI2_HOST en ESP32-S2/S3

// Mapeo de pines para la Tarjeta SD (usando los pines elegidos anteriormente)
#define PIN_NUM_MISO GPIO_NUM_13
#define PIN_NUM_MOSI GPIO_NUM_11
#define PIN_NUM_CLK  GPIO_NUM_12
#define PIN_NUM_CS   GPIO_NUM_10

sdmmc_card_t *card; // Variable global para la tarjeta SD

void init_sdcard() {
    esp_err_t ret;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // No formatear si el montaje falla
        .max_files = 5,                  // Máximo de archivos abiertos
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_BUS_HOST; // Especificar SPI2_HOST

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus. Error: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem to %s", mount_point);
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want to format card, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors if needed, or card is properly inserted.", esp_err_to_name(ret));
        }
        card = NULL; // Asegurarse de que 'card' es NULL si falla el montaje
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);
}

void list_sdcard_files(const char *path) {
    ESP_LOGI(TAG, "Listing directory: %s", path);
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s. Error: %s", path, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char full_path[256 + 20]; // Suficiente espacio para path + nombre de archivo
        // Usar MOUNT_POINT como base si el path es solo MOUNT_POINT
        // o si el path ya contiene MOUNT_POINT, simplemente añadir / y el nombre
        if (strcmp(path, MOUNT_POINT) == 0) {
             snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
        } else {
             snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        }


        struct stat entry_stat;
        // Ignorar "." y ".." para evitar errores con stat o listados innecesarios
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (stat(full_path, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s: %s", full_path, strerror(errno));
            // Puedes decidir continuar o no aquí
        }

        if (entry->d_type == DT_DIR) {
            printf("  DIR : %s/\n", entry->d_name);
        } else if (entry->d_type == DT_REG) {
            printf("  FILE: %s (size: %ld bytes)\n", entry->d_name, (long)entry_stat.st_size);
        } else {
            printf("  OTHER: %s (type: %d)\n", entry->d_name, entry->d_type);
        }
    }
    closedir(dir);
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32 SD Card List Files Example");

    // Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_sdcard();

    // Si la tarjeta se inicializó correctamente (card no es NULL)
    if (card != NULL) {
        list_sdcard_files(MOUNT_POINT); // Listar archivos en la raíz de la SD
    } else {
        ESP_LOGE(TAG, "SD card not initialized or mount failed. Cannot list files.");
    }

    ESP_LOGI(TAG, "Program ended. ESP will idle.");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}