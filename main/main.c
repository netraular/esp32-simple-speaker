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
#include "driver/gpio.h"    // Para control de GPIO (botones)


// ---- Includes Adicionales ----
#include <inttypes.h>   // Para PRIu32, PRIu16, etc.
#include "esp_timer.h"   // Para esp_timer_get_time()


static const char *TAG = "AUDIO_PLAYER";

// ----- Configuración de la Tarjeta SD -----
#define MOUNT_POINT "/sdcard"
#define SPI_BUS_HOST SPI2_HOST

#define PIN_NUM_MISO GPIO_NUM_13
#define PIN_NUM_MOSI GPIO_NUM_11
#define PIN_NUM_CLK  GPIO_NUM_12
#define PIN_NUM_CS   GPIO_NUM_10

sdmmc_card_t *card;

// ----- Configuración de I2S (Nueva API) -----
#define I2S_PORT_NUM         (I2S_NUM_0)
#define I2S_BCK_IO      (GPIO_NUM_5)
#define I2S_WS_IO       (GPIO_NUM_6)
#define I2S_DO_IO       (GPIO_NUM_4)

i2s_chan_handle_t tx_chan; // Handle para el canal de transmisión I2S

// ***** NUEVO: Factor de volumen (0.0 a 1.0) y porcentaje *****
static volatile float volume_factor = 0.25f; // Por defecto, 25% de volumen
static volatile uint8_t current_volume_percentage = 25; // Porcentaje actual de volumen (0-100)
#define VOLUME_STEP 5 // Cuánto cambia el volumen con cada pulsación (en porcentaje)

// ----- Configuración de Botones de Volumen (Pins updated as per user log) -----
#define GPIO_VOLUME_UP   GPIO_NUM_15 // Pin para subir volumen (was GPIO_NUM_14)
#define GPIO_VOLUME_DOWN GPIO_NUM_16 // Pin para bajar volumen (was GPIO_NUM_15)
#define DEBOUNCE_TIME_MS 50       // Tiempo de antirrebote para los botones

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
    host.slot = SPI_BUS_HOST;

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
            ESP_LOGE(TAG, "Failed to mount filesystem. Make sure SD card is formatted as FAT32.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check CS pin, connections, and card format.", esp_err_to_name(ret));
        }
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
        char full_path[256 + 20];
        if (strcmp(path, MOUNT_POINT) == 0 && path[strlen(path)-1] == '/') {
             snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, entry->d_name);
        } else if (strcmp(path, MOUNT_POINT) == 0) {
             snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
        }
         else {
             snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        }

        struct stat entry_stat;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (stat(full_path, &entry_stat) == -1) {
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

// Función para ajustar el volumen
void set_volume(uint8_t percentage) {
    if (percentage > 100) percentage = 100;
    current_volume_percentage = percentage; // Actualizar la variable global de porcentaje
    volume_factor = (float)current_volume_percentage / 100.0f;
    ESP_LOGI(TAG, "Volume set to %u%% (factor: %.2f)", current_volume_percentage, volume_factor);
}

void play_wav(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s, errno: %s", filepath, strerror(errno));
        return;
    }
    ESP_LOGI(TAG, "Opened WAV file: %s", filepath);

    wav_header_t wav_header;
    if (fread(&wav_header, 1, sizeof(wav_header_t) - 8, fp) != (sizeof(wav_header_t) - 8)) {
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

    if (wav_header.fmt_chunk_size > 16) {
        ESP_LOGI(TAG, "fmt_chunk_size is %" PRIu32 ", skipping %" PRIu32 " extra bytes.", wav_header.fmt_chunk_size, wav_header.fmt_chunk_size - 16);
        if (fseek(fp, wav_header.fmt_chunk_size - 16, SEEK_CUR) != 0) {
            ESP_LOGE(TAG, "Failed to seek past extra fmt data.");
            fclose(fp);
            return;
        }
    }

    while (1) {
        if (fread(wav_header.data_header, 1, 4, fp) != 4) {
            ESP_LOGE(TAG, "Failed to read chunk ID while searching for 'data'.");
            fclose(fp);
            return;
        }
        if (fread(&wav_header.data_size, 1, 4, fp) != 4) {
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
            break;
        }
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

    if (wav_header.bits_per_sample != 8 && wav_header.bits_per_sample != 16 &&
        wav_header.bits_per_sample != 24 && wav_header.bits_per_sample != 32) {
        ESP_LOGE(TAG, "Unsupported bits per sample: %" PRIu16 ". Only 8, 16, 24, or 32-bit supported by I2S driver.", wav_header.bits_per_sample);
        fclose(fp);
        return;
    }

    if (tx_chan != NULL) {
        ESP_LOGI(TAG, "Disabling and deleting previous I2S TX channel.");
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }

    ESP_LOGI(TAG, "Configuring I2S TX channel...");
    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 256,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wav_header.sample_rate);

    i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)wav_header.bits_per_sample,
                                                                   (wav_header.num_channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);

    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_BCK_IO,
        .ws = I2S_WS_IO,
        .dout = I2S_DO_IO,
        .din = I2S_GPIO_UNUSED,
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

    int buffer_size = wav_header.block_align > 0 ? wav_header.block_align * 256 : 2048;
    if (buffer_size > 4096) buffer_size = 4096;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer of size %d", buffer_size);
        fclose(fp);
        if (tx_chan) {
            i2s_channel_disable(tx_chan);
            i2s_del_channel(tx_chan);
            tx_chan = NULL;
        }
        return;
    }

    size_t bytes_read;
    size_t bytes_written;
    uint32_t total_bytes_played = 0;

    ESP_LOGI(TAG, "Starting playback... Buffer size: %d bytes. Current Volume: %u%%", buffer_size, current_volume_percentage);
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

        // Aplicar factor de volumen global
        if (volume_factor < 0.999f) { // Evitar procesamiento si el volumen es 100%
            if (wav_header.bits_per_sample == 16) {
                int16_t *samples = (int16_t *)buffer;
                size_t num_samples = bytes_read / sizeof(int16_t);
                for (size_t i = 0; i < num_samples; i++) {
                    samples[i] = (int16_t)((float)samples[i] * volume_factor);
                }
            } else if (wav_header.bits_per_sample == 8) {
                uint8_t *samples = (uint8_t *)buffer;
                size_t num_samples = bytes_read / sizeof(uint8_t);
                for (size_t i = 0; i < num_samples; i++) {
                    int16_t sample_signed = (int16_t)samples[i] - 128;
                    sample_signed = (int16_t)((float)sample_signed * volume_factor);
                    if (sample_signed > 127) sample_signed = 127;
                    else if (sample_signed < -128) sample_signed = -128;
                    samples[i] = (uint8_t)(sample_signed + 128);
                }
            }
        }

        esp_err_t write_err = i2s_channel_write(tx_chan, buffer, bytes_read, &bytes_written, portMAX_DELAY);
        if (write_err != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(write_err));
            break;
        }
        if (bytes_read != bytes_written) {
            ESP_LOGW(TAG, "I2S write underrun or mismatch: read %zu, wrote %zu", bytes_read, bytes_written);
        }
        total_bytes_played += bytes_written;
    }
    uint64_t end_time = esp_timer_get_time();
    float duration_sec = (end_time - start_time) / 1000000.0f;

    ESP_LOGI(TAG, "Playback finished. Total bytes played: %" PRIu32 " / %" PRIu32 ". Duration: %.2f s",
            total_bytes_played, wav_header.data_size, duration_sec);

    vTaskDelay(pdMS_TO_TICKS(100));

    if(tx_chan) ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));

    free(buffer);
    fclose(fp);
}

