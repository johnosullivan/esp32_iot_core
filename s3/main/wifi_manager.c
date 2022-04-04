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

#include "wifi_manager.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 4

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;


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

/**
 * The actual WiFi settings in use
 * CONFIG_WIFI_MANAGER_TASK_PRIORITY=5
CONFIG_WIFI_MANAGER_MAX_RETRY=2
CONFIG_DEFAULT_AP_SSID="esp32"
CONFIG_DEFAULT_AP_PASSWORD="esp32pwd"
CONFIG_DEFAULT_AP_CHANNEL=1
CONFIG_DEFAULT_AP_IP="10.10.0.1"
CONFIG_DEFAULT_AP_GATEWAY="10.10.0.1"
CONFIG_DEFAULT_AP_NETMASK="255.255.255.0"
CONFIG_DEFAULT_AP_MAX_CONNECTIONS=4
CONFIG_DEFAULT_AP_BEACON_INTERVAL=100
 */
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

/*
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}*/


/*
esp_err_t wifi_manager_save_sta_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	ESP_LOGD(TAG, "About to save config to flash");

	if(wifi_manager_config_sta){

		esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "ssid", wifi_manager_config_sta->sta.ssid, 32);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "password", wifi_manager_config_sta->sta.password, 64);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "settings", &wifi_settings, sizeof(wifi_settings));
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_commit(handle);
		if (esp_err != ESP_OK) return esp_err;

		nvs_close(handle);

		ESP_LOGD(TAG, "wifi_manager_wrote wifi_sta_config: ssid:%s password:%s",wifi_manager_config_sta->sta.ssid,wifi_manager_config_sta->sta.password);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_ssid: %s",wifi_settings.ap_ssid);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_pwd: %s",wifi_settings.ap_pwd);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_channel: %i",wifi_settings.ap_channel);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_hidden (1 = yes): %i",wifi_settings.ap_ssid_hidden);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz): %i",wifi_settings.ap_bandwidth);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: sta_only (0 = APSTA, 1 = STA when connected): %i",wifi_settings.sta_only);
		ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: sta_power_save (1 = yes): %i",wifi_settings.sta_power_save);
	}

	return ESP_OK;
}*/

