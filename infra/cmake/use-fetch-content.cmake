# Opt-in developer provider, selected ONLY by CMAKE_PROJECT_TOP_LEVEL_INCLUDES.
# Package-manager builds can omit this file entirely. Inspired by beman.exemplar.
include(FetchContent)
set(_tinyhook_lockfile "${CMAKE_CURRENT_LIST_DIR}/../../dependencies.json")
cmake_path(NORMAL_PATH _tinyhook_lockfile)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_tinyhook_lockfile}")
file(READ "${_tinyhook_lockfile}" _tinyhook_dependencies)

function(tinyhook_silence_dependency_warnings directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type "${target}" TYPE)
        if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            target_compile_options("${target}" PRIVATE
                "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-w>"
                "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-w>"
                "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/w>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w>"
            )
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        tinyhook_silence_dependency_warnings("${subdirectory}")
    endforeach()
endfunction()

function(tinyhook_provide_dependency method package_name)
    if(NOT method STREQUAL "FIND_PACKAGE")
        return()
    endif()
    string(JSON dependency ERROR_VARIABLE not_found GET "${_tinyhook_dependencies}" "${package_name}")
    if(not_found)
        return() # Threads, Python, and unrelated parent-project dependencies.
    endif()
    string(JSON name GET "${dependency}" name)
    string(JSON repo GET "${dependency}" git_repository)
    string(JSON revision GET "${dependency}" git_tag)
    string(JSON count LENGTH "${dependency}" options)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE "${last}")
            string(JSON key MEMBER "${dependency}" options "${index}")
            string(JSON value GET "${dependency}" options "${key}")
            # Normal function-local variables, consumed under CMP0077 NEW.
            set("${key}" "${value}")
        endforeach()
    endif()
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    FetchContent_Declare(
        "${name}"
        GIT_REPOSITORY "${repo}"
        GIT_TAG "${revision}"
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    FetchContent_MakeAvailable("${name}")
    # SYSTEM suppresses diagnostics from dependency headers in our code. Private
    # options also silence warnings while compiling the dependencies themselves.
    if(DEFINED ${name}_SOURCE_DIR AND EXISTS "${${name}_SOURCE_DIR}/CMakeLists.txt")
        tinyhook_silence_dependency_warnings("${${name}_SOURCE_DIR}")
    endif()
    if(package_name STREQUAL "Luau")
        if(NOT TARGET Luau::VM)
            add_library(Luau::VM ALIAS Luau.VM)
            add_library(Luau::Compiler ALIAS Luau.Compiler)
        endif()
    elseif(package_name STREQUAL "minhook")
        if(NOT TARGET minhook::minhook)
            add_library(minhook::minhook ALIAS minhook)
        endif()
    endif()
    # This provider's runtime distribution statically incorporates the dependency.
    # Keep its license with that distribution, without installing the dependency SDK.
    get_property(license_registered GLOBAL PROPERTY "tinyhook_license_${name}")
    if(TINYHOOK_INSTALL AND NOT license_registered)
        string(JSON license_file ERROR_VARIABLE no_license GET "${dependency}" license_file)
        if(no_license)
            set(license_file "LICENSE.txt")
        endif()
        install(
            FILES "${${name}_SOURCE_DIR}/${license_file}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/tinyhook"
            RENAME "${name}-LICENSE.txt"
            COMPONENT tinyhook-runtime
        )
        set_property(GLOBAL PROPERTY "tinyhook_license_${name}" TRUE)
    endif()
    set("${package_name}_FOUND" TRUE PARENT_SCOPE)
endfunction()

cmake_language(
    SET_DEPENDENCY_PROVIDER tinyhook_provide_dependency
    SUPPORTED_METHODS FIND_PACKAGE
)
