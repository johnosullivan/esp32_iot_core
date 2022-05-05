#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_gatt_common_api.h"

#include "bt_manager.h"


static const char *TAG_BT_MG = "esp32_hub_bt_manager";

#define GATTS_DEMO_CHAR_VAL_LEN_MAX 500
#define PREPARE_BUF_MAX_SIZE        1024
#define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))

char *read_bt_data;
uint8_t status = 0x00;


static uint8_t attr_value_str[] = { 0x11,0x22,0x33 };

/*
static esp_gatt_char_prop_t esp_gatt_property = 0;
static esp_attr_value_t gatts_demo_char1_val = {
    .attr_max_len = GATTS_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(attr_value_str),
    .attr_value   = attr_value_str,
};
*/

static uint8_t adv_config_done = 0;

/*
static uint8_t raw_adv_data[] = {
    0x02, 0x01, 0x06, 0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd
};

static uint8_t raw_scan_rsp_data[] = {
    0x0f, 0x09, 0x45, 0x53, 0x50, 0x5f, 0x4d, 0x49, 0x48, 0x4f, 0x4d, 0x45, 0x00, 0x00, 0x00, 0x00
};*/

static uint8_t raw_adv_data[] = {
        /* flags */
        0x02, 0x01, 0x06,
        /* tx power*/
        0x02, 0x0a, 0xeb,
        /* service uuid */
        0x03, 0x03, 0xFA, 0x00,
        /* device name */
        0x0f, 0x09, 'E', 'X', 'P', '_', 'G', 'A', 'T', 'T', 'S', '_', 'D','E', 'M', 'O'
};
static uint8_t raw_scan_rsp_data[] = {
        /* flags */
        0x02, 0x01, 0x06,
        /* tx power */
        0x02, 0x0a, 0xeb,
        /* service uuid */
        0x03, 0x03, 0xFF,0x00
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_ID] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    }
};

/* Service */
static const uint16_t GATTS_SERVICE_DEVICE         = 0x00FF;
static const uint16_t GATTS_CHAR_DEVICE_NAME       = 0xFF01;
static const uint16_t GATTS_CHAR_DEVICE_VERSION    = 0xFF02;
static const uint16_t GATTS_CHAR_DEVICE_BLE_ADDR   = 0xFF03;
static const uint16_t GATTS_CHAR_DEVICE_WIFI_ADDR  = 0xFF04;

static const uint16_t GATTS_SERVICE_CONFIG         = 0x00AA;
static const uint16_t GATTS_CHAR_CONFIG_READY      = 0xAA01;
static const uint16_t GATTS_CHAR_CONFIG_SSID       = 0xAA02;
static const uint16_t GATTS_CHAR_CONFIG_PWD        = 0xAA03;
static const uint16_t GATTS_CHAR_CONFIG_URL        = 0xAA04;
static const uint16_t GATTS_CHAR_CONFIG_UUID       = 0xAA05;

static const uint8_t CHAR_PROP_READ                = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t CHAR_PROP_READ_WRITE          = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;
// static const uint8_t CHAR_PROP_READ_WRITE_NOTIFY   = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
//static const uint8_t CHAR_PROP_READ_NOTIFY         = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY ESP_GATT_CHAR_PROP_BIT_WRITE;

static const uint8_t heart_measurement_ccc[2]      = {0x00, 0x00};


static const uint8_t char_value[1]                 = {0x00};

uint8_t ready_value[1]                       = {0x00};


const uint8_t uuid_value[1]                 = {0x00};
static const uint8_t password_value[1]             = {0x00};
static const uint8_t sys_url_value[1]              = {0x00};
static const uint8_t sys_uuid_value[1]             = {0x00};

static const uint16_t P_SERVICE_UUID               = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t C_DECLARATION_UUID           = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t CC_DECLARATION_UUID          = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

uint8_t base_mac_addr[6] = {0};


static const uint8_t char_prop_read_write_notify   =  ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;


