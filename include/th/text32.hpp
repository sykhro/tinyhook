#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace th {
    // Read-only decoder for the *target's* 24-byte, release-mode MSVC wide-string
    // layout. Never reinterpret this as the host's std::wstring or host pointers.
    // Reader signature: bool(uint32_t address, void* destination, size_t size).
    template<class Read>
    bool read_msvc_wstring32(std::uint32_t object,std::span<std::uint8_t> out,
                            std::uint32_t& length,Read&& read) noexcept {
        length=0;
        std::array<std::uint8_t,24> header{},after{};
        if(!object || object>UINT32_MAX-23 || (object&1) ||
           !read(object,header.data(),header.size()))return false;
        auto word=[&](unsigned offset){
            std::uint32_t v=0;
            for(unsigned i=0;i<4;++i)v|=std::uint32_t(header[offset+i])<<(i*8);
            return v;
        };
        auto size=word(16),capacity=word(20);
        if(!size || size>capacity || size>out.size()/2 || capacity>0x3fffffffu)return false;
        const auto bytes=size*2;
        if(capacity<8) {
            if(size>7)return false;
            std::memcpy(out.data(),header.data(),bytes);
        } else {
            auto pointer=word(0);
            if(!pointer || (pointer&1) || pointer>UINT32_MAX-(bytes-1) ||
               !read(pointer,out.data(),bytes))return false;
        }
        // Best-effort race check. Does not promise atomicity against concurrent writers.
        if(!read(object,after.data(),after.size()) || after!=header)return false;
        length=bytes;
        return true;
    }
}
