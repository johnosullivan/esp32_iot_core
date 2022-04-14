#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"

#include "wifi_manager.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 4
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* objects used to manipulate the main queue of events */
QueueHandle_t wifi_manager_queue;
SemaphoreHandle_t wifi_manager_json_mutex = NULL;
SemaphoreHandle_t wifi_manager_sta_ip_mutex = NULL;

char *wifi_manager_sta_ip = NULL;
uint16_t ap_num = MAX_AP_NUM;
wifi_ap_record_t *accessp_records;
char *accessp_json = NULL;
char *ip_info_json = NULL;
wifi_config_t* wifi_manager_config_sta = NULL;

/* @brief Array of callback function pointers */
void (**cb_ptr_arr)(void*) = NULL;

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "mihome_esp32_wifi_manager";

/* @brief task handle for the main wifi_manager task */
static TaskHandle_t task_wifi_manager = NULL;

/* @brief netif object for the STATION */
static esp_netif_t* esp_netif_sta = NULL;

/* @brief netif object for the ACCESS POINT */
static esp_netif_t* esp_netif_ap = NULL;

struct wifi_settings_t wifi_settings = {
	.ap_channel = 1,
	.ap_bandwidth = WIFI_BW_HT20,
	.sta_only = 1,
	.sta_power_save = WIFI_PS_NONE,
	.sta_static_ip = 0,
};

const char wifi_manager_nvs_namespace[] = "espwifimgr";

static EventGroupHandle_t wifi_manager_event_group;

/* @brief indicate that the ESP32 is currently connected. */
const int WIFI_MANAGER_WIFI_CONNECTED_BIT = BIT0;

const int WIFI_MANAGER_AP_STA_CONNECTED_BIT = BIT1;

/* @brief Set automatically once the SoftAP is started */
const int WIFI_MANAGER_AP_STARTED_BIT = BIT2;

/* @brief When set, means a client requested to connect to an access point.*/
const int WIFI_MANAGER_REQUEST_STA_CONNECT_BIT = BIT3;

/* @brief This bit is set automatically as soon as a connection was lost */
const int WIFI_MANAGER_STA_DISCONNECT_BIT = BIT4;

/* @brief When set, means the wifi manager attempts to restore a previously saved connection at startup. */
const int WIFI_MANAGER_REQUEST_RESTORE_STA_BIT = BIT5;

/* @brief When set, means a client requested to disconnect from currently connected AP. */
const int WIFI_MANAGER_REQUEST_WIFI_DISCONNECT_BIT = BIT6;

/* @brief When set, means a scan is in progress */
const int WIFI_MANAGER_SCAN_BIT = BIT7;

/* @brief When set, means user requested for a disconnect */
const int WIFI_MANAGER_REQUEST_DISCONNECT_BIT = BIT8;

/**
 * @brief Standard WiFi event handler
 */
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT){
		switch(event_id){
			case WIFI_EVENT_WIFI_READY:
				ESP_LOGI(TAG, "WIFI_EVENT_WIFI_READY");
				break;

			case WIFI_EVENT_SCAN_DONE:
				ESP_LOGD(TAG, "WIFI_EVENT_SCAN_DONE");
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
				break;

			/* If esp_wifi_start() returns ESP_OK and the current Wi-Fi mode is Station or AP+Station, then this event will
			* arise. Upon receiving this event, the event task will initialize the LwIP network interface (netif).
			* Generally, the application event callback needs to call esp_wifi_connect() to connect to the configured AP. */
			case WIFI_EVENT_STA_START:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
				break;

			/* If esp_wifi_stop() returns ESP_OK and the current Wi-Fi mode is Station or AP+Station, then this event will arise.
			* Upon receiving this event, the event task will release the station’s IP address, stop the DHCP client, remove
			* TCP/UDP-related connections and clear the LwIP station netif, etc. The application event callback generally does
			* not need to do anything. */
			case WIFI_EVENT_STA_STOP:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
				break;

			case WIFI_EVENT_STA_CONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
				break;

			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_SCAN_BIT);
				break;

			case WIFI_EVENT_STA_AUTHMODE_CHANGE:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE");
				break;

			case WIFI_EVENT_AP_START:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED_BIT);
				break;

			case WIFI_EVENT_AP_STOP:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
				break;

			case WIFI_EVENT_AP_STACONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
				break;

			case WIFI_EVENT_AP_STADISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
				break;

			case WIFI_EVENT_AP_PROBEREQRECVED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_PROBEREQRECVED");
				break;
		} /* end switch */
	} else if(event_base == IP_EVENT) {

		switch(event_id){
			case IP_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
				ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
				wifi_manager_send_message(EVENT_STA_GOT_IP, (void*)event->ip_info.ip.addr);
				break;

			/* This event arises when the IPV6 SLAAC support auto-configures an address for the ESP32, or when this address changes.
			* The event means that everything is ready and the application can begin its tasks (e.g., creating sockets). */
			case IP_EVENT_GOT_IP6:
				ESP_LOGI(TAG, "IP_EVENT_GOT_IP6");
				break;

			case IP_EVENT_STA_LOST_IP:
				ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
				break;

			}
	}

}

BaseType_t wifi_manager_send_message(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend(wifi_manager_queue, &msg, portMAX_DELAY);
}

