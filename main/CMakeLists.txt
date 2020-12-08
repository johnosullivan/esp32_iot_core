idf_component_register(
				SRCS 		"libs/cJSON.c"
							"libs/ws2812.c"
							"libs/fmt/src/format.cc"
							"libs/fmt/src/os.cc"

							"main.cpp" 
							"core/core.cpp" 
							"core/sys/utils/utils.cpp"
							"core/sys/log/log.cpp"
							"core/sys/wifi/wifi_manager.cpp"
							"core/sys/ble/ble_manager.cpp"
							"core/application/service/service.cpp"
                INCLUDE_DIRS "."
)