#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "sdkconfig.h"

#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/queue.h"

#include "utils.h"
#include "iridium.h"
#include "bt_manager.h"
#include "wifi_manager.h"

#include <pthread.h>

static const char *TAG_CORE = "esp32_hub_core";

void system_monitoring_task(void *pvParameter) {
    ESP_LOGI(TAG_CORE, "System [system_monitoring_task]");
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void cb_satcom(iridium_t* satcom, iridium_command_t command, iridium_status_t status) { 
    if (status == SAT_OK) {
        switch (command) {
            case AT_CSQ:
                ESP_LOGI(TAG_CORE, "Signal Strength [0-5]: %d", satcom->signal_strength);
                break;
            case AT_CGMM:
                ESP_LOGI(TAG_CORE, "Model Identification: %s", satcom->model_identification);
                break;
            case AT_CGMI:
                ESP_LOGI(TAG_CORE, "Manufacturer Identification: %s", satcom->manufacturer_identification);
                break;
            default:
                break;
        }
    }
}

void cb_message(iridium_t* satcom, char* data) { 
    ESP_LOGI(TAG_CORE, "CALLBACK[INCOMING] %s", data);

    if (strcmp ("PING", data) == 0) {
        iridium_result_t r1 = iridium_tx_message(satcom, "PONG");
        if (r1.status != SAT_OK) {
            ESP_LOGI(TAG_CORE, "R[%d] TX Failed!", r1.status);
        } else {
            ESP_LOGI(TAG_CORE, "R[%d] = %s", r1.status, r1.result);
        }
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
    printf("[Target: %s] - %d CPU Core(s), WiFi%s%s, SR %d, %dMB %s Flash\n", 
            CONFIG_IDF_TARGET, chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", 
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "", chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024), 
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
    
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

    /* Pull NVS State + Start BLE/WiFi Managers */
    int8_t  restart_flag = 0x00;
    int32_t configured   = 0x00;

    if (err != ESP_OK) {
        ESP_LOGE(TAG_CORE, "ERROR: (%s) NVS", esp_err_to_name(err));
    } else {
        err = nvs_get_i32(handle, MIHOME_STORAGE_IS_CONFIG, &configured);
        switch (err) {
            case ESP_OK:
                ESP_LOGD(TAG_CORE, "STORAGE_IS_CONFIG = %d\n", configured);
                if (configured) {
                    /* Start WiFi Manager + Callback (EVENT_STA_GOT_IP) */
                    wifi_manager_start();
                    wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_established);
                } else {
                    /* Start BLE Manager / GATTS Service */
                    bt_manager_start();
                }
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                /* Set NVS Default Configs */
                err = nvs_set_i32(handle, MIHOME_STORAGE_IS_CONFIG, 0x00);
                ESP_ERROR_CHECK(err);
                /* Commit NVS Changes */
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

    /* Restart Flag Equals 0x01 */
    if (restart_flag) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }



    /* Configuration Iridium SatCom */
    iridium_t *satcom = iridium_default_configuration();
    satcom->callback = &cb_satcom;
    satcom->message_callback = &cb_message;
    /* UART Port Configuration */
    satcom->uart_number = UART_NUM_1;
    satcom->uart_txn_number = GPIO_NUM_17;
    satcom->uart_rxd_number = GPIO_NUM_18;
    satcom->uart_rts_number = UART_PIN_NO_CHANGE;
    satcom->uart_cts_number = UART_PIN_NO_CHANGE;
    
    /* Initialized */
    if (iridium_config(satcom) == SAT_OK) {
        ESP_LOGI(TAG_CORE, "Iridium Modem [Initialized]");
    }

    /* Allow Ring Triggers */
    iridium_result_t ring = iridium_config_ring(satcom, true);
    if (ring.status == SAT_OK) {
        ESP_LOGI(TAG_CORE, "Iridium Modem [Ring Enabled]");
    }
   
    /* Create FreeRTOS Monitoring Task */
    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);

    for(;;) {
        iridium_result_t r1 = iridium_send(satcom, AT_CSQ, "", true, 500);
        if (r1.status == SAT_OK) {
            ESP_LOGI(TAG_CORE, "R[%d] = %s", r1.status, r1.result);
        }                         
        vTaskDelay(pdMS_TO_TICKS(120000));
    }
}