bool wifi_manager_lock_sta_ip_string(TickType_t xTicksToWait) {
	if(wifi_manager_sta_ip_mutex){
		if( xSemaphoreTake( wifi_manager_sta_ip_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		} else{
			return false;
		}
	} else{
		return false;
	}
}

void wifi_manager_unlock_sta_ip_string() {
	xSemaphoreGive(wifi_manager_sta_ip_mutex);
}


void wifi_manager_safe_update_sta_ip_string(uint32_t ip) {
	/*if(wifi_manager_lock_sta_ip_string(portMAX_DELAY)){
		esp_ip4_addr_t ip4;
		ip4.addr = ip;

		//char* str_ip = (char*)malloc(sizeof(char) * IP4ADDR_STRLEN_MAX);
		//char str_ip[IP4ADDR_STRLEN_MAX];
		//esp_ip4addr_ntoa(&ip4, str_ip, IP4ADDR_STRLEN_MAX);

		strcpy(wifi_manager_sta_ip, str_ip);

		//free(str_ip);

		ESP_LOGI(TAG, "STA IP = %s", wifi_manager_sta_ip);

		wifi_manager_unlock_sta_ip_string();
	}*/
}

void wifi_manager_start() {
	BaseType_t xStatus;
	queue_message msg;

	wifi_manager_event_group = xEventGroupCreate();
	wifi_manager_queue = xQueueCreate(3, sizeof(queue_message));

	wifi_manager_sta_ip_mutex = xSemaphoreCreateMutex();
	wifi_manager_sta_ip = (char*)malloc(sizeof(char) * IP4ADDR_STRLEN_MAX);
	wifi_manager_safe_update_sta_ip_string((uint32_t)0);

	/* malloc the function callback */
	cb_ptr_arr = malloc(sizeof(sizeof(void(*)(void*)))*MESSAGE_CODE_COUNT);
	for(int i=0; i<MESSAGE_CODE_COUNT; i++){
		cb_ptr_arr[i] = NULL;
	}

	/* config default parameters */
	ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	/* event handler for the connection */
    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_wifi_event));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_ip_event));

	/* start wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);
}

void wifi_manager_destroy(){
	vTaskDelete(task_wifi_manager);
	task_wifi_manager = NULL;

	/* heap buffers */
	free(accessp_records);
	accessp_records = NULL;
	free(accessp_json);
	accessp_json = NULL;
	free(ip_info_json);
	ip_info_json = NULL;
	free(wifi_manager_sta_ip);
	wifi_manager_sta_ip = NULL;
	if(wifi_manager_config_sta){
		free(wifi_manager_config_sta);
		wifi_manager_config_sta = NULL;
	}

	/* RTOS objects */
	vSemaphoreDelete(wifi_manager_json_mutex);
	wifi_manager_json_mutex = NULL;
	vSemaphoreDelete(wifi_manager_sta_ip_mutex);
	wifi_manager_sta_ip_mutex = NULL;
	vEventGroupDelete(wifi_manager_event_group);
	wifi_manager_event_group = NULL;
	vQueueDelete(wifi_manager_queue);
	wifi_manager_queue = NULL;


}

void wifi_manager_set_callback(message_code_t message_code, void (*func_ptr)(void*) ){
	if(cb_ptr_arr && message_code < MESSAGE_CODE_COUNT){
		cb_ptr_arr[message_code] = func_ptr;
	}
}

esp_netif_t* wifi_manager_get_esp_netif_ap(){
	return esp_netif_ap;
}

esp_netif_t* wifi_manager_get_esp_netif_sta(){
	return esp_netif_sta;
}

void wifi_manager(void *pvParameters){
	BaseType_t xStatus;
	EventBits_t uxBits;
	queue_message msg;

	wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Look Ma No Wires",
			.password = "Mogilska12!",
	     	.threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

	wifi_manager_send_message(WM_LOAD_AND_RESTORE_STA, NULL);
	for(;;){
		xStatus = xQueueReceive( wifi_manager_queue, &msg, portMAX_DELAY );
		if(xStatus == pdPASS){
			switch(msg.code){
				case EVENT_SCAN_DONE:
					ESP_LOGD(TAG, "EVENT_SCAN_DONE");

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_START_WIFI_SCAN:
					ESP_LOGD(TAG, "WM_START_WIFI_SCAN");

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_LOAD_AND_RESTORE_STA:
					ESP_LOGI(TAG, "WM_LOAD_AND_RESTORE_STA");

					wifi_manager_send_message(WM_CONNECT_STA, (void*)CONNECTION_REQUEST_RESTORE_CONNECTION);
					
					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_CONNECT_STA:
					ESP_LOGI(TAG, "WM_CONNECT_STA");

					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

					ESP_ERROR_CHECK(esp_wifi_connect());

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case EVENT_STA_DISCONNECTED:
					ESP_LOGI(TAG, "EVENT_STA_DISCONNECTED %d", (uint32_t)msg.param);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_START_AP:
					ESP_LOGI(TAG, "WM_START_AP");

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case EVENT_STA_GOT_IP:
					ESP_LOGI(TAG, "EVENT_STA_GOT_IP");

					/* reset connection requests bits */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;
				case WM_DISCONNECT_STA:
					ESP_LOGI(TAG, "WM_DISCONNECT_STA");

					/* precise this is coming from a user request */
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

					/* order wifi discconect */
					ESP_ERROR_CHECK(esp_wifi_disconnect());

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				default:
					break;

				} 
		}  
	}
	/* clean up the wifi manager task */
	vTaskDelete(NULL);
}
