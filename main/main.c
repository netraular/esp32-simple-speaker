#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include "nvs_flash.h"

// ---- Includes para la NUEVA API I2S ----
#include "driver/i2s_std.h" // Nueva API para modo estándar I2S

// ---- Includes Adicionales ----
#include <inttypes.h>   // Para PRIu32, PRIu16, etc.
#include "esp_timer.h"   // Para esp_timer_get_time()


static const char *TAG = "AUDIO_PLAYER";

// ----- Configuración de la Tarjeta SD -----
#define MOUNT_POINT "/sdcard"
#define SPI_BUS_HOST SPI2_HOST // ESP32-S3 por defecto usa SPI2 para esto si se usa SDSPI_HOST_DEFAULT() con slot 1 o 2.
                               // SPI2_HOST es el ID del controlador SPI, no confundir con HSPI_HOST o VSPI_HOST de ESP32 original.

#define PIN_NUM_MISO GPIO_NUM_13
#define PIN_NUM_MOSI GPIO_NUM_11
#define PIN_NUM_CLK  GPIO_NUM_12
#define PIN_NUM_CS   GPIO_NUM_10

sdmmc_card_t *card;

// ----- Configuración de I2S (Nueva API) -----
#define I2S_PORT_NUM         (I2S_NUM_0) // El controlador I2S a usar (0 o 1)
// Los siguientes valores son solo predeterminados, se sobrescribirán por el WAV
// #define I2S_SAMPLE_RATE (44100) // No es necesario un default aquí, se toma del WAV
// #define I2S_BITS_PER_SAMPLE (I2S_BITS_PER_SAMPLE_16BIT) // Idem

#define I2S_BCK_IO      (GPIO_NUM_5)
#define I2S_WS_IO       (GPIO_NUM_6) // También conocido como LRCK
#define I2S_DO_IO       (GPIO_NUM_4) // Data Out
// #define I2S_DI_IO       (-1) // Data In, no usado para reproducción

i2s_chan_handle_t tx_chan; // Handle para el canal de transmisión I2S

typedef struct {
    char riff_header[4];
    uint32_t wav_size;
    char wave_header[4];
    char fmt_header[4];
    uint32_t fmt_chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    // Estos dos pueden no estar inmediatamente después, por eso el bucle de búsqueda
    char data_header[4]; 
    uint32_t data_size;
} wav_header_t;


void init_sdcard() {
    esp_err_t ret;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_BUS_HOST; // Usar SPI2_HOST en ESP32-S3

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000, // Puede ser 4092 o un múltiplo de 512 para mejor rendimiento
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
            ESP_LOGE(TAG, "Failed to mount filesystem. Make sure SD card is formatted as FAT32.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check CS pin, connections, and card format.", esp_err_to_name(ret));
        }
        // No es necesario desinicializar el bus SPI aquí si otras partes del sistema podrían usarlo,
        // pero si es exclusivo para la SD y falla el montaje, se podría considerar.
        // spi_bus_free(host.slot); // Considerar si la inicialización falla catastróficamente
        card = NULL;
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
        char full_path[256 + 20]; // Suficiente para MOUNT_POINT + '/' + nombre de archivo
        if (strcmp(path, MOUNT_POINT) == 0 && path[strlen(path)-1] == '/') { // MOUNT_POINT podría ser "/sdcard/"
             snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, entry->d_name);
        } else if (strcmp(path, MOUNT_POINT) == 0) { // MOUNT_POINT es "/sdcard"
             snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
        }
         else {
             snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        }

        struct stat entry_stat;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Intentar obtener stat, pero no fallar si es un archivo de sistema oculto problemático
        if (stat(full_path, &entry_stat) == -1) {
            // Común para "System Volume Information" o archivos con nombres extraños
            if (strstr(full_path, "System Volume Information") == NULL) { 
                 ESP_LOGW(TAG, "Failed to stat %s: %s. Skipping.", full_path, strerror(errno));
            }
            continue;
        }

        if (entry->d_type == DT_DIR) {
            printf("  DIR : %s/\n", entry->d_name);
        } else if (entry->d_type == DT_REG) {
            printf("  FILE: %s (size: %jd bytes)\n", entry->d_name, (intmax_t)entry_stat.st_size);
        } else {
            printf("  OTHER: %s (type: %d)\n", entry->d_name, entry->d_type);
        }
    }
    closedir(dir);
}

// No necesitamos una función `init_i2s` separada si `play_wav` lo hace todo dinámicamente.
// Si quisiéramos una inicialización por defecto, la pondríamos aquí.