//uint16_t heart_rate_handle_table[HRS_CONFIG_NB];
uint16_t gatt_db2_table[HRS_CONFIG_NB];

/* Full Database Description - Add attributes into the database */
static const esp_gatts_attr_db_t gatt_db[HRS_CONFIG_NB] =
{
    // Service Declaration
    [DEVICE_SVC]        =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&P_SERVICE_UUID, ESP_GATT_PERM_READ,
      sizeof(uint16_t), sizeof(GATTS_SERVICE_DEVICE), (uint8_t *)&GATTS_SERVICE_DEVICE}},

    /* Characteristic Declaration */
    [DEVICE_CHAR_NAME]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ}},

    /* Characteristic Value */
    [DEVICE_CHAR_VAL_NAME] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_DEVICE_NAME, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(BT_DEVICE_NAME), (uint8_t *)BT_DEVICE_NAME}},

    /* Characteristic Declaration */
    [DEVICE_CHAR_VERSION]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ}},

    /* Characteristic Value */
    [DEVICE_CHAR_VAL_VERSION]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_DEVICE_VERSION, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(MIHOME_VERSION), (uint8_t *)MIHOME_VERSION}},

    /* Characteristic Declaration */
    [DEVICE_CHAR_BLE]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ}},
 
    /* Characteristic Value */
    [DEVICE_CHAR_VAL_BLE_ADDRESS]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_DEVICE_BLE_ADDR, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(base_mac_addr), (uint8_t *)base_mac_addr}},

    /* Characteristic Declaration */
    [DEVICE_CHAR_WIFI]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ}},

    /* Characteristic Value */
    [DEVICE_CHAR_VAL_WIFI_ADDRESS]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_DEVICE_WIFI_ADDR, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},
};

static const esp_gatts_attr_db_t gatt_db2[HRS_CONFIG_NB] = {
    // Service Declaration
    [CONFIG_SVC]        =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&P_SERVICE_UUID, ESP_GATT_PERM_READ,
      sizeof(uint16_t), sizeof(GATTS_SERVICE_CONFIG), (uint8_t *)&GATTS_SERVICE_CONFIG}},

    /* Characteristic Declaration */
    [CONFIG_CHAR_READY]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [CONFIG_CHAR_VAL_READY] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_CONFIG_READY, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(ready_value), (uint8_t *)ready_value}},

    [CONFIG_CHAR_CFG_READY]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&CC_DECLARATION_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(heart_measurement_ccc), (uint8_t *)heart_measurement_ccc}},

    /* Characteristic Declaration */
    [CONFIG_CHAR_WIFI_SSID]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ_WRITE}},

    /* Characteristic Value */
    [CONFIG_CHAR_VAL_WIFI_SSID] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_CONFIG_SSID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(uuid_value), (uint8_t *)uuid_value}},

    /* Characteristic Declaration */
    [CONFIG_CHAR_WIFI_PASS]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ_WRITE}},

    /* Characteristic Value */
    [CONFIG_CHAR_VAL_WIFI_PASS] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_CONFIG_PWD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(password_value), (uint8_t *)password_value}},

    /* Characteristic Declaration */
    [CONFIG_CHAR_SYS_URL]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ_WRITE}},

    /* Characteristic Value */
    [CONFIG_CHAR_VAL_SYS_URL] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_CONFIG_URL, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(sys_url_value), (uint8_t *)sys_url_value}},

    /* Characteristic Declaration */
    [CONFIG_CHAR_SYS_UUID]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&C_DECLARATION_UUID, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&CHAR_PROP_READ_WRITE}},

    /* Characteristic Value */
    [CONFIG_CHAR_VAL_SYS_UUID] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_CONFIG_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(sys_uuid_value), (uint8_t *)sys_uuid_value}},
};