/**
 * @brief Standard wifi event handler
 */
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT){
		switch(event_id){

		/* The Wi-Fi driver will never generate this event, which, as a result, can be ignored by the application event
		 * callback. This event may be removed in future releases. */
		case WIFI_EVENT_WIFI_READY:
			ESP_LOGI(TAG, "WIFI_EVENT_WIFI_READY");
			break;

		/* The scan-done event is triggered by esp_wifi_scan_start() and will arise in the following scenarios:
			  The scan is completed, e.g., the target AP is found successfully, or all channels have been scanned.
			  The scan is stopped by esp_wifi_scan_stop().
			  The esp_wifi_scan_start() is called before the scan is completed. A new scan will override the current
				 scan and a scan-done event will be generated.
			The scan-done event will not arise in the following scenarios:
			  It is a blocked scan.
			  The scan is caused by esp_wifi_connect().
			Upon receiving this event, the event task does nothing. The application event callback needs to call
			esp_wifi_scan_get_ap_num() and esp_wifi_scan_get_ap_records() to fetch the scanned AP list and trigger
			the Wi-Fi driver to free the internal memory which is allocated during the scan (do not forget to do this)!
		 */
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

		/* If esp_wifi_connect() returns ESP_OK and the station successfully connects to the target AP, the connection event
		 * will arise. Upon receiving this event, the event task starts the DHCP client and begins the DHCP process of getting
		 * the IP address. Then, the Wi-Fi driver is ready for sending and receiving data. This moment is good for beginning
		 * the application work, provided that the application does not depend on LwIP, namely the IP address. However, if
		 * the application is LwIP-based, then you need to wait until the got ip event comes in. */
		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
			break;

		/* This event can be generated in the following scenarios:
		 *
		 *     When esp_wifi_disconnect(), or esp_wifi_stop(), or esp_wifi_deinit(), or esp_wifi_restart() is called and
		 *     the station is already connected to the AP.
		 *
		 *     When esp_wifi_connect() is called, but the Wi-Fi driver fails to set up a connection with the AP due to certain
		 *     reasons, e.g. the scan fails to find the target AP, authentication times out, etc. If there are more than one AP
		 *     with the same SSID, the disconnected event is raised after the station fails to connect all of the found APs.
		 *
		 *     When the Wi-Fi connection is disrupted because of specific reasons, e.g., the station continuously loses N beacons,
		 *     the AP kicks off the station, the AP’s authentication mode is changed, etc.
		 *
		 * Upon receiving this event, the default behavior of the event task is: - Shuts down the station’s LwIP netif.
		 * - Notifies the LwIP task to clear the UDP/TCP connections which cause the wrong status to all sockets. For socket-based
		 * applications, the application callback can choose to close all sockets and re-create them, if necessary, upon receiving
		 * this event.
		 *
		 * The most common event handle code for this event in application is to call esp_wifi_connect() to reconnect the Wi-Fi.
		 * However, if the event is raised because esp_wifi_disconnect() is called, the application should not call esp_wifi_connect()
		 * to reconnect. It’s application’s responsibility to distinguish whether the event is caused by esp_wifi_disconnect() or
		 * other reasons. Sometimes a better reconnect strategy is required, refer to <Wi-Fi Reconnect> and
		 * <Scan When Wi-Fi Is Connecting>.
		 *
		 * Another thing deserves our attention is that the default behavior of LwIP is to abort all TCP socket connections on
		 * receiving the disconnect. Most of time it is not a problem. However, for some special application, this may not be
		 * what they want, consider following scenarios:
		 *
		 *    The application creates a TCP connection to maintain the application-level keep-alive data that is sent out
		 *    every 60 seconds.
		 *
		 *    Due to certain reasons, the Wi-Fi connection is cut off, and the <WIFI_EVENT_STA_DISCONNECTED> is raised.
		 *    According to the current implementation, all TCP connections will be removed and the keep-alive socket will be
		 *    in a wrong status. However, since the application designer believes that the network layer should NOT care about
		 *    this error at the Wi-Fi layer, the application does not close the socket.
		 *
		 *    Five seconds later, the Wi-Fi connection is restored because esp_wifi_connect() is called in the application
		 *    event callback function. Moreover, the station connects to the same AP and gets the same IPV4 address as before.
		 *
		 *    Sixty seconds later, when the application sends out data with the keep-alive socket, the socket returns an error
		 *    and the application closes the socket and re-creates it when necessary.
		 *
		 * In above scenario, ideally, the application sockets and the network layer should not be affected, since the Wi-Fi
		 * connection only fails temporarily and recovers very quickly. The application can enable “Keep TCP connections when
		 * IP changed” via LwIP menuconfig.*/
		case WIFI_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
			// event = (wifi_event_sta_disconnected_t*)event_data;
			/* if a DISCONNECT message is posted while a scan is in progress this scan will NEVER end, causing scan to never work again. For this reason SCAN_BIT is cleared too */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_SCAN_BIT);
			break;

		/* This event arises when the AP to which the station is connected changes its authentication mode, e.g., from no auth
		 * to WPA. Upon receiving this event, the event task will do nothing. Generally, the application event callback does
		 * not need to handle this either. */
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

		/* Every time a station is connected to ESP32 AP, the <WIFI_EVENT_AP_STACONNECTED> will arise. Upon receiving this
		 * event, the event task will do nothing, and the application callback can also ignore it. However, you may want
		 * to do something, for example, to get the info of the connected STA, etc. */
		case WIFI_EVENT_AP_STACONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
			xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
			break;

		/* This event can happen in the following scenarios:
		 *   The application calls esp_wifi_disconnect(), or esp_wifi_deauth_sta(), to manually disconnect the station.
		 *   The Wi-Fi driver kicks off the station, e.g. because the AP has not received any packets in the past five minutes, etc.
		 *   The station kicks off the AP.
		 * When this event happens, the event task will do nothing, but the application event callback needs to do
		 * something, e.g., close the socket which is related to this station, etc. */
		case WIFI_EVENT_AP_STADISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
			break;

		/* This event is disabled by default. The application can enable it via API esp_wifi_set_event_mask().
		 * When this event is enabled, it will be raised each time the AP receives a probe request. */
		case WIFI_EVENT_AP_PROBEREQRECVED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_PROBEREQRECVED");
			break;

		} /* end switch */
	}
	else if(event_base == IP_EVENT){

		switch(event_id){

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
	        //ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
	        //wifi_manager_send_message(EVENT_STA_GOT_IP, (void*)event->ip_info.ip.addr);
			break;

		/* This event arises when the IPV6 SLAAC support auto-configures an address for the ESP32, or when this address changes.
		 * The event means that everything is ready and the application can begin its tasks (e.g., creating sockets). */
		case IP_EVENT_GOT_IP6:
			ESP_LOGI(TAG, "IP_EVENT_GOT_IP6");
			break;

		/* This event arises when the IPV4 address become invalid.
		 * IP_STA_LOST_IP doesn’t arise immediately after the WiFi disconnects, instead it starts an IPV4 address lost timer,
		 * if the IPV4 address is got before ip lost timer expires, IP_EVENT_STA_LOST_IP doesn’t happen. Otherwise, the event
		 * arises when IPV4 address lost timer expires.
		 * Generally the application don’t need to care about this event, it is just a debug event to let the application
		 * know that the IPV4 address is lost. */
		case IP_EVENT_STA_LOST_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
			break;

		}
	}

}

BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSendToFront( wifi_manager_queue, &msg, portMAX_DELAY);
}

BaseType_t wifi_manager_send_message(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend( wifi_manager_queue, &msg, portMAX_DELAY);
}

void wifi_manager_start(){
	/* initialize flash memory */
	//nvs_flash_init();

	queue_message msg;
	BaseType_t xStatus;

	//wifi_manager_event_group = xEventGroupCreate();

	/* start wifi manager task */
	//xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);
	s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_manager_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_manager_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Look Ma No Wires",
			.password = "Mogilska12!",
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    /*EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);*/

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    /*if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect");
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }*/
		wifi_manager_send_message(WM_LOAD_AND_RESTORE_STA, NULL);

	 for(;;){
		xStatus = xQueueReceive( wifi_manager_queue, &msg, portMAX_DELAY );
		if( xStatus == pdPASS ){
			switch(msg.code){

			case EVENT_SCAN_DONE:
				/* As input param, it stores max AP number ap_records can hold. As output param, it receives the actual AP number this API returns.
				 * As a consequence, ap_num MUST be reset to MAX_AP_NUM at every scan */
				//ap_num = MAX_AP_NUM;
				//ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));
				/* make sure the http server isn't trying to access the list while it gets refreshed */

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_START_WIFI_SCAN:
				ESP_LOGD(TAG, "WM_START_WIFI_SCAN");

				/* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
				/*uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(! (uxBits & WIFI_MANAGER_SCAN_BIT) ){
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
					ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
				}*/

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_LOAD_AND_RESTORE_STA:
				ESP_LOGI(TAG, "WM_LOAD_AND_RESTORE_STA");

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_CONNECT_STA:
				ESP_LOGI(TAG, "WM_CONNECT_STA");

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "EVENT_STA_DISCONNECTED %d", (uint32_t)msg.param);

				/* this even can be posted in numerous different conditions
				 *
				 * 1. SSID password is wrong
				 * 2. Manual disconnection ordered
				 * 3. Connection lost
				 *
				 * Having clear understand as to WHY the event was posted is key to having an efficient wifi manager
				 *
				 * With wifi_manager, we determine:
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, We consider it's a client that requested the connection.
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, it's probably a password/something went wrong with the handshake.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, it's a disconnection that was ASKED by the client (clicking disconnect in the app)
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, saved wifi is erased from the NVS memory.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT and WIFI_MANAGER_REQUEST_STA_CONNECT_BIT are NOT set, it's a lost connection
				 *
				 *  In this version of the software, reason codes are not used. They are indicated here for potential future usage.
				 *
				 *  REASON CODE:
				 *  1		UNSPECIFIED
				 *  2		AUTH_EXPIRE					auth no longer valid, this smells like someone changed a password on the AP
				 *  3		AUTH_LEAVE
				 *  4		ASSOC_EXPIRE
				 *  5		ASSOC_TOOMANY				too many devices already connected to the AP => AP fails to respond
				 *  6		NOT_AUTHED
				 *  7		NOT_ASSOCED
				 *  8		ASSOC_LEAVE					tested as manual disconnect by user
				 *  9		ASSOC_NOT_AUTHED
				 *  10		DISASSOC_PWRCAP_BAD
				 *  11		DISASSOC_SUPCHAN_BAD
				 *	12		<n/a>
				 *  13		IE_INVALID
				 *  14		MIC_FAILURE
				 *  15		4WAY_HANDSHAKE_TIMEOUT		wrong password! This was personnaly tested on my home wifi with a wrong password.
				 *  16		GROUP_KEY_UPDATE_TIMEOUT
				 *  17		IE_IN_4WAY_DIFFERS
				 *  18		GROUP_CIPHER_INVALID
				 *  19		PAIRWISE_CIPHER_INVALID
				 *  20		AKMP_INVALID
				 *  21		UNSUPP_RSN_IE_VERSION
				 *  22		INVALID_RSN_IE_CAP
				 *  23		802_1X_AUTH_FAILED			wrong password?
				 *  24		CIPHER_SUITE_REJECTED
				 *  200		BEACON_TIMEOUT
				 *  201		NO_AP_FOUND
				 *  202		AUTH_FAIL
				 *  203		ASSOC_FAIL
				 *  204		HANDSHAKE_TIMEOUT
				 *
				 * */

				/* reset saved sta IP */

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_START_AP:
				ESP_LOGI(TAG, "WM_START_AP");
				//esp_wifi_set_mode(WIFI_MODE_APSTA);


				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "EVENT_STA_GOT_IP");

				//uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* reset connection requests bits -- doesn't matter if it was set or not */
				//xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

				/* save IP as a string for the HTTP server host */
				//((uint32_t)msg.param);

				/* save wifi config in NVS if it wasn't a restored of a connection */
				/*if(uxBits & WIFI_MANAGER_REQUEST_RESTORE_STA_BIT){
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
				}
				else{
					//wifi_manager_save_sta_config();
				}*/

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_DISCONNECT_STA:
				ESP_LOGI(TAG, "WM_DISCONNECT_STA");

				/* precise this is coming from a user request */
				//xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

				/* order wifi discconect */
				//ESP_ERROR_CHECK(esp_wifi_disconnect());

				/* callback */
				//if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			default:
				break;

			} /* end of switch/case */
		} /* end of if status=pdPASS */
	} /* end of for loop */

	vTaskDelete( NULL );
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