void play_wav(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s, errno: %s", filepath, strerror(errno));
        return;
    }
    ESP_LOGI(TAG, "Opened WAV file: %s", filepath);

    wav_header_t wav_header;
    // Leer los primeros 36 bytes del encabezado (hasta bits_per_sample inclusive)
    if (fread(&wav_header, 1, sizeof(wav_header_t) - 8, fp) != (sizeof(wav_header_t) - 8)) { // -8 para data_header y data_size
        ESP_LOGE(TAG, "Failed to read initial WAV header.");
        fclose(fp);
        return;
    }

    if (strncmp(wav_header.riff_header, "RIFF", 4) != 0 ||
        strncmp(wav_header.wave_header, "WAVE", 4) != 0 ||
        strncmp(wav_header.fmt_header, "fmt ", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV file header (RIFF/WAVE/fmt).");
        fclose(fp);
        return;
    }
    
    // El chunk "fmt " puede tener un tamaño mayor a 16 si hay datos extra.
    // Los campos estándar (audio_format a bits_per_sample) ocupan 16 bytes.
    // Si fmt_chunk_size es mayor, debemos saltar los bytes extra.
    if (wav_header.fmt_chunk_size > 16) {
        ESP_LOGI(TAG, "fmt_chunk_size is %" PRIu32 ", skipping %" PRIu32 " extra bytes.", wav_header.fmt_chunk_size, wav_header.fmt_chunk_size - 16);
        if (fseek(fp, wav_header.fmt_chunk_size - 16, SEEK_CUR) != 0) {
            ESP_LOGE(TAG, "Failed to seek past extra fmt data.");
            fclose(fp);
            return;
        }
    }

    // Bucle para encontrar el chunk "data"
    // Esto es importante porque puede haber otros chunks (como "LIST", "INFO", etc.) antes de "data"
    while (1) {
        if (fread(wav_header.data_header, 1, 4, fp) != 4) { // Leer ID del chunk (ej. "data")
            ESP_LOGE(TAG, "Failed to read chunk ID while searching for 'data'.");
            fclose(fp);
            return;
        }
        if (fread(&wav_header.data_size, 1, 4, fp) != 4) { // Leer tamaño del chunk
            ESP_LOGE(TAG, "Failed to read chunk size while searching for 'data'.");
            fclose(fp);
            return;
        }
        ESP_LOGI(TAG, "Found chunk: %.4s, size: %" PRIu32, wav_header.data_header, wav_header.data_size);

        if (strncmp(wav_header.data_header, "data", 4) == 0) {
            if (wav_header.data_size == 0) {
                ESP_LOGE(TAG, "'data' chunk has zero size. Nothing to play.");
                fclose(fp);
                return;
            }
            break; // Encontrado el chunk "data", salir del bucle
        }
        // Si no es "data", saltar este chunk
        if (fseek(fp, wav_header.data_size, SEEK_CUR) != 0) {
             ESP_LOGE(TAG, "Failed to seek past unknown chunk '%.4s'.", wav_header.data_header);
             fclose(fp);
             return;
        }
    }

    ESP_LOGI(TAG, "WAV Info: Format %" PRIu16 ", Channels %" PRIu16 ", Sample Rate %" PRIu32 ", Bits/Sample %" PRIu16 ", Data Size %" PRIu32,
             wav_header.audio_format, wav_header.num_channels, wav_header.sample_rate,
             wav_header.bits_per_sample, wav_header.data_size);

    if (wav_header.audio_format != 1) { // 1 = PCM
        ESP_LOGE(TAG, "Unsupported WAV audio format: %" PRIu16 ". Only PCM (1) is supported.", wav_header.audio_format);
        fclose(fp);
        return;
    }
    
    // Validar bits_per_sample para la API I2S
    if (wav_header.bits_per_sample != 8 && wav_header.bits_per_sample != 16 &&
        wav_header.bits_per_sample != 24 && wav_header.bits_per_sample != 32) {
        ESP_LOGE(TAG, "Unsupported bits per sample: %" PRIu16 ". Only 8, 16, 24, or 32-bit supported by I2S driver.", wav_header.bits_per_sample);
        fclose(fp);
        return;
    }
    
    // Si ya hay un canal I2S configurado, deshabilitarlo y eliminarlo antes de reconfigurar
    if (tx_chan != NULL) {
        ESP_LOGI(TAG, "Disabling and deleting previous I2S TX channel.");
        ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
        ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
        tx_chan = NULL;
    }
    
    ESP_LOGI(TAG, "Configuring I2S TX channel...");
    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8, // Número de descriptores DMA
        .dma_frame_num = 256, // Número de frames por descriptor DMA (frame = muestra para todos los canales)
                              // Un frame para 16-bit stereo es 4 bytes. 256 frames * 4 bytes/frame = 1024 bytes por buffer DMA
        .auto_clear = true, // Limpiar automáticamente el buffer TX en underrun
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL)); // tx_chan, no rx_chan

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wav_header.sample_rate);
    // Para ESP32-S3, la fuente de reloj por defecto suele ser PLL_F160M. 
    // Si se necesita APLL para mayor precisión en ciertas tasas de muestreo, se puede configurar:
    // clk_cfg.clk_src = I2S_CLK_SRC_APLL;
    
    i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)wav_header.bits_per_sample, 
                                                                   (wav_header.num_channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
    // Para MAX98357A, el formato es I2S standard (Philips), que es MSB justificado.
    // slot_cfg.slot_mask = (wav_header.num_channels == 2) ? I2S_STD_SLOT_BOTH : I2S_STD_SLOT_LEFT; // Ya manejado por I2S_SLOT_MODE_MONO/STEREO

    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED, // MAX98357A no necesita MCLK externo
        .bclk = I2S_BCK_IO,
        .ws = I2S_WS_IO,
        .dout = I2S_DO_IO,
        .din = I2S_GPIO_UNUSED, // No se usa para TX
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    ESP_LOGI(TAG, "I2S reconfigured for WAV: SR=%" PRIu32 ", BPS=%" PRIu16 ", CH=%" PRIu16,
             wav_header.sample_rate, wav_header.bits_per_sample, wav_header.num_channels);

    // El tamaño del buffer de lectura puede ser un múltiplo del block_align o un tamaño fijo razonable.
    // block_align = num_channels * bits_per_sample / 8
    int buffer_size = wav_header.block_align > 0 ? wav_header.block_align * 256 : 2048; // ej. 256 frames de datos
    if (buffer_size > 4096) buffer_size = 4096; // Limitar el tamaño del buffer
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer of size %d", buffer_size);
        fclose(fp);
        // Aquí también deberíamos limpiar el canal I2S si fallamos después de configurarlo
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
        return;
    }

    size_t bytes_read;
    size_t bytes_written;
    uint32_t total_bytes_played = 0;

    ESP_LOGI(TAG, "Starting playback... Buffer size: %d bytes", buffer_size);
    uint64_t start_time = esp_timer_get_time();

    while (total_bytes_played < wav_header.data_size) {
        size_t to_read = (wav_header.data_size - total_bytes_played < buffer_size) ? 
                         (wav_header.data_size - total_bytes_played) : buffer_size;
        
        bytes_read = fread(buffer, 1, to_read, fp);
        if (bytes_read == 0) {
            if (feof(fp)) {
                ESP_LOGI(TAG, "End of file reached.");
            } else if (ferror(fp)) {
                ESP_LOGE(TAG, "Error reading from file: %s", strerror(errno));
            }
            break;
        }

        // Escribir datos al canal I2S
        esp_err_t write_err = i2s_channel_write(tx_chan, buffer, bytes_read, &bytes_written, portMAX_DELAY);
        if (write_err != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(write_err));
            break;
        }
        if (bytes_read != bytes_written) {
            ESP_LOGW(TAG, "I2S write underrun or mismatch: read %zu, wrote %zu", bytes_read, bytes_written);
            // Podría ser normal al final del archivo si bytes_read no es múltiplo de frame size
        }
        total_bytes_played += bytes_written;
    }
    uint64_t end_time = esp_timer_get_time();
    float duration_sec = (end_time - start_time) / 1000000.0f;

    ESP_LOGI(TAG, "Playback finished. Total bytes played: %" PRIu32 " / %" PRIu32 ". Duration: %.2f s",
            total_bytes_played, wav_header.data_size, duration_sec);

    // Esperar a que se reproduzcan todos los datos en el buffer DMA antes de deshabilitar
    vTaskDelay(pdMS_TO_TICKS(50)); // Pequeña demora para asegurar que el DMA termine de enviar.
                                   // Un cálculo más preciso sería (dma_desc_num * dma_frame_num * bytes_per_frame) / byte_rate

    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    // No eliminamos el canal aquí (i2s_del_channel) si vamos a reproducir otra canción.
    // Lo eliminamos al principio de play_wav si ya existe, o al final del programa.
    // Por ahora, lo mantenemos para una posible siguiente reproducción. Si solo es una canción, podríamos eliminarlo.

    free(buffer);
    fclose(fp);
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 Audio Player from SD Card (New I2S API)");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_sdcard();

    if (card != NULL) {
        list_sdcard_files(MOUNT_POINT);
        // No se llama a init_i2s() aquí, play_wav() maneja toda la configuración de I2S
        play_wav(MOUNT_POINT "/SONG.WAV"); // Asegúrate de que este archivo exista
        // play_wav(MOUNT_POINT "/OTRA_CANCION.WAV"); // Podrías llamar de nuevo
    } else {
        ESP_LOGE(TAG, "SD card not initialized. Cannot play audio.");
    }

    // Limpieza final si ya no se va a usar I2S
    if (tx_chan != NULL) {
        ESP_LOGI(TAG, "Cleaning up I2S TX channel.");
        i2s_channel_disable(tx_chan); // Asegurarse que esté deshabilitado
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }
    
    ESP_LOGI(TAG, "Program ended. ESP will idle or you can add a loop/restart.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}