/*
    Start Bluetooth Manager / Device Configuration 
*/
void bt_manager_start() {
    esp_err_t err;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    err = esp_efuse_mac_get_default(base_mac_addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BT_MG, "Failed to get base MAC address from EFUSE BLK3. (%s)", esp_err_to_name(err));
    }

    err = esp_bt_controller_init(&bt_cfg);
    if (err) {
        ESP_LOGE(TAG_BT_MG, "%s BT_E001: %s", __func__, esp_err_to_name(err));
        return;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err) {
        ESP_LOGE(TAG_BT_MG, "%s BT_E002: %s", __func__, esp_err_to_name(err));
        return;
    }

    err = esp_bluedroid_init();
    if (err) {
        ESP_LOGE(TAG_BT_MG, "%s BT_E003: %s", __func__, esp_err_to_name(err));
        return;
    }

    err = esp_bluedroid_enable();
    if (err) {
        ESP_LOGE(TAG_BT_MG, "%s BT_E004: %s", __func__, esp_err_to_name(err));
        return;
    }

    err = esp_ble_gatts_register_callback(gatts_event_handler);
    if (err) {
        ESP_LOGE(TAG_BT_MG, "GATTS_E001, EC = %x", err);
        return;
    }

    err = esp_ble_gap_register_callback(gap_event_handler);
    if (err) {
        ESP_LOGE(TAG_BT_MG, "GATTS_E002, EC = %x", err);
        return;
    }

    err = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (err) {
        ESP_LOGE(TAG_BT_MG, "GATTS_E003, EC = %x", err);
        return;
    }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret) {
        ESP_LOGE(TAG_BT_MG, "MTU Failed, EC = %x", local_mtu_ret);
    }
}

/*
    BLE GATTS Event Function
*/
void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(TAG_BT_MG, "RAF APP_ID: %04x, Status: %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    /* If the gatts_if equal to profile A, call profile A cb handler, so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

/*
    BLE GAP Callback Function
*/
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~adv_config_flag);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~scan_rsp_config_flag);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG_BT_MG, "GATTS_E004");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG_BT_MG, "GATTS_E005");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
             ESP_LOGI(TAG_BT_MG, "UCPS = %d, min_int = %d, max_int = %d, conn_int = %d, latency = %d, timeout = %d",
                param->update_conn_params.status,
                param->update_conn_params.min_int,
                param->update_conn_params.max_int,
                param->update_conn_params.conn_int,
                param->update_conn_params.latency,
                param->update_conn_params.timeout);
            break;
        default:
            break;
    }
}























void gatts_create_service(esp_gatt_if_t gatts_if, bool is_primary, int16_t inst_id, int16_t service_uuid16) {
    gl_profile_tab[PROFILE_APP_ID].service_id.is_primary = is_primary;
    gl_profile_tab[PROFILE_APP_ID].service_id.id.inst_id = inst_id;
    gl_profile_tab[PROFILE_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
    gl_profile_tab[PROFILE_APP_ID].service_id.id.uuid.uuid.uuid16 = service_uuid16;

    esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(BT_DEVICE_NAME);
    if (set_dev_name_ret){
        ESP_LOGE(TAG_BT_MG, "set device name failed, error code = %x", set_dev_name_ret);
    }

    esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
    if (raw_adv_ret){
        ESP_LOGE(TAG_BT_MG, "config raw adv data failed, error code = %x ", raw_adv_ret);
    }
    adv_config_done |= adv_config_flag;

    esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
    if (raw_scan_ret){
        ESP_LOGE(TAG_BT_MG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
    }
    adv_config_done |= scan_rsp_config_flag;

    esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_APP_ID].service_id, GATTS_NUM_HANDLE);
}

struct gatts_char_inst {
    int16_t uuid16;
    esp_gatt_perm_t perm;
    esp_attr_value_t attr;
    esp_gatt_char_prop_t property;
};

