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

static const char *TAG_CORE = "mihome_hub_core";

void system_monitoring_task(void *pvParameter) {
    for(;;) {
          vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void app_main(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("[Target: %s] - %d CPU Core(s), WiFi%s%s, SR %d, %dMB %s Flash\n", CONFIG_IDF_TARGET, chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "", chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
    

    /* 
        WiFi Debugging
        wifi_scanner();
    */

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG_CORE,"ESP_BT_MANAGER_START");

    bt_manager_start();

    /* Create FreeRTOS Monitoring Task */
    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);
}