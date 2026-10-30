add_library(tinyhook.core INTERFACE)
add_library(tinyhook::core ALIAS tinyhook.core)

set_target_properties(tinyhook.core PROPERTIES EXPORT_NAME core)

target_sources(
    tinyhook.core
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
        FILES
            protocol.hpp
            websocket.hpp
            gateway.hpp
            gateway32.hpp
            text32.hpp
)
