idf_component_register(
				SRCS 		"libs/cJSON.c"
							"libs/ws2812.c"
							#"libs/fmt/src/format.cc"
							#"libs/fmt/src/os.cc"

							"main.cpp" 
							"core/core.cpp" 
							"core/sys/utils/utils.cpp"
							"core/sys/log/log.cpp"
							"core/sys/wifi/wifi_manager.cpp"
							"core/sys/ble/ble_manager.cpp"
							"core/application/service/service.cpp"
                INCLUDE_DIRS ".")

# Build static library, do not build test executables
option(BUILD_SHARED_LIBS OFF)
option(BUILD_TESTING OFF)

# Unfortunately the library performs install and export. Would
# have been nice if devs made that an option like BUILD_SHARED_LIBS
# and BUILD_TESTING. Override install() and export() to do nothing
# instead.
# function(install)
# endfunction()

# function(export)
# endfunction()

# Import cJSON targets
# add_subdirectory(libs/cJSON)

# Link tinyxml2 to main component
# target_link_libraries(${COMPONENT_LIB} PUBLIC cJSON)