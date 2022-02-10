#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "utils.h"

static const char *TAG_UTILS = "mihome_hub_utils";

static void wifi_scanner_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
   if (event_id == SYSTEM_EVENT_SCAN_DONE) {
      uint16_t apCount = 0;
      esp_wifi_scan_get_ap_num(&apCount);
      if (apCount == 0) { }
      wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
      ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));
      int i;
      ESP_LOGI(TAG_UTILS, "======================================================================");
      ESP_LOGI(TAG_UTILS, "             SSID             |    RSSI    |           AUTH           ");
      ESP_LOGI(TAG_UTILS, "======================================================================");
      for (i=0; i<apCount; i++) {
         char *authmode;
         switch(list[i].authmode) {
            case WIFI_AUTH_OPEN:
               authmode = "WIFI_AUTH_OPEN";
               break;
            case WIFI_AUTH_WEP:
               authmode = "WIFI_AUTH_WEP";
               break;
            case WIFI_AUTH_WPA_PSK:
               authmode = "WIFI_AUTH_WPA_PSK";
               break;
            case WIFI_AUTH_WPA2_PSK:
               authmode = "WIFI_AUTH_WPA2_PSK";
               break;
            case WIFI_AUTH_WPA_WPA2_PSK:
               authmode = "WIFI_AUTH_WPA_WPA2_PSK";
               break;
            default:
               authmode = "Unknown";
               break;
         }
         ESP_LOGI(TAG_UTILS, "%26.26s    |    % 4d    |    %22.22s",list[i].ssid, list[i].rssi, authmode);
      }
      free(list);
   }
}

/*
void set_logging_levels() {
  if (WIFI_CORE_LOGGING != 0) {
    esp_log_level_set("wifi", ESP_LOG_NONE);
  }
}
*/

void wifi_scanner() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_event_handler_instance_t event_instance_wifi_scanner;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_scanner_handler, NULL, &event_instance_wifi_scanner));

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, event_instance_wifi_scanner));
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}
