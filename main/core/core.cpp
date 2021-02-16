#include "core/core.h"
#include "core/sys/log/log.h"
#include "core/sys/utils/utils.h"
#include "core/sys/ble/ble_manager.h"
#include "core/sys/wifi/wifi_manager.h"
#include "core/application/service/service.h"

using namespace core::sys;
using namespace core::application;
using namespace core::sys::ble::manager;
using namespace core::sys::wifi::manager;

namespace core
{
	static const char TAG[] = "mihome_esp32_core";

	struct mihome_settings_t mihome_settings = { "", "" };

	void system_monitoring_task(void *pvParameter) {
	    for(;;) {
	          vTaskDelay(pdMS_TO_TICKS(60000));
	    }
	}

	int init_nvs_state() {
		// initialize NVS
	    esp_err_t err = nvs_flash_init();
	    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	        ESP_ERROR_CHECK(nvs_flash_erase());
	        err = nvs_flash_init();
	    }
	    ESP_ERROR_CHECK(err);

	    // boot config check
	    nvs_handle_t handle;
	    int8_t nvs_init_state = 0;
	    err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);

	    if (err != ESP_OK) {
	        ESP_LOGE(TAG, "Error (%s) opening NVS handler on Boot!", esp_err_to_name(err));
	    } else {
	        int32_t is_configured = 0; // value will default to 0, if not set yet in NVS
	        err = nvs_get_i32(handle, MIHOME_STORAGE_IS_CONFIG, &is_configured);
	        switch (err) {
	            case ESP_OK:
	                //ESP_LOGI(TAG, "configured = %d\n", is_configured);
	                if (is_configured) {
	                    // begin Wifi manager
	                    nvs_init_state = 2;
	                } else {
	                    // start BLE gatt service setup.
	                    nvs_init_state = 3;
	                }
	                break;
	            case ESP_ERR_NVS_NOT_FOUND:
	                is_configured = 0;
	                err = nvs_set_i32(handle, MIHOME_STORAGE_IS_CONFIG, is_configured);
	                err = nvs_commit(handle);
	                nvs_init_state = 1;
	                break;
	            default:
	                ESP_LOGE(TAG, "Error (%s) reading", esp_err_to_name(err));
	        }

	        // grabs the mihome cloud size
	        size_t mihome_settings_size = sizeof(mihome_settings);
	        err = nvs_get_blob(handle, MIHOME_STORAGE_SETTINGS, &mihome_settings, &mihome_settings_size);
	        switch (err) {
	            case ESP_OK:
	                break;
	            case ESP_ERR_NVS_NOT_FOUND:
	                err = nvs_set_blob(handle, MIHOME_STORAGE_SETTINGS, &mihome_settings, mihome_settings_size);
	                err = nvs_commit(handle);
	                nvs_init_state = 1;
	                break;
	            default:
	                ESP_LOGE(TAG, "Error (%s) reading", esp_err_to_name(err));
	        }
	    }

	    // close NVS handler
	    nvs_close(handle);

	    return nvs_init_state;
	}

	void cb_connection_established(void *pvParameter) {
		wifi_ap_record_t wifidata;
	    if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
	        ESP_LOGI(TAG, "wifi_rssi: %d", wifidata.rssi);
	    }

		utils::update_status_led(HEX_COLOR_GREEN);
		service::start_service();
	}

 	void start_core() {
 		// get chip info
 		esp_chip_info_t chip_info;
	    esp_chip_info(&chip_info);

	    ESP_LOGI(TAG, "MiHome ESP32 Base %s (%s chip, %d CPU cores, WiFi%s%s, revision %d, %dMB %s flash, free heap: %d)",
	            MIHOME_VERSION,
	            CONFIG_IDF_TARGET,
	            chip_info.cores,
	            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
	            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
	            chip_info.revision,
	            spi_flash_get_chip_size() / (1024 * 1024),
	            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external",
	            esp_get_free_heap_size());

	    utils::set_logging_levels();

	    // init the color status white
	    utils::init_status_led(MIHOME_LED_PIN);
	    utils::update_status_led(HEX_COLOR_BLACK);

	    // init the nvs and check state
	    int init_state = init_nvs_state();
	    switch (init_state) {
	    	case 1: {
	    		vTaskDelay(1000 / portTICK_PERIOD_MS);
        		esp_restart();
        		break;
	    	}
	    	case 2: {
	    		// start the wifi manager / tcp stack
	    		wifi::manager::start();
	    		wifi::manager::wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_established);
	    		break;
	    	}
	    	case 3: {
	    		utils::update_status_led(HEX_COLOR_BLUE);
	    		// start the ble manager for pair to wifi network / cloud. 
	    		ble::manager::start();
	    		break;
	    	}
	    	default:
	    		ESP_LOGE(TAG, "");
	    		break;
	    }

	    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);

	    /* detect restart button. */
	    gpio_reset_pin(gpio_num_t(RESTART_GPIO));
	    gpio_set_direction(gpio_num_t(RESTART_GPIO), GPIO_MODE_INPUT);

	    /* settle 1 seconds */
	    vTaskDelay(1000 / portTICK_PERIOD_MS);

	    while (1) {
	        /* check status and resets if needed */
	        if (gpio_get_level(gpio_num_t(RESTART_GPIO)) == 0) {
	            utils::update_status_led(HEX_COLOR_RED);
	            vTaskDelay(3000 / portTICK_PERIOD_MS);

	            if (gpio_get_level(gpio_num_t(RESTART_GPIO)) == 0) {
	                esp_err_t err = nvs_flash_erase();
	                ESP_ERROR_CHECK(err);

	                utils::update_status_led(HEX_COLOR_BLACK);
	                vTaskDelay(3000 / portTICK_PERIOD_MS);

	                esp_restart();
	            } else {

	            }
	        }
	        vTaskDelay(200 / portTICK_PERIOD_MS);
	    }
 	}
}