void wifi_init_sta(void)
{
nvs_flash_init();

	wifi_manager_event_group = xEventGroupCreate();

	/* start wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);
}

void wifi_manager( void * pvParameters ){
	queue_message msg;
	BaseType_t xStatus;
	EventBits_t uxBits;
	uint8_t	retries = 0;

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
	wifi_config_t ap_config = {
		.sta = {
			.ssid = "Look Ma No Wires",
			.password = "Mogilska12!",
		},
	};
	//memcpy(ap_config.ap.ssid, wifi_settings.ap_ssid , sizeof(wifi_settings.ap_ssid));
	//memcpy(ap_config.ap.password, wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd));

	/* DHCP AP configuration */
	/*esp_netif_dhcps_stop(esp_netif_ap); 
	esp_netif_ip_info_t ap_ip_info;
	memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));
	inet_pton(AF_INET, DEFAULT_AP_IP, &ap_ip_info.ip);
	inet_pton(AF_INET, DEFAULT_AP_GATEWAY, &ap_ip_info.gw);
	inet_pton(AF_INET, DEFAULT_AP_NETMASK, &ap_ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));
	ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));*/

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &ap_config));
	//ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, wifi_settings.ap_bandwidth));
	ESP_ERROR_CHECK(esp_wifi_set_ps(wifi_settings.sta_power_save));


	/* STA - Wifi Station configuration setup */
