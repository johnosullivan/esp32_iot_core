#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/queue.h"

#include "utils.h"
#include "bt_manager.h"
#include "wifi_manager.h"

static const char *TAG_CORE = "esp32_hub_core";

//#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
//#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
//static const int RX_BUF_SIZE = 1024;

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_18)

#define UART UART_NUM_1

int num = 0;

#define BUF_SIZE (4096)
#define RD_BUF_SIZE (BUF_SIZE)

//static QueueHandle_t uart0_queue;

typedef struct stack {
    int cap;
    int size;
    char** item;
} Stack;

Stack* creatstack(int maxSize) {
    Stack* s;
    s=(Stack*)malloc(sizeof(Stack));
    s->item=(char**)malloc(sizeof(char)*5); 
    s->cap=maxSize;
    s->size=0;
    return s;
}

void push(Stack* s, char* data)
{
    s->item[s->size++]=data;
}
 
char* top(Stack* s)
{
    return s->item[s->size-1];
}
 
void pop(Stack* s)
{
    s->size--;
}


typedef struct {
    QueueHandle_t uart_queue;
} iridium_settings_t;

//extern struct iridium_settings_t satcom_settings;


void uart_event_task0(void *pvParameters) {
    iridium_settings_t satcom = *(iridium_settings_t *)pvParameters;

    uart_event_t event;
    //size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);

    Stack *s = creatstack(10);

    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(satcom.uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            ESP_LOGI(TAG_CORE, "uart[%d] event:", UART);
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    uart_read_bytes(UART, dtmp, event.size, portMAX_DELAY);

                    ESP_LOGI(TAG_CORE, "\n%s", dtmp);

                    char* pch = NULL;
                    pch = strtok((char*)dtmp, "\r\n");

                    while (pch != NULL)
                    {
                        printf("- %s\n", pch);
                        pch = strtok(NULL, "\r\n");
                    }

                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART);
                    xQueueReset(satcom.uart_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART);
                    xQueueReset(satcom.uart_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG_CORE, "UART_BREAK");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    break;
                //Others
                default:
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}


void iridium_config(/*iridium_settings_t *stacom*/) {
    iridium_settings_t stacom;

    uart_config_t uart_config = {
        .baud_rate = 19200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        //.source_clk = UART_SCLK_REF_TICK,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART, BUF_SIZE * 2, BUF_SIZE * 2, 20, &stacom.uart_queue, 0));

    ESP_ERROR_CHECK(uart_param_config(UART, &uart_config));

    ESP_ERROR_CHECK(uart_set_pin(UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreatePinnedToCore(&uart_event_task0, "uart_event_task0", 2048, &stacom, 12, NULL, 1);
}



void system_monitoring_task(void *pvParameter) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    /*uart_config_t uart_config = {
        .baud_rate = 19200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_REF_TICK,
    };
    int intr_alloc_flags = 0;

    ESP_ERROR_CHECK(uart_driver_install(UART, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));*/

    // Configure a temporary buffer for the incoming data
    //uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    char* data = (char*) malloc(100);

    for(;;) {
          vTaskDelay(pdMS_TO_TICKS(30000));
          // PING SATCOM AT+ COMMAND
          data = "AT+CSQ\r";
          //ESP_LOGI(TAG_CORE, "uart_write_bytes");
          uart_write_bytes(UART, data, strlen(data));

          //vTaskDelay(5000 / portTICK_PERIOD_MS);
          /*uint8_t* dd = (uint8_t*) malloc(BUF_SIZE+1);
          const int rxBytes = uart_read_bytes(UART, dd, BUF_SIZE, 4000 / portTICK_RATE_MS);
          ESP_LOGI(TAG_CORE, "uart_read_bytes");
          ESP_LOGI(TAG_CORE, "%d bytes", rxBytes);
          ESP_LOGI(TAG_CORE, "%s", dd);

          ESP_ERROR_CHECK(uart_flush(UART_NUM_1));*/
    }
}

void cb_connection_established(void *pvParameter) {
	ESP_LOGI(TAG_CORE, "connection_established"); 
}

void app_main(void)
{
    /* Print Chip Information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("[Target: %s] - %d CPU Core(s), WiFi%s%s, SR %d, %dMB %s Flash\n", 
            CONFIG_IDF_TARGET, chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", 
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "", chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024), 
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
    
    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Open NVS Storage Namespace */
    nvs_handle_t handle;
    err = nvs_open(MIHOME_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    ESP_ERROR_CHECK(err);

    /* Pull NVS State + Start BLE/WiFi Managers */
    int8_t  restart_flag = 0x00;
    int32_t configured   = 0x00;

    if (err != ESP_OK) {
        ESP_LOGE(TAG_CORE, "ERROR: (%s) NVS", esp_err_to_name(err));
    } else {
        err = nvs_get_i32(handle, MIHOME_STORAGE_IS_CONFIG, &configured);
        switch (err) {
            case ESP_OK:
                ESP_LOGD(TAG_CORE, "MIHOME_STORAGE_IS_CONFIG = %d\n", configured);
                if (configured) {
                    /* Start WiFi Manager + Callback (EVENT_STA_GOT_IP) */
                    wifi_manager_start();
                    wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_established);
                } else {
                    /* Start BLE Manager / GATTS Service */
                    bt_manager_start();
                }
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                /* Set NVS Default Configs */
                err = nvs_set_i32(handle, MIHOME_STORAGE_IS_CONFIG, 0x00);
                ESP_ERROR_CHECK(err);
                /* Commit NVS Changes */
                err = nvs_commit(handle);
                ESP_ERROR_CHECK(err);
                restart_flag = 0x01;
                break;
            default:
                ESP_LOGE(TAG_CORE, "ERROR: (%s)", esp_err_to_name(err));
        }
    }
    /* Close NVS Handle */
    nvs_close(handle);

    /* Restart Flag Equals 0x01 */
    if (restart_flag) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    /* Config Iridium SatCom via UART */
    iridium_config();

    /* Create FreeRTOS Monitoring Task */
    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);

    vTaskDelay(pdMS_TO_TICKS(10000));

    char* data = (char*) malloc(100);
    data = "AT+SBDSX\r";
    uart_write_bytes(UART, data, strlen(data));
}
