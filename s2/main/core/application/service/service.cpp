#include "core/application/service/service.h"
#include "core/sys/utils/utils.h"

using namespace core::sys;

namespace core::application::service
{
	static const char TAG[] = "mihome_esp32_service";
	/*
	 Secure Websocket timer and handles
	*/
	static TimerHandle_t shutdown_signal_timer;
	static SemaphoreHandle_t shutdown_sema;

	esp_websocket_client_handle_t client;

	struct mihome_settings_t mihome_settings = { "", "" };

	static void shutdown_signaler(TimerHandle_t xTimer) {
	    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
	    xSemaphoreGive(shutdown_sema);
	}

	void processing_ws_data(char *data) {
	    cJSON *json_obj = cJSON_Parse(data);
	    const cJSON *type = NULL;

	    if (json_obj == NULL)
	    {
	        const char *error_ptr = cJSON_GetErrorPtr();
	        if (error_ptr != NULL)
	        {
	            ESP_LOGI(TAG, " error before: %s", error_ptr);
	        }
	        goto end;
	    }

	    type = cJSON_GetObjectItemCaseSensitive(json_obj, "type");

	    if (cJSON_IsString(type) && (type->valuestring != NULL))
	    {
	        char *typeValue = type->valuestring;

	        ESP_LOGI(TAG, "WSType: %s", typeValue);

	        if (strcmp(typeValue, PING_COMMAND) == 0) {
	            utils::ping_ws2812_signal();
	        }

	        goto end;
	    } else {
	        goto end;
	    }

	    end:
	      cJSON_Delete(json_obj);
	}

	static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
	    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
	    switch (event_id) {
	        case WEBSOCKET_EVENT_CONNECTED: {
	            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");

	            // grabs the uuid for auth
	            esp_err_t err;
	            nvs_handle_t handle;
	            size_t mihome_settings_size = sizeof(mihome_settings);

	            err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
	            ESP_ERROR_CHECK(err);

	            err = nvs_get_blob(handle, MIHOME_STORAGE_SETTINGS, &mihome_settings, &mihome_settings_size);
	            ESP_ERROR_CHECK(err);

	            char uuid[100];
	            sprintf(uuid, "%s", mihome_settings.cloud_uuid);

	            // builds json body
	            cJSON *root;
	            root = cJSON_CreateObject();
	            cJSON_AddStringToObject(root, "type", "AUTH");
	            cJSON_AddStringToObject(root, "node_id", uuid);

	            char *auth_data;
	            auth_data = cJSON_PrintUnformatted(root);
	            cJSON_Delete(root);

	            ESP_LOGI(TAG, "auth_data: %s", auth_data);

	            esp_websocket_client_send_text(client, auth_data, strlen(auth_data), portMAX_DELAY);
	            break;
	        }
	        case WEBSOCKET_EVENT_DISCONNECTED: {
	            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
	            break;
	        }
	        case WEBSOCKET_EVENT_DATA: {
	            if (data->payload_len != 0) {
	                ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
	                ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
	                ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
	                ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d", data->payload_len, data->data_len, data->payload_offset);

	                char rcv_buffer[data->data_len]; 
	                strncpy(rcv_buffer, (char*)data->data_ptr, data->data_len); // TODO: BUG CORE #0 PANIC strcpy
	                ESP_LOGI(TAG, "processing_ws_data");
	                processing_ws_data(rcv_buffer);
	            }
	            xTimerReset(shutdown_signal_timer, portMAX_DELAY);
	            break;
	        }
	        case WEBSOCKET_EVENT_ERROR: {
	            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
	            break;
	        }
	    }
	}

	static void ping_ws_task(void *pvParameter) {
	  	for(;;) {
	        cJSON *root;
	        root = cJSON_CreateObject();
	        cJSON_AddStringToObject(root, "type", "PING");

	        char *ping_data = cJSON_PrintUnformatted(root);
	        cJSON_Delete(root);

	        esp_websocket_client_send_text(client, ping_data, strlen(ping_data), portMAX_DELAY);
	  		vTaskDelay(pdMS_TO_TICKS(30000));
	  	}
	}

	static void websocket_app_start(void) {
		// grabs the websocket config from nvs
	    esp_err_t err;
	    nvs_handle_t handle;
	    size_t mihome_settings_size = sizeof(mihome_settings);

	    err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
	    ESP_ERROR_CHECK(err);

	    err = nvs_get_blob(handle, MIHOME_STORAGE_SETTINGS, &mihome_settings, &mihome_settings_size);
	    ESP_ERROR_CHECK(err);

	    ESP_LOGI(TAG, "cloud_url %s", mihome_settings.cloud_url);

	    char ws_url[100];
	    sprintf(ws_url, "ws://%s/ws", mihome_settings.cloud_url);

	    esp_websocket_client_config_t websocket_cfg = { };
	    websocket_cfg.uri = ws_url;
	    
	    shutdown_signal_timer = xTimerCreate("ws_s_t", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS, pdFALSE, NULL, shutdown_signaler);
	    shutdown_sema = xSemaphoreCreateBinary();

	    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

	    client = esp_websocket_client_init(&websocket_cfg);

	    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
	    esp_websocket_client_start(client);

	    xTaskCreatePinnedToCore(&ping_ws_task, "ping_ws_task", 2048, NULL, 1, NULL, 1);
	    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
	    xSemaphoreTake(shutdown_sema, portMAX_DELAY);

	    esp_websocket_client_stop(client);
	    esp_websocket_client_destroy(client);
	}

	void start_service() {
		websocket_app_start();
	}
}