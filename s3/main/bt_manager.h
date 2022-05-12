#ifndef BT_MANAGER_H_INCLUDED
#define BT_MANAGER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "constants.h"

typedef struct {
    uint8_t                 *prepare_buf;
    int                      prepare_len;
} prepare_type_env_t;

/*
    PROFILES COUNT #2

    1) PROFILE_APP_ID_1 = ID: 0
    1) PROFILE_APP_ID_1 = ID: 0


*/

#define PROFILE_NUM 				        1
#define PROFILE_APP_ID				        0

#define GATTS_SERVICE_CHAR_COUNT            2
#define GATTS_SERVICE_DEVICE_UUID   		0x00FF
#define GATTS_CHAR_DEVICE_NAME_UUID      	0xFF01
#define GATTS_CHAR_DEVICE_MIOS_UUID      	0xFF02
#define GATTS_SERVICE_CONFIG_UUID 		0x2000

#define GATTS_NUM_HANDLE     		4
#define GATTS_CHAR_VAL_LEN_MAX 		0x40
#define PREPARE_BUF_MAX_SIZE 		1024
#define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))


/* Attributes State Machine */
enum {
    DEVICE_SVC,
    DEVICE_CHAR_NAME,
    DEVICE_CHAR_VAL_NAME,

    DEVICE_CHAR_VERSION,
    DEVICE_CHAR_VAL_VERSION,

    DEVICE_CHAR_BLE,
    DEVICE_CHAR_VAL_BLE_ADDRESS,

    DEVICE_CHAR_WIFI,
    DEVICE_CHAR_VAL_WIFI_ADDRESS,

    HRS_DEVICE_NB
};

enum {
    CONFIG_SVC,
    CONFIG_CHAR_READY,
    CONFIG_CHAR_VAL_READY,
    CONFIG_CHAR_CFG_READY,

    CONFIG_CHAR_WIFI_SSID,
    CONFIG_CHAR_VAL_WIFI_SSID,

    CONFIG_CHAR_WIFI_PASS,
    CONFIG_CHAR_VAL_WIFI_PASS,

    CONFIG_CHAR_SYS_URL,
    CONFIG_CHAR_VAL_SYS_URL,

    CONFIG_CHAR_SYS_UUID,
    CONFIG_CHAR_VAL_SYS_UUID,

    HRS_CONFIG_NB
};

#define adv_config_flag      		(1 << 0)
#define scan_rsp_config_flag 		(1 << 1)

prepare_type_env_t prepare_write_env;
 
void bt_manager_start();

void parse_bt_json_playload(char *data);

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

void write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* BT_MANAGER_H_INCLUDED */
