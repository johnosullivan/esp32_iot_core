#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_system.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
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

#include "core/sys/wifi/wifi_manager.h"

namespace core::sys::wifi::manager
{
	/* objects used to manipulate the main queue of events */
	QueueHandle_t wifi_manager_queue;

	SemaphoreHandle_t wifi_manager_json_mutex = NULL;
	SemaphoreHandle_t wifi_manager_sta_ip_mutex = NULL;

	char *wifi_manager_sta_ip = NULL;
	char *accessp_json = NULL;
	char *ip_info_json = NULL;

	uint16_t ap_num = MAX_AP_NUM;

	wifi_ap_record_t *accessp_records;
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

	/**
	 * The actual WiFi settings in use
	 */
	struct wifi_settings_t wifi_settings = { 
		DEFAULT_AP_SSID, 
		DEFAULT_AP_PASSWORD, 
		DEFAULT_AP_CHANNEL, 
		DEFAULT_AP_SSID_HIDDEN, 
		DEFAULT_AP_BANDWIDTH, 
		DEFAULT_STA_ONLY, 
		DEFAULT_STA_POWER_SAVE, 
		0,
		{}
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

	void wifi_manager_scan_async() {
		wifi_manager_send_message(WM_START_WIFI_SCAN, NULL);
	}

	void wifi_manager_disconnect_async() {
		wifi_manager_send_message(WM_DISCONNECT_STA, NULL);
	}

	void start() {
		/* initialize flash memory */
		nvs_flash_init();

		/* memory allocation */
		wifi_manager_queue = xQueueCreate(3, sizeof(queue_message));
		wifi_manager_json_mutex = xSemaphoreCreateMutex();

		accessp_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * MAX_AP_NUM);
		accessp_json = (char*)malloc(MAX_AP_NUM * JSON_ONE_APP_SIZE + 4); /* 4 bytes for json encapsulation of "[\n" and "]\0" */
		
		wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
		memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
		memset(&wifi_settings.sta_static_ip_config, 0x00, sizeof(esp_netif_ip_info_t));

		cb_ptr_arr = (void (**)(void*))malloc(sizeof(void(*)(void*)) * MESSAGE_CODE_COUNT);
		for(int i=0; i<MESSAGE_CODE_COUNT; i++){
			cb_ptr_arr[i] = NULL;
		}

		wifi_manager_sta_ip_mutex = xSemaphoreCreateMutex();
		wifi_manager_sta_ip = (char*)malloc(sizeof(char) * IP4ADDR_STRLEN_MAX);
		wifi_manager_event_group = xEventGroupCreate();

		xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);
	}

	bool wifi_manager_fetch_wifi_sta_config(){
		nvs_handle handle;
		esp_err_t esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &handle);

		if(esp_err == ESP_OK){
			if(wifi_manager_config_sta == NULL){
				wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
			}
			memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));

			/* allocate buffer */
			size_t sz = sizeof(wifi_settings);
			uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);
			memset(buff, 0x00, sizeof(sz));

			/* ssid */
			sz = sizeof(wifi_manager_config_sta->sta.ssid);
			esp_err = nvs_get_blob(handle, "ssid", buff, &sz);
			if(esp_err != ESP_OK){
				free(buff);
				return false;
			}
			memcpy(wifi_manager_config_sta->sta.ssid, buff, sz);

			/* password */
			sz = sizeof(wifi_manager_config_sta->sta.password);
			esp_err = nvs_get_blob(handle, "password", buff, &sz);
			if(esp_err != ESP_OK){
				free(buff);
				return false;
			}
			memcpy(wifi_manager_config_sta->sta.password, buff, sz);

			/* settings */
			sz = sizeof(wifi_settings);
			esp_err = nvs_get_blob(handle, "settings", buff, &sz);
			if(esp_err != ESP_OK){
				free(buff);
				return false;
			}
			memcpy(&wifi_settings, buff, sz);

			free(buff);
			nvs_close(handle);

			return wifi_manager_config_sta->sta.ssid[0] != '\0';
		} else{
			return false;
		}
	}

	static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
		if (event_base == WIFI_EVENT) {
			switch(event_id){
				/* The Wi-Fi driver will never generate this event, which, as a result, can be ignored by the application event
				 * callback. This event may be removed in future releases. */
				case WIFI_EVENT_WIFI_READY: {
					ESP_LOGI(TAG, "WIFI_EVENT_WIFI_READY");
					break;
				}
				default:
					break;

			}
		} else if(event_base == IP_EVENT) {
			switch(event_id) {
				/* This event arises when the DHCP client successfully gets the IPV4 address from the DHCP server,
				 * or when the IPV4 address is changed. The event means that everything is ready and the application can begin
				 * its tasks (e.g., creating sockets).
				 * The IPV4 may be changed because of the following reasons:
				 *    The DHCP client fails to renew/rebind the IPV4 address, and the station’s IPV4 is reset to 0.
				 *    The DHCP client rebinds to a different address.
				 *    The static-configured IPV4 address is changed.
				 * Whether the IPV4 address is changed or NOT is indicated by field ip_change of ip_event_got_ip_t.
				 * The socket is based on the IPV4 address, which means that, if the IPV4 changes, all sockets relating to this
				 * IPV4 will become abnormal. Upon receiving this event, the application needs to close all sockets and recreate
				 * the application when the IPV4 changes to a valid one. */
				case IP_EVENT_STA_GOT_IP:
					ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
			        xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
			        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			        wifi_manager_send_message(EVENT_STA_GOT_IP, (void*)event->ip_info.ip.addr);
					break;
			}
		}

	}


	void wifi_manager_filter_unique( wifi_ap_record_t * aplist, uint16_t * aps) {
		int total_unique;
		wifi_ap_record_t * first_free;
		total_unique=*aps;

		first_free=NULL;

		for(int i=0; i<*aps-1;i++) {
			wifi_ap_record_t * ap = &aplist[i];

			/* skip the previously removed APs */
			if (ap->ssid[0] == 0) continue;

			/* remove the identical SSID+authmodes */
			for(int j=i+1; j<*aps;j++) {
				wifi_ap_record_t * ap1 = &aplist[j];
				if ( (strcmp((const char *)ap->ssid, (const char *)ap1->ssid)==0) &&
				     (ap->authmode == ap1->authmode) ) { /* same SSID, different auth mode is skipped */
					/* save the rssi for the display */
					if ((ap1->rssi) > (ap->rssi)) ap->rssi=ap1->rssi;
					/* clearing the record */
					memset(ap1,0, sizeof(wifi_ap_record_t));
				}
			}
		}
		/* reorder the list so APs follow each other in the list */
		for(int i=0; i<*aps;i++) {
			wifi_ap_record_t * ap = &aplist[i];
			/* skipping all that has no name */
			if (ap->ssid[0] == 0) {
				/* mark the first free slot */
				if (first_free==NULL) first_free=ap;
				total_unique--;
				continue;
			}
			if (first_free!=NULL) {
				memcpy(first_free, ap, sizeof(wifi_ap_record_t));
				memset(ap,0, sizeof(wifi_ap_record_t));
				/* find the next free slot */
				for(int j=0; j<*aps;j++) {
					if (aplist[j].ssid[0]==0) {
						first_free=&aplist[j];
						break;
					}
				}
			}
		}
		/* update the length of the list */
		*aps = total_unique;
	}

	BaseType_t wifi_manager_send_message(message_code_t code, void *param) {
		queue_message msg;
		msg.code = code;
		msg.param = param;
		return xQueueSend(wifi_manager_queue, &msg, portMAX_DELAY);
	}

	BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param){
		queue_message msg;
		msg.code = code;
		msg.param = param;
		return xQueueSendToFront(wifi_manager_queue, &msg, portMAX_DELAY);
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

	wifi_config_t* wifi_manager_get_wifi_sta_config(){
		return wifi_manager_config_sta;
	}

	void wifi_manager(void *pvParameters) {
		queue_message msg;
		BaseType_t xStatus;
		EventBits_t uxBits;
		//uint8_t	retries = 0;


		/* initialize the tcp stack */
		ESP_ERROR_CHECK(esp_netif_init());

		/* event loop for the wifi driver */
		ESP_ERROR_CHECK(esp_event_loop_create_default());

		esp_netif_sta = esp_netif_create_default_wifi_sta();
		esp_netif_ap = esp_netif_create_default_wifi_ap();


		/* default wifi config */
		wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
		ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

		/* event handler for the connection */
	    esp_event_handler_instance_t instance_wifi_event;
	    esp_event_handler_instance_t instance_ip_event;
	    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_wifi_event));
	    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_ip_event));

	    /* SoftAP - Wifi Access Point configuration setup */
	    wifi_config_t ap_config = { };

		/*wifi_config_t ap_config = {
			.ap = {
				.ssid_len = 0,
				.channel = wifi_settings.ap_channel,
				.authmode = WIFI_AUTH_WPA2_PSK,
				.ssid_hidden = wifi_settings.ap_ssid_hidden,
				.max_connection = DEFAULT_AP_MAX_CONNECTIONS,
				.beacon_interval = DEFAULT_AP_BEACON_INTERVAL,
			},
		};*/

		ap_config.ap.ssid_len = 0;
		ap_config.ap.channel = wifi_settings.ap_channel;
		ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
		ap_config.ap.ssid_hidden = wifi_settings.ap_ssid_hidden;
		ap_config.ap.max_connection = DEFAULT_AP_MAX_CONNECTIONS;
		ap_config.ap.beacon_interval = DEFAULT_AP_BEACON_INTERVAL;

		memcpy(ap_config.ap.ssid, wifi_settings.ap_ssid , sizeof(wifi_settings.ap_ssid));
		memcpy(ap_config.ap.password, wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd));

		/* DHCP AP configuration */
		esp_netif_dhcps_stop(esp_netif_ap); /* DHCP client/server must be stopped before setting new IP information. */
		esp_netif_ip_info_t ap_ip_info;
		memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));
		inet_pton(AF_INET, DEFAULT_AP_IP, &ap_ip_info.ip);
		inet_pton(AF_INET, DEFAULT_AP_GATEWAY, &ap_ip_info.gw);
		inet_pton(AF_INET, DEFAULT_AP_NETMASK, &ap_ip_info.netmask);
		ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));
		ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
		ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, wifi_settings.ap_bandwidth));
		ESP_ERROR_CHECK(esp_wifi_set_ps(wifi_settings.sta_power_save));

		/* by default the mode is STA because wifi_manager will not start the access point unless it has to! */
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());

		/* wifi scanner config */
		/*wifi_scan_config_t scan_config = {};
		scan_config.ssid = 0;
		scan_config.bssid = 0;
		scan_config.channel = 0;
		scan_config.show_hidden = true;*/

		/* enqueue first event: load previous config */
		wifi_manager_send_message(WM_LOAD_AND_RESTORE_STA, NULL);

		/* main processing loop */
		for(;;){
			xStatus = xQueueReceive( wifi_manager_queue, &msg, portMAX_DELAY );

			if( xStatus == pdPASS ){
				switch(msg.code){

				case EVENT_SCAN_DONE:
					/* As input param, it stores max AP number ap_records can hold. As output param, it receives the actual AP number this API returns.
					 * As a consequence, ap_num MUST be reset to MAX_AP_NUM at every scan */
					ap_num = MAX_AP_NUM;
					ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));
					/* make sure the http server isn't trying to access the list while it gets refreshed */

					/* callback */
					//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_START_WIFI_SCAN:
					ESP_LOGD(TAG, "WM_START_WIFI_SCAN");
					/* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
					

					/* callback */
					//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_LOAD_AND_RESTORE_STA:
					ESP_LOGI(TAG, "WM_LOAD_AND_RESTORE_STA");

					if(wifi_manager_fetch_wifi_sta_config()) {
						wifi_manager_send_message(WM_CONNECT_STA, (void*)CONNECTION_REQUEST_RESTORE_CONNECTION);
					} else {
						ESP_LOGI(TAG, "wifi_manager_fetch_wifi_sta_config - 0");
					}

					/* callback */
					//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case WM_CONNECT_STA:
					ESP_LOGI(TAG, "WM_CONNECT_STA");

					if((BaseType_t)msg.param == CONNECTION_REQUEST_USER) {
						xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
					}
					else if((BaseType_t)msg.param == CONNECTION_REQUEST_RESTORE_CONNECTION) {
						xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
					}

					uxBits = xEventGroupGetBits(wifi_manager_event_group);
					if( uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT ){
						wifi_manager_send_message(WM_DISCONNECT_STA, NULL);
						/* todo: reconnect */
					}
					else{
						/* update config to latest and attempt connection */
						ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, wifi_manager_get_wifi_sta_config())); //
						ESP_ERROR_CHECK(esp_wifi_connect());
					}

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case EVENT_STA_DISCONNECTED:
					ESP_LOGI(TAG, "EVENT_STA_DISCONNECTED %d", (uint32_t)msg.param);

					break;

				case WM_START_AP:
					ESP_LOGI(TAG, "WM_START_AP");
					esp_wifi_set_mode(WIFI_MODE_APSTA);

					//http_server_start();
					//dns_server_start();

					/* callback */
					//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

					break;

				case EVENT_STA_GOT_IP:
					ESP_LOGI(TAG, "EVENT_STA_GOT_IP");

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

				} /* end of switch/case */
			} /* end of if status=pdPASS */
		} /* end of for loop */

		vTaskDelete( NULL );
	}
}