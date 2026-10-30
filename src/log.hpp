#pragma once

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#ifdef _WIN32
#include <spdlog/sinks/msvc_sink.h>
#endif

namespace th::log {
    inline spdlog::level::level_enum configured_level() {
        if(const char* value=std::getenv("TINYHOOK_LOG")) {
            const std::string_view name(value);
            if(name=="warn")return spdlog::level::warn;
            const auto level=spdlog::level::from_str(std::string(name));
            if(level!=spdlog::level::off || name=="off")return level;
        }
        return spdlog::level::info;
    }

    inline void configure_console(std::string_view name) {
        auto sink=std::make_shared<spdlog::sinks::stderr_sink_mt>();
        auto logger=std::make_shared<spdlog::logger>(std::string(name),std::move(sink));
        logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
        logger->set_level(configured_level());
        spdlog::set_default_logger(std::move(logger));
    }

#ifdef _WIN32
    inline void configure_debugger(std::string_view name) {
        auto sink=std::make_shared<spdlog::sinks::msvc_sink_mt>();
        auto logger=std::make_shared<spdlog::logger>(std::string(name),std::move(sink));
        logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
        logger->set_level(configured_level());
        spdlog::set_default_logger(std::move(logger));
    }
#endif
}