static struct gatts_char_inst gl_device_characteristics[GATTS_SERVICE_CHAR_COUNT] = {
    [0] = {
        .uuid16 = GATTS_CHAR_DEVICE_NAME_UUID,
        .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        .attr = {
            .attr_max_len = GATTS_CHAR_VAL_LEN_MAX,
            .attr_len     = sizeof(attr_value_str),
            .attr_value   = attr_value_str,
        },
        .property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
    },
    [1] = {
        .uuid16 = GATTS_CHAR_DEVICE_MIOS_UUID,
        .perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        .attr = {
            .attr_max_len = GATTS_CHAR_VAL_LEN_MAX,
            .attr_len     = sizeof(attr_value_str),
            .attr_value   = attr_value_str,
        },
        .property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
    },
};

void gatts_create_characteristics(esp_ble_gatts_cb_param_t *param, struct gatts_char_inst* chars, size_t len) {
    gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;
    esp_ble_gatts_start_service(gl_profile_tab[PROFILE_APP_ID].service_handle);

    for (size_t i = 0; i < len; i++) {
        gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_APP_ID].char_uuid.uuid.uuid16 = gl_device_characteristics[i].uuid16;

        esp_err_t err = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_APP_ID].service_handle,
                    &gl_profile_tab[PROFILE_APP_ID].char_uuid,
                    gl_device_characteristics[i].perm,
                    gl_device_characteristics[i].property,
                    &gl_device_characteristics[i].attr, NULL);
        if (err) {
            ESP_LOGE(TAG_BT_MG, "ACF, EC =%x", err);
        }
    }
}

