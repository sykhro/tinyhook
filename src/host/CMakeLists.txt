find_package(spdlog CONFIG REQUIRED)

if(TINYHOOK_BUILD_COMPILER_TOOL AND NOT TINYHOOK_WITH_LUAU)
    message(FATAL_ERROR "TINYHOOK_BUILD_COMPILER_TOOL requires TINYHOOK_WITH_LUAU")
endif()

if(TINYHOOK_WITH_LUAU)
    # A package manager or an explicitly selected provider supplies these targets.
    # Dependency acquisition is deliberately outside the product build graph.
    find_package(Luau CONFIG REQUIRED)
endif()

add_executable(tinyhook.host)
set_target_properties(tinyhook.host PROPERTIES OUTPUT_NAME th-host)
target_sources(
    tinyhook.host
    PRIVATE main.cpp script.cpp
    PRIVATE FILE_SET HEADERS FILES script.hpp
)
target_link_libraries(tinyhook.host PRIVATE tinyhook::core spdlog::spdlog)
target_include_directories(tinyhook.host PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
    tinyhook.host
    PRIVATE
        TH_WITH_LUAU=$<BOOL:${TINYHOOK_WITH_LUAU}>
        TH_WITH_COMPILER=$<BOOL:${TINYHOOK_WITH_COMPILER}>
)

if(TINYHOOK_WITH_LUAU)
    target_link_libraries(tinyhook.host PRIVATE Luau::VM)
    if(TINYHOOK_WITH_COMPILER)
        target_link_libraries(tinyhook.host PRIVATE Luau::Compiler)
    endif()
    if(TINYHOOK_BUILD_COMPILER_TOOL)
        add_executable(tinyhook.compile)
        set_target_properties(tinyhook.compile PROPERTIES OUTPUT_NAME th-compile)
        target_sources(tinyhook.compile PRIVATE compile.cpp)
        target_link_libraries(tinyhook.compile PRIVATE tinyhook::core Luau::Compiler spdlog::spdlog)
        target_include_directories(tinyhook.compile PRIVATE "${PROJECT_SOURCE_DIR}/src")
        if(TINYHOOK_INSTALL)
            install(TARGETS tinyhook.compile RUNTIME COMPONENT tinyhook-tools)
        endif()
    endif()
endif()

if(TINYHOOK_INSTALL)
    install(TARGETS tinyhook.host RUNTIME COMPONENT tinyhook-runtime)
endif()

if(APPLE)
    enable_language(OBJCXX)
    add_executable(tinyhook.pick ../macos/picker.mm)
    set_target_properties(tinyhook.pick PROPERTIES OUTPUT_NAME th-pick OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_compile_options(tinyhook.pick PRIVATE -fobjc-arc)
    target_link_libraries(tinyhook.pick PRIVATE tinyhook::core "-framework AppKit" "-framework CoreGraphics")
    if(TINYHOOK_INSTALL)
        install(TARGETS tinyhook.pick RUNTIME COMPONENT tinyhook-runtime)
    endif()
endif()
