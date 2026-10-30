include(CMakePackageConfigHelpers)

install(
    TARGETS tinyhook.core
    EXPORT tinyhook-targets
    FILE_SET HEADERS COMPONENT tinyhook-development
)
install(
    EXPORT tinyhook-targets
    FILE tinyhook-targets.cmake
    NAMESPACE tinyhook::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tinyhook"
    COMPONENT tinyhook-development
)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/tinyhook-config.cmake.in"
    "${PROJECT_BINARY_DIR}/tinyhook-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tinyhook"
)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/tinyhook-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
    ARCH_INDEPENDENT
)
install(
    FILES "${PROJECT_BINARY_DIR}/tinyhook-config.cmake"
          "${PROJECT_BINARY_DIR}/tinyhook-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tinyhook"
    COMPONENT tinyhook-development
)
install(
    FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/tinyhook"
    COMPONENT tinyhook-runtime
)
install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/scripts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyhook/scripts"
    COMPONENT tinyhook-runtime
    FILES_MATCHING PATTERN "*.luau"
)
