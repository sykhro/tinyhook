include(CheckCXXSourceCompiles)

check_cxx_source_compiles([=[
    #include <bit>
    #include <cstdint>
    #include <span>
    #if defined(_MSVC_LANG)
    static_assert(_MSVC_LANG >= 202002L);
    #else
    static_assert(__cplusplus >= 202002L);
    #endif
    int main() {
        std::uint32_t data[] = {1, 2};
        std::span<std::uint32_t> s(data);
        return std::bit_cast<std::int32_t>(s[0]) - 1;
    }
]=] TINYHOOK_HAS_CXX20)

if(NOT TINYHOOK_HAS_CXX20)
    message(FATAL_ERROR
        "TinyHook requires a C++20-capable compiler and standard library in C++20 "
        "or newer mode. Use a supplied preset, or configure with "
        "-DCMAKE_CXX_STANDARD=20 (or your toolchain's equivalent). "
        "TinyHook does not override the caller's compilation policy."
    )
endif()
