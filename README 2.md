# Hello World Example

Starts a FreeRTOS task to print "Hello World"

See the README.md file in the upper level 'examples' directory for more information about examples.

. $HOME/esp/esp-idf/export.sh


#include "drivers/I2CMasterDevice.h"
#include "drivers/Master.h"

#include "drivers/I2CCore.h"

#include "drivers/BME280Core.h"

EMBED_FILES conf.json

//extern const char config_json_start[] asm("_binary_conf_json_start");
//extern const char config_json_end[]   asm("_binary_conf_json_end");

							//"drivers/I2CCommandLink.cpp"
							//"drivers/I2CMasterDevice.cpp"
							//"drivers/Master.cpp"
							//"drivers/I2CCore.cpp"
							//"drivers/BME280Core.cpp"

							   /*
	    ==============================================================================================================================
		==============================================================================================================================
		==============================================================================================================================
		==============================================================================================================================
		*/


	    ESP_LOGI(TAG, "i2c_scanner");

	    core::io::i2c::Master i2c_master(I2C_NUM_0,                       	// I2C Port 0
                       GPIO_NUM_22,                     					// SCL pin
                       false,                           					// SCL internal pullup NOT enabled
                       GPIO_NUM_23,                     					// SDA pin
                       false,                           					// SDA internal pullup NOT enabled
                       100 * 1000);                      					// Clock Frequency - 100kHz

	    bool res = false;
	    auto device = i2c_master.create_device<application::ic2core::I2CCore>(0x77);

	    if (device->is_present()) {
			ESP_LOGI(TAG, "is_present");	
			bool measuring = false;
            bool loading_from_nvm = false;

            while (!device->read_status(measuring, loading_from_nvm) || loading_from_nvm)
            {
                ESP_LOGI(TAG, "Waiting for BME280 to complete reset operation..");
            }

            res = device->configure_sensor(application::io::i2c::BME280Core::SensorMode::Normal,
                                           application::io::i2c::BME280Core::OverSampling::Oversamplingx1,
                                           application::io::i2c::BME280Core::OverSampling::Oversamplingx1,
                                           application::io::i2c::BME280Core::OverSampling::Oversamplingx1,
                                           application::io::i2c::BME280Core::StandbyTimeMS::ST_1000,
                                           application::io::i2c::BME280Core::FilterCoeff::FC_OFF);

            ESP_LOGI(TAG, "Configure BME280: %d", res);

            if (res)
            {
                ESP_LOGI(TAG, "BME280 initialized, ID: %i", device->read_id());

                float temperature, humidity, pressure;
		        device->read_measurements(humidity, pressure, temperature);

		        ESP_LOGI(TAG,  "BME280 Temperature  (degC)  = %f", temperature);
		        ESP_LOGI(TAG,  "BME280 Humidity     (RH)   = %f", humidity);
		        ESP_LOGI(TAG,  "BME280 Pressure     (hPa)   = %f", pressure / 100);
		        ESP_LOGI(TAG,  "Barometric Pressure (in Hg) = %f", pressure / static_cast<float> (3386.389));
		        ESP_LOGI(TAG,  "........................................" );
            }
            else
            {
                ESP_LOGI(TAG, "Could not init BME280");
            }
	    } else {
	    	ESP_LOGI(TAG, "is_not_present");
	    }



	    /*
	    ==============================================================================================================================
		==============================================================================================================================
		==============================================================================================================================
		==============================================================================================================================
		*/