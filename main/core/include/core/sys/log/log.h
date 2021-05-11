#pragma once

#include <string>
#include <mutex>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
//#include "fmt/format.h"
#pragma GCC diagnostic pop
#include <iostream>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

namespace core::sys::log
{
    /*
    class Log
    {
        public:
            static std::mutex guard;
            static fmt::basic_memory_buffer<char, 500> buff;
            static constexpr const char warning_level = 'W';
            static constexpr const char error_level = 'E';
            static constexpr const char information_level = 'I';
            static constexpr const char verbose_level = 'V';
            static constexpr const char debug_level = 'D';

#ifdef ESP_PLATFORM
            template<typename... Args>
            static void log(const char level, const std::string& tag, Args&&... args)
            {
                std::unique_lock<std::mutex> lock(guard);
                buff.clear();
                fmt::format_to(buff, args...);

                if (level == error_level) {
                    ESP_LOGE(tag.c_str(), "%s", fmt::to_string(buff).c_str());
                } else if (level == warning_level) {
                    ESP_LOGW(tag.c_str(), "%s", fmt::to_string(buff).c_str());
                } else if (level == information_level) {
                    ESP_LOGI(tag.c_str(), "%s", fmt::to_string(buff).c_str());
                } else if (level == verbose_level) {
                    ESP_LOGV(tag.c_str(), "%s", fmt::to_string(buff).c_str());
                } else {
                    ESP_LOGD(tag.c_str(), "%s", fmt::to_string(buff).c_str());
                }
            }
#else
            template<typename... Args>
            static void log(const char level, const std::string& tag, Args&&... args)
            {
                std::unique_lock<std::mutex> lock(guard);
                buff.clear();
                fmt::format_to(buff, args...);
                std::cout << "(" << level << ")" << tag << ": " << fmt::to_string(buff) << std::endl;
            }
#endif

            template<typename... Args>
            static void error(const std::string& tag, Args&&... args)
            {
                log(error_level, tag, args...);
            }

            template<typename Arg>
            static void error(const std::string& tag, const Arg val)
            {
                log(error_level, tag, "{}", val);
            }

            template<typename... Args>
            static void warning(const std::string& tag, Args&&... args)
            {
                log(warning_level, tag, args...);
            }

            template<typename Arg>
            static void warning(const std::string& tag, const Arg val)
            {
                log(warning_level, tag, "{}", val);
            }

            template<typename... Args>
            static void info(const std::string& tag, Args&&... args)
            {
                log(information_level, tag, args...);
            }

            template<typename Arg>
            static void info(const std::string& tag, const Arg val)
            {
                log(information_level, tag, "{}", val);
            }

            template<typename... Args>
            static void debug(const std::string& tag, Args&&... args)
            {
                log(debug_level, tag, args...);
            }

            template<typename Arg>
            static void debug(const std::string& tag, const Arg val)
            {
                log(debug_level, tag, "{}", val);
            }

            template<typename... Args>
            static void verbose(const std::string& tag, Args&&... args)
            {
                log(verbose_level, tag, args...);
            }

            template<typename Arg>
            static void verbose(const std::string& tag, const Arg val)
            {
                log(verbose_level, tag, "{}", val);
            }
    };
    */
}