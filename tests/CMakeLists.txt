add_executable(tinyhook.tests.core)
target_sources(tinyhook.tests.core PRIVATE core.cpp)
target_link_libraries(tinyhook.tests.core PRIVATE tinyhook::core)
add_test(NAME tinyhook.core COMMAND tinyhook.tests.core)

add_executable(tinyhook.tests.text32)
target_sources(tinyhook.tests.text32 PRIVATE text32.cpp)
target_link_libraries(tinyhook.tests.text32 PRIVATE tinyhook::core)
add_test(NAME tinyhook.text32 COMMAND tinyhook.tests.text32)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_executable(tinyhook.tests.gateway64)
    target_sources(tinyhook.tests.gateway64 PRIVATE gateway.cpp)
    target_link_libraries(tinyhook.tests.gateway64 PRIVATE tinyhook::core)
    add_test(NAME tinyhook.gateway64 COMMAND tinyhook.tests.gateway64)

    add_executable(tinyhook.tests.gateway32)
    target_sources(tinyhook.tests.gateway32 PRIVATE gateway32.cpp)
    target_link_libraries(tinyhook.tests.gateway32 PRIVATE tinyhook::core)
    add_test(NAME tinyhook.gateway32 COMMAND tinyhook.tests.gateway32)
    set_tests_properties(tinyhook.gateway32 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 15)
endif()

if(NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(TARGET tinyhook.host)
        add_test(
            NAME tinyhook.websocket
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/integration.py" $<TARGET_FILE:tinyhook.host>
        )
        set_tests_properties(tinyhook.websocket PROPERTIES TIMEOUT 30)
        if(TINYHOOK_WITH_LUAU AND TINYHOOK_WITH_COMPILER)
            add_test(
                NAME tinyhook.luau
                COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/integration.py" $<TARGET_FILE:tinyhook.host> --luau
            )
            set_tests_properties(tinyhook.luau PROPERTIES TIMEOUT 30)
        endif()
    endif()
    if(TARGET tinyhook.demo)
        add_test(
            NAME tinyhook.windows-injection
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/windows_smoke.py"
                    $<TARGET_FILE_DIR:tinyhook.client> --bits "${CMAKE_SIZEOF_VOID_P}"
        )
        set_tests_properties(tinyhook.windows-injection PROPERTIES TIMEOUT 90)
    endif()
    if(NOT WIN32)
        add_test(
            NAME tinyhook.cmake-contract
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/cmake_contract.py"
                    "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}" "${CMAKE_CXX_COMPILER}" "${CMAKE_C_COMPILER}"
        )
        set_tests_properties(tinyhook.cmake-contract PROPERTIES TIMEOUT 120)
    endif()
endif()

if(TARGET tinyhook.pick AND NOT CMAKE_CROSSCOMPILING)
    add_test(
        NAME tinyhook.window-picker
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/mac_picker.py"
                $<TARGET_FILE:tinyhook.host> $<TARGET_FILE:tinyhook.pick>
    )
    set_tests_properties(tinyhook.window-picker PROPERTIES TIMEOUT 30)
endif()
