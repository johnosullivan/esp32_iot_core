#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

// Lightweight cJSON parser
#include "libs/cJSON.h"

// Standard needed headers
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"

#define RESTART_GPIO	12
#define BLINK_GPIO			CONFIG_BLINK_GPIO
#define MIHOME_API_URI 		CONFIG_MIHOME_API_URI
#define WIFI_CORE_LOGGING 	CONFIG_WIFI_CORE_LOGGING
#define CONNECT_IOT_OPTION 	CONFIG_CONNECT_IOT_OPTION

#define BT_DEVICE_NAME   "MIHOME_SWQZ192"

#define MIHOME_VERSION  			"v0.0.1"
#define MIHOME_STORAGE_NAMESPACE 	"storage"
#define MIHOME_STORAGE_IS_CONFIG 	"is_configured"
#define MIHOME_STORAGE_SETTINGS 	"mihome_settings"
#define MIHOME_SETTINGS_CLOUD_URL_MAX_SIZE 64
#define MIHOME_SETTINGS_CLOUD_UUID_MAX_SIZE 64

#define MIHOME_LED_PIN 				4
#define HEX_COLOR_BLUE 				"0000ff"
#define HEX_COLOR_YELLOW 			"e3ff00"
#define HEX_COLOR_GREEN 			"00ff00"
#define HEX_COLOR_RED 				"ff0000"
#define HEX_COLOR_BLACK 			"000000"
#define HEX_COLOR_WHITE 			"ffffff"
#define HEX_COLOR_RESET_PENDING 	"d25f2e"
#define HEX_COLOR_RESET_RESTART 	"e3ff00"

#define PING_COMMAND "PING"

struct mihome_settings_t {
	uint8_t cloud_url[MIHOME_SETTINGS_CLOUD_UUID_MAX_SIZE];
	uint8_t cloud_uuid[MIHOME_SETTINGS_CLOUD_UUID_MAX_SIZE];
};
extern struct mihome_settings_t mihome_settings;

// Certs PEMS
extern const char api_cert_pem_start[] asm("_binary_api_cert_pem_start");
extern const char api_cert_pem_end[]   asm("_binary_api_cert_pem_end");

#ifdef __cplusplus
}
#endif

#endif /* CONSTANTS_H_INCLUDED */