//	tcpip_adapter_dhcp_status_t status;
//	if(wifi_settings.sta_static_ip) {
//		ESP_LOGI(TAG, "Assigning static ip to STA interface. IP: %s , GW: %s , Mask: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));
//
//		/* stop DHCP client*/
//		ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
//		/* assign a static IP to the STA network interface */
//		ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &wifi_settings.sta_static_ip_config));
//		}
//	else {
//		/* start DHCP client if not started*/
//		ESP_LOGI(TAG, "wifi_manager: Start DHCP client for STA interface. If not already running");
//		ESP_ERROR_CHECK(tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &status));
//		if (status!=TCPIP_ADAPTER_DHCP_STARTED)
//			ESP_ERROR_CHECK(tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA));
//	}


	/* by default the mode is STA because wifi_manager will not start the access point unless it has to! */
	ESP_ERROR_CHECK(esp_wifi_start());


	/* wifi scanner config */
	/*wifi_scan_config_t scan_config = {
		.ssid = 0,
		.bssid = 0,
		.channel = 0,
		.show_hidden = true
	};*/

	/* main processing loop */
	for(;;){
		xStatus = xQueueReceive( wifi_manager_queue, &msg, portMAX_DELAY );
		if(xStatus == pdPASS){
			switch(msg.code){
			case EVENT_SCAN_DONE:
				/* As input param, it stores max AP number ap_records can hold. As output param, it receives the actual AP number this API returns.
				 * As a consequence, ap_num MUST be reset to MAX_AP_NUM at every scan */
				//ap_num = MAX_AP_NUM;
				//ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));
				/* make sure the http server isn't trying to access the list while it gets refreshed */

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_START_WIFI_SCAN:
				ESP_LOGD(TAG, "WM_START_WIFI_SCAN");

				/* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
				/*uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(! (uxBits & WIFI_MANAGER_SCAN_BIT) ){
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
					ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
				}*/

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_LOAD_AND_RESTORE_STA:
				ESP_LOGI(TAG, "WM_LOAD_AND_RESTORE_STA");

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_CONNECT_STA:
				ESP_LOGI(TAG, "WM_CONNECT_STA");

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "EVENT_STA_DISCONNECTED %d", (uint32_t)msg.param);

				/* this even can be posted in numerous different conditions
				 *
				 * 1. SSID password is wrong
				 * 2. Manual disconnection ordered
				 * 3. Connection lost
				 *
				 * Having clear understand as to WHY the event was posted is key to having an efficient wifi manager
				 *
				 * With wifi_manager, we determine:
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, We consider it's a client that requested the connection.
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, it's probably a password/something went wrong with the handshake.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, it's a disconnection that was ASKED by the client (clicking disconnect in the app)
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, saved wifi is erased from the NVS memory.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT and WIFI_MANAGER_REQUEST_STA_CONNECT_BIT are NOT set, it's a lost connection
				 *
				 *  In this version of the software, reason codes are not used. They are indicated here for potential future usage.
				 *
				 *  REASON CODE:
				 *  1		UNSPECIFIED
				 *  2		AUTH_EXPIRE					auth no longer valid, this smells like someone changed a password on the AP
				 *  3		AUTH_LEAVE
				 *  4		ASSOC_EXPIRE
				 *  5		ASSOC_TOOMANY				too many devices already connected to the AP => AP fails to respond
				 *  6		NOT_AUTHED
				 *  7		NOT_ASSOCED
				 *  8		ASSOC_LEAVE					tested as manual disconnect by user
				 *  9		ASSOC_NOT_AUTHED
				 *  10		DISASSOC_PWRCAP_BAD
				 *  11		DISASSOC_SUPCHAN_BAD
				 *	12		<n/a>
				 *  13		IE_INVALID
				 *  14		MIC_FAILURE
				 *  15		4WAY_HANDSHAKE_TIMEOUT		wrong password! This was personnaly tested on my home wifi with a wrong password.
				 *  16		GROUP_KEY_UPDATE_TIMEOUT
				 *  17		IE_IN_4WAY_DIFFERS
				 *  18		GROUP_CIPHER_INVALID
				 *  19		PAIRWISE_CIPHER_INVALID
				 *  20		AKMP_INVALID
				 *  21		UNSUPP_RSN_IE_VERSION
				 *  22		INVALID_RSN_IE_CAP
				 *  23		802_1X_AUTH_FAILED			wrong password?
				 *  24		CIPHER_SUITE_REJECTED
				 *  200		BEACON_TIMEOUT
				 *  201		NO_AP_FOUND
				 *  202		AUTH_FAIL
				 *  203		ASSOC_FAIL
				 *  204		HANDSHAKE_TIMEOUT
				 *
				 * */

				/* reset saved sta IP */

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_START_AP:
				ESP_LOGI(TAG, "WM_START_AP");
				esp_wifi_set_mode(WIFI_MODE_APSTA);


				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "EVENT_STA_GOT_IP");

				//uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* reset connection requests bits -- doesn't matter if it was set or not */
				//xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

				/* save IP as a string for the HTTP server host */
				//((uint32_t)msg.param);

				/* save wifi config in NVS if it wasn't a restored of a connection */
				/*if(uxBits & WIFI_MANAGER_REQUEST_RESTORE_STA_BIT){
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
				}
				else{
					//wifi_manager_save_sta_config();
				}*/

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_DISCONNECT_STA:
				ESP_LOGI(TAG, "WM_DISCONNECT_STA");

				/* precise this is coming from a user request */
				//xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

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