void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_REG_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
            /* GATTS_SERVICE_UUID_DEVICE */
            //gatts_create_service(gatts_if, true, 0x00, GATTS_SERVICE_DEVICE_UUID);
            /* GATTS_SERVICE_UUID_CONFIG */
            // gatts_create_service(gatts_if, true, 0x01, GATTS_SERVICE_CONFIG_UUID);
            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(BT_DEVICE_NAME);
            if (set_dev_name_ret){
                ESP_LOGE(TAG_BT_MG, "set device name failed, error code = %x", set_dev_name_ret);
            }

            esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
            if (raw_adv_ret){
                ESP_LOGE(TAG_BT_MG, "config raw adv data failed, error code = %x ", raw_adv_ret);
            }
            adv_config_done |= adv_config_flag;

            esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
            if (raw_scan_ret){
                ESP_LOGE(TAG_BT_MG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
            }
            adv_config_done |= scan_rsp_config_flag;
 
            //const uint8_t *mac = esp_bt_dev_get_address();
            ESP_LOGI(TAG_BT_MG, "%d", sizeof(gatt_db2));

            esp_err_t create_attr_ret1 = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_DEVICE_NB, 0);
            if (create_attr_ret1){
                ESP_LOGE(TAG_BT_MG, "create attr table failed, error code = %x", create_attr_ret1);
            }
            esp_err_t create_attr_ret2 = esp_ble_gatts_create_attr_tab(gatt_db2, gatts_if, HRS_CONFIG_NB, 1);
            if (create_attr_ret2){
                ESP_LOGE(TAG_BT_MG, "create attr table failed, error code = %x", create_attr_ret2);
            }
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            if (param->add_attr_tab.status != ESP_GATT_OK){
                ESP_LOGE(TAG_BT_MG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            }
            /*else if (param->add_attr_tab.num_handle != HRS_XDX_NB){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, HRS_IDX_NB);
            }*/
            else {
                ESP_LOGI(TAG_BT_MG, "create attribute table successfully, the number handle = %d\n",param->add_attr_tab.num_handle);
                
                memcpy(gatt_db2_table, param->add_attr_tab.handles, sizeof(gatt_db2_table));
                esp_ble_gatts_start_service(gatt_db2_table[0]);
            }
            break;
        }
        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_CREATE_EVT, status %d, service_handle %d, service_id %d", param->create.status, param->create.service_handle, param->create.service_id.id.uuid.uuid.uuid16);

            /*if (param->create.service_id.id.uuid.uuid.uuid16 == GATTS_SERVICE_DEVICE_UUID) { 
                gatts_create_characteristics(param, gl_device_characteristics, GATTS_SERVICE_CHAR_COUNT);
            }*/
            /*
            if (param->create.service_id.id.uuid.uuid.uuid16 == GATTS_SERVICE_DEVICE_UUID) { 
                //for (size_t i = 0; i < GATTS_SERVICE_CHAR_COUNT; i++) {
                    gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;
                    gl_profile_tab[PROFILE_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
                    gl_profile_tab[PROFILE_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_DEVICE_NAME_UUID;
                    gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;

                    esp_ble_gatts_start_service(gl_profile_tab[PROFILE_APP_ID].service_handle);

                    esp_gatt_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
                    
                    esp_err_t err1 = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_APP_ID].service_handle,
                                &gl_profile_tab[PROFILE_APP_ID].char_uuid,
                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                esp_gatt_property,
                                &gatts_demo_char1_val, NULL);
                    if (err1) {
                        ESP_LOGE(TAG_BT_MG, "ACF, EC =%x", err1);
                    }

                    gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;
                    gl_profile_tab[PROFILE_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
                    gl_profile_tab[PROFILE_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_DEVICE_MIOS_UUID;
                    gl_profile_tab[PROFILE_APP_ID].service_handle = param->create.service_handle;

                    esp_ble_gatts_start_service(gl_profile_tab[PROFILE_APP_ID].service_handle);

                    esp_gatt_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

                    esp_err_t err2 = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_APP_ID].service_handle,
                                &gl_profile_tab[PROFILE_APP_ID].char_uuid,
                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                esp_gatt_property,
                                &gatts_demo_char1_val, NULL);
                    if (err2) {
                        ESP_LOGE(TAG_BT_MG, "ACF, EC =%x", err2);
                    }
                //}
            }
            */
            break;
        case ESP_GATTS_READ_EVT: {
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_READ_EVT, conn_id %d, trans_id %d, handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);

            /*esp_gatt_rsp_t rsp;

            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;

            read_bt_data = "{}";

            uint8_t * uhex = (uint8_t *)read_bt_data;
            int size = strlen(read_bt_data);

            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_READ_EVT_VALUE: %s", read_bt_data);
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_READ_EVT_VALUE_PS: %d", size);

            rsp.attr_value.len = size;

            for(int i = 0; i < size; i += 1)
            {
              rsp.attr_value.value[i] = uhex[i];
            }

            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);*/
            break;
        }
        case ESP_GATTS_WRITE_EVT: {
            /*
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_WRITE_EVT, conn_id %d, trans_id %d, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);



            if (!param->write.is_prep){
                ESP_LOGI(TAG_BT_MG, "ESP_GATTS_WRITE_EVT, value len %d", param->write.len);

                char write_buffer[param->write.len];
                strcpy(write_buffer,(char*)param->write.value);
    
                if (gatt_db2_table[CONFIG_CHAR_VAL_WIFI_SSID] == param->write.handle) {
                    ESP_LOGI(TAG_BT_MG, "ssid");
                    ESP_LOGI(TAG_BT_MG, "write_buffer: %s", write_buffer);
                }
                if (gatt_db2_table[CONFIG_CHAR_VAL_WIFI_PASS] == param->write.handle) {
                    ESP_LOGI(TAG_BT_MG, "pass");
                    ESP_LOGI(TAG_BT_MG, "write_buffer: %s", write_buffer);

                    
                }

                esp_ble_gatts_set_attr_value(param->write.handle,param->write.len,param->write.value);




                esp_log_buffer_hex(TAG_BT_MG, param->write.value, param->write.len);
            }
            write_event_env(gatts_if, &prepare_write_env, param);
            break;
            */
           if (!param->write.is_prep){
                // the data length of gattc write  must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
                ESP_LOGI(TAG_BT_MG, "GATT_WRITE_EVT, handle = %d, value len = %d, value :", param->write.handle, param->write.len);
                esp_log_buffer_hex(TAG_BT_MG, param->write.value, param->write.len);


                if (gatt_db2_table[CONFIG_CHAR_CFG_READY] == param->write.handle && param->write.len == 2){
                    uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                    if (descr_value == 0x0001){
                        ESP_LOGI(TAG_BT_MG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i % 0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gatt_db2_table[CONFIG_CHAR_VAL_READY],
                                                sizeof(notify_data), notify_data, false);
                    } else if (descr_value == 0x0002){
                        ESP_LOGI(TAG_BT_MG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i % 0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gatt_db2_table[CONFIG_CHAR_VAL_READY],
                                            sizeof(indicate_data), indicate_data, true);
                    } else if (descr_value == 0x0000){
                        ESP_LOGI(TAG_BT_MG, "notify/indicate disable ");
                    }else{
                        ESP_LOGE(TAG_BT_MG, "unknown descr value");
                        esp_log_buffer_hex(TAG_BT_MG, param->write.value, param->write.len);
                    }

                } else {
                    ESP_LOGI(TAG_BT_MG, "CONFIG_CHAR_VAL_READY Test ");
                   // const uint8_t indicate_data[2] = {0x01, 0x02};
                   // esp_log_buffer_hex(TAG_BT_MG, indicate_data, sizeof(indicate_data));

                    esp_ble_gatts_set_attr_value(gatt_db2_table[CONFIG_CHAR_VAL_READY], param->write.len,  param->write.value);

                    /* Open NVS Storage Namespace */
                    nvs_handle_t handle;
                    esp_err_t err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
                    ESP_ERROR_CHECK(err);

                    err = nvs_set_i32(handle, MIHOME_STORAGE_IS_CONFIG, 0x01);
                    ESP_ERROR_CHECK(err);
                    err = nvs_commit(handle);
                    ESP_ERROR_CHECK(err);


                    /*esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gatt_db2_table[CONFIG_CHAR_VAL_READY],
                                            sizeof(indicate_data), indicate_data, true);*/


                    //esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, indicate_data);
                  

                }

                /* send response when param->write.need_rsp is true*/
                if (param->write.need_rsp){
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            } else{
                /* handle prepare write */
                write_event_env(gatts_if, &prepare_write_env, param);
            }
      	    break;
        }
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(TAG_BT_MG,"ESP_GATTS_EXEC_WRITE_EVT");
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            exec_write_event_env(&prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_UNREG_EVT:
            break;
        case ESP_GATTS_ADD_INCL_SRVC_EVT:
            break;
        case ESP_GATTS_ADD_CHAR_EVT: {
            break;
        }
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            /*gl_profile_tab[PROFILE_APP_ID].descr_handle = param->add_char_descr.attr_handle;
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_ADD_CHAR_DESCR_EVT, status %d, attr_handle %d, service_handle %d",
                     param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);*/
            break;
        case ESP_GATTS_DELETE_EVT:
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_STOP_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT: {
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));

            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;     // timeout = 400*10ms = 4000ms

            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_APP_ID].conn_id = param->connect.conn_id;

            //update led color - connected
            //update_status_led(HEX_COLOR_YELLOW);

            //start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            esp_ble_gap_start_advertising(&adv_params);
            //update led color - advertising
            //update_status_led(HEX_COLOR_BLUE);
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(TAG_BT_MG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
            if (param->conf.status != ESP_GATT_OK){
                esp_log_buffer_hex(TAG_BT_MG, param->conf.value, param->conf.len);
            }
            break;
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        default:
            break;
    }
}