// ***** NUEVA FUNCIÓN: Tarea para controlar el volumen con botones *****
static void volume_control_task(void *arg) {
    // Configurar los pines de los botones
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE; // Sin interrupciones por ahora
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_VOLUME_UP) | (1ULL << GPIO_VOLUME_DOWN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Habilitar pull-up interno
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Volume control task started. UP: GPIO%d, DOWN: GPIO%d", GPIO_VOLUME_UP, GPIO_VOLUME_DOWN);

    TickType_t last_press_time_up = 0;
    TickType_t last_press_time_down = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        // Botón Subir Volumen (activo bajo debido al pull-up)
        if (gpio_get_level(GPIO_VOLUME_UP) == 0) {
            if ((now - last_press_time_up) * portTICK_PERIOD_MS > DEBOUNCE_TIME_MS) {
                ESP_LOGD(TAG, "Volume UP button pressed");
                uint8_t new_volume = current_volume_percentage + VOLUME_STEP;
                if (new_volume > 100) new_volume = 100;
                if (new_volume != current_volume_percentage) {
                    set_volume(new_volume);
                }
                last_press_time_up = now;
            }
        }


        // Botón Bajar Volumen (activo bajo debido al pull-up)
        if (gpio_get_level(GPIO_VOLUME_DOWN) == 0) {
            if ((now - last_press_time_down) * portTICK_PERIOD_MS > DEBOUNCE_TIME_MS) {
                ESP_LOGD(TAG, "Volume DOWN button pressed");
                int temp_volume = (int)current_volume_percentage - VOLUME_STEP;
                uint8_t new_volume;
                if (temp_volume < 0) new_volume = 0;
                else new_volume = (uint8_t)temp_volume;

                if (new_volume != current_volume_percentage) {
                    set_volume(new_volume);
                }
                last_press_time_down = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 Audio Player from SD Card (New I2S API)");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    set_volume(current_volume_percentage);

    init_sdcard();

    // ***** NUEVO: Crear la tarea para el control de volumen, AUMENTAR STACK *****
    xTaskCreate(volume_control_task, "volume_task", 3072, NULL, 5, NULL); // Stack increased to 3072 bytes


    if (card != NULL) {
        list_sdcard_files(MOUNT_POINT);
        play_wav(MOUNT_POINT "/SONG.WAV");
    } else {
        ESP_LOGE(TAG, "SD card not initialized. Cannot play audio.");
    }

    if (tx_chan != NULL) {
        ESP_LOGI(TAG, "Cleaning up I2S TX channel.");
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }

    ESP_LOGI(TAG, "Program ended. ESP will idle or you can add a loop/restart.");
    while(1) {
        ESP_LOGI(TAG, "Main task idling. Current volume: %u%%", current_volume_percentage);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}