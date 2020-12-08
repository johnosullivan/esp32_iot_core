#ifndef SERVICE_H_INCLUDED
#define SERVICE_H_INCLUDED

#include "core/constants.h"

#include "esp_http_client.h"
#include "esp_websocket_client.h"

#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

namespace core::application::service
{
	#define MAX_HTTP_OUTPUT_BUFFER 2048
	#define NO_DATA_TIMEOUT_SEC 120

	void start_service();
}

#endif /* SERVICE_H_INCLUDED */