void write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.need_rsp){
        if (param->write.is_prep){
            if (prepare_write_env->prepare_buf == NULL) {
                prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE*sizeof(uint8_t));
                prepare_write_env->prepare_len = 0;
                if (prepare_write_env->prepare_buf == NULL) {
                    ESP_LOGE(TAG_BT_MG, "Gatt_server prep no mem\n");
                    status = ESP_GATT_NO_RESOURCES;
                }
            } else {
                if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_OFFSET;
                } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                    status = ESP_GATT_INVALID_ATTR_LEN;
                }
            }

            esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);

            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK) {
               ESP_LOGE(TAG_BT_MG, "SEND_RES_ERROR");
            }
            free(gatt_rsp);
            if (status != ESP_GATT_OK) {
                return;
            }
            memcpy(prepare_write_env->prepare_buf + param->write.offset,
                   param->write.value,
                   param->write.len);
            prepare_write_env->prepare_len += param->write.len;
        } else {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
        }
    }
}

void exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param) {
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
        esp_log_buffer_hex(TAG_BT_MG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(TAG_BT_MG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}






/*
void parse_bt_json_playload(char *data) {
    cJSON *json_obj = cJSON_Parse(data);
    const cJSON *type = NULL;

    if (json_obj == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGI(TAG_BT_MG, " error before: %s", error_ptr);
        }
        goto end;
    }

    type = cJSON_GetObjectItemCaseSensitive(json_obj, "type");

    if (cJSON_IsString(type) && (type->valuestring != NULL))
    {
        char *typeValue = type->valuestring;

        if (strcmp(typeValue, "RESTART") == 0) {
            // Restart to boot and connect to the MiHome mihomecloud
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            esp_restart();
        }

        if (strcmp(typeValue, "INFO") == 0) {
            cJSON *root;
            esp_chip_info_t chip_info;
            esp_chip_info(&chip_info);

            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "mh_version", MIHOME_VERSION);
            cJSON_AddNumberToObject(root, "num_cores", chip_info.cores);
            cJSON_AddNumberToObject(root, "revision", chip_info.revision);
            cJSON_AddNumberToObject(root, "flash", spi_flash_get_chip_size() / (1024 * 1024));
            cJSON_AddStringToObject(root, "device_name", BT_DEVICE_NAME);

            read_bt_data = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
        }

        if (strcmp(typeValue, "CONFIG") == 0) {
            cJSON *json_data = cJSON_GetObjectItemCaseSensitive(json_obj, "data");

            cJSON *ssidValue = cJSON_GetObjectItemCaseSensitive(json_data, "ssid");
            ESP_LOGI(TAG_BT_MG, "ssid: %s", ssidValue->valuestring);

            cJSON *passwordValue = cJSON_GetObjectItemCaseSensitive(json_data, "password");
            ESP_LOGI(TAG_BT_MG, "password: %s", passwordValue->valuestring);

            cJSON *micloudurlValue = cJSON_GetObjectItemCaseSensitive(json_data, "cloud_url");
            ESP_LOGI(TAG_BT_MG, "cloud_url: %s", micloudurlValue->valuestring);

            cJSON *micloudclouduuidValue = cJSON_GetObjectItemCaseSensitive(json_data, "cloud_uuid");
            ESP_LOGI(TAG_BT_MG, "cloud_uuid: %s", micloudclouduuidValue->valuestring);

            //goto save_config_status;

            //save_config_status:
                // Update the device sync info
                cJSON *root;
                esp_chip_info_t chip_info;
                esp_chip_info(&chip_info);

                root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "mh_version", MIHOME_VERSION);
                cJSON_AddNumberToObject(root, "num_cores", chip_info.cores);
                cJSON_AddNumberToObject(root, "revision", chip_info.revision);
                cJSON_AddNumberToObject(root, "flash", spi_flash_get_chip_size() / (1024 * 1024));
                cJSON_AddStringToObject(root, "device_name", BT_DEVICE_NAME);
                //cJSON_AddNumberToObject(root, "is_configured", is_configured);

                read_bt_data = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
        }

        ESP_LOGI(TAG_BT_MG, "BT_Payload_Type: %s", typeValue);
        goto end;
    } else {
        goto end;
    }

    end:
      cJSON_Delete(json_obj);
}
*/