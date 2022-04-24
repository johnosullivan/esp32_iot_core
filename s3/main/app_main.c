#include <stdio.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_spi_flash.h"

#include "utils.h"

#include "nvs.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "bt_manager.h"
#include "wifi_manager.h"

static const char *TAG_CORE = "mihome_hub_core";

void system_monitoring_task(void *pvParameter) {
    for(;;) {
          vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void cb_connection_established(void *pvParameter) {
	ESP_LOGI(TAG_CORE, "connection_established");
}

void app_main(void)
{
    /* Print Chip Information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("[Target: %s] - %d CPU Core(s), WiFi%s%s, SR %d, %dMB %s Flash\n", CONFIG_IDF_TARGET, chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "", chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
    
    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Open NVS Storage Namespace */
    nvs_handle_t handle;
    err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    ESP_ERROR_CHECK(err);

    int8_t  restart_flag = 0x00;
    int32_t configured = 0x00;
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CORE, "ERROR: (%s) NVS", esp_err_to_name(err));
    } else {
        err = nvs_get_i32(handle, MIHOME_STORAGE_IS_CONFIG, &configured);
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG_CORE, "configured = %d\n", configured);
                if (configured) {
                    ESP_LOGI(TAG_CORE, "connect wifi\n");
                    wifi_manager_start();
                    wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_established);
                } else {
                    /* Start BLE Manager / GATTS Service */
                    bt_manager_start();
                }
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                err = nvs_set_i32(handle, MIHOME_STORAGE_IS_CONFIG, 0x00);
                ESP_ERROR_CHECK(err);
                err = nvs_commit(handle);
                ESP_ERROR_CHECK(err);
                restart_flag = 0x01;
                break;
            default:
                ESP_LOGE(TAG_CORE, "ERROR: (%s)", esp_err_to_name(err));
        }
    }
    /* Close NVS Handle */
    nvs_close(handle);

    if (restart_flag) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    /* Create FreeRTOS Monitoring Task */
    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);
}
