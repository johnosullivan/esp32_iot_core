#ifndef WIFI_MANAGER_H_INCLUDED
#define WIFI_MANAGER_H_INCLUDED

#include <stdbool.h>

#include "constants.h"

#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
					*
					*  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, We consider it's a client that requested the connection.
					*    When SYSTEM_EVENT_STA_DISCONNECTED is posted, it's probably a password/something went wrong with the handshake.
					*
					*  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, it's a disconnection that was ASKED by the client (clicking disconnect in the app)
					*    When SYSTEM_EVENT_STA_DISCONNECTED is posted, saved wifi is erased from the NVS memory.
					*
					*  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT and WIFI_MANAGER_REQUEST_STA_CONNECT_BIT are NOT set, it's a lost connection
					*
					*  REASON CODE:
					*  1		UNSPECIFIED
					*  2		AUTH_EXPIRE	
					*  3		AUTH_LEAVE
					*  4		ASSOC_EXPIRE
					*  5		ASSOC_TOOMANY
					*  6		NOT_AUTHED
					*  7		NOT_ASSOCED
					*  8		ASSOC_LEAVE
					*  9		ASSOC_NOT_AUTHED
					*  10		DISASSOC_PWRCAP_BAD
					*  11		DISASSOC_SUPCHAN_BAD
					*	12		<n/a>
					*  13		IE_INVALID
					*  14		MIC_FAILURE
					*  15		4WAY_HANDSHAKE_TIMEOUT
					*  16		GROUP_KEY_UPDATE_TIMEOUT
					*  17		IE_IN_4WAY_DIFFERS
					*  18		GROUP_CIPHER_INVALID
					*  19		PAIRWISE_CIPHER_INVALID
					*  20		AKMP_INVALID
					*  21		UNSUPP_RSN_IE_VERSION
					*  22		INVALID_RSN_IE_CAP
					*  23		802_1X_AUTH_FAILED
					*  24		CIPHER_SUITE_REJECTED
					*  200		BEACON_TIMEOUT
					*  201		NO_AP_FOUND
					*  202		AUTH_FAIL
					*  203		ASSOC_FAIL
					*  204		HANDSHAKE_TIMEOUT
					*
					* */

#define WIFI_MANAGER_TASK_PRIORITY 			5
#define MAX_SSID_SIZE						32
#define MAX_PASSWORD_SIZE					64
#define MAX_AP_NUM 							15
#define DEFAULT_STA_ONLY 					1
#define DEFAULT_STA_POWER_SAVE 				WIFI_PS_NONE

/**
 * @brief simplified reason codes for a lost connection.
 */
typedef enum update_reason_code_t {
	UPDATE_CONNECTION_OK 			= 0,
	UPDATE_FAILED_ATTEMPT 			= 1,
	UPDATE_USER_DISCONNECT 			= 2,
	UPDATE_LOST_CONNECTION 			= 3
} update_reason_code_t;

typedef enum connection_request_made_by_code_t{
	CONNECTION_REQUEST_NONE 		= 0,
	CONNECTION_REQUEST_USER 		= 1,
	CONNECTION_REQUEST_AUTO_RECONNECT 		= 2,
	CONNECTION_REQUEST_RESTORE_CONNECTION 	= 3,
	CONNECTION_REQUEST_MAX 					= 0x7fffffff
} connection_request_made_by_code_t;

/*
 * 
 */
struct wifi_settings_t {
	uint8_t ap_ssid[MAX_SSID_SIZE];
	uint8_t ap_pwd[MAX_PASSWORD_SIZE];
	uint8_t ap_channel;
	uint8_t ap_ssid_hidden;
	bool sta_only;
	bool sta_static_ip;
	wifi_ps_type_t sta_power_save;
	wifi_bandwidth_t ap_bandwidth;
	esp_netif_ip_info_t sta_static_ip_config;
};
extern struct wifi_settings_t wifi_settings;

/**
 * @brief structure used to store one message in the queue.
 */
typedef struct{
	message_code_t code;
	void *param;
} queue_message;

/**
 * @brief returns the current esp_netif object for the STAtion
 */
esp_netif_t* wifi_manager_get_esp_netif_sta();

/**
 * @brief returns the current esp_netif object for the Access Point
 */
esp_netif_t* wifi_manager_get_esp_netif_ap();

void wifi_manager_start();

void wifi_manager(void * pvParameters);

/**
 * @brief Register a callback to a custom function when specific event message_code happens.
 */
void wifi_manager_set_callback(message_code_t message_code, void (*func_ptr)(void*));

BaseType_t wifi_manager_send_message(message_code_t code, void *param);

BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H_INCLUDED */
