#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include "core/constants.h"

#include "esp_wifi.h"	
#include "sdkconfig.h"	
#include "driver/rmt.h"
#include "driver/gpio.h"

#include "libs/ws2812.h"

namespace core::sys::utils
{
	#define RMT_TX_CHANNEL RMT_CHANNEL_0

	void wifi_scanner();

	void set_logging_levels();

	void ping_ws2812_signal();

	void init_status_led(uint8_t pin);

	void update_status_led(const char *color_hex);
}

#endif /* UTILS_H_INCLUDED */