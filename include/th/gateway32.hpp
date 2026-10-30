#pragma once
#include "protocol.hpp"

namespace th {
    // PUSHFD followed by PUSHAD: saved_esp is ESP after PUSHFD, NOT entry ESP.
    struct Context32 {
        std::uint32_t edi,esi,ebp,saved_esp,ebx,edx,ecx,eax,eflags;
        std::uint32_t stack()const noexcept { return saved_esp+4; }
        std::uint32_t get(Register r)const noexcept {
            switch(r) {
                case Register::eax:return eax;
                case Register::ecx:return ecx;
                case Register::edx:return edx;
                case Register::ebx:return ebx;
                case Register::ebp:return ebp;
                case Register::esi:return esi;
                case Register::edi:return edi;
                case Register::esp:return stack();
                default:return 0;
            }
        }
    };
    static_assert(sizeof(Context32)==36 && offsetof(Context32,eax)==28);

    // cdecl observer(void* userdata, const Context32*). The original function's
    // calling convention is irrelevant: a tail jump leaves its stack untouched.
    // Supports ordinary x86 code with x87/MMX/SSE state; NOT live AVX/AVX-512 state.
    inline Bytes make_gateway32(std::uint32_t address,std::uint32_t userdata,
                                std::uint32_t callback,std::uint32_t trampoline) {
        Bytes b;
        auto emit=[&](std::initializer_list<std::uint8_t> x){b.insert(b.end(),x);};
        emit({0x9c,0x60,0x8b,0xec});       // pushfd; pushad; mov ebp,esp (context)
        emit({0x83,0xe4,0xf0});           // and esp,-16 (arbitrary mid-hook stack)
        emit({0x81,0xec});u32(b,528);     // args[16] + aligned FXSAVE area[512]
        emit({0x0f,0xae,0x44,0x24,0x10}); // fxsave [esp+16] (x87 + XMM0..7)
        emit({0xdb,0xe3});                // fninit: callback gets empty x87 stack
        emit({0xc7,0x44,0x24,0x08});u32(b,0x1f80); // default masked MXCSR
        emit({0x0f,0xae,0x54,0x24,0x08}); // ldmxcsr [esp+8]
        emit({0xfc});                     // cld: C ABI requires clear direction flag
        emit({0xc7,0x04,0x24});u32(b,userdata);
        emit({0x89,0x6c,0x24,0x04});       // mov [esp+4],ebp (Context32*)
        emit({0xb8});u32(b,callback);
        emit({0xff,0xd0});                // call eax; args are caller-cleaned
        emit({0x0f,0xae,0x4c,0x24,0x10}); // fxrstor [esp+16]
        emit({0x8b,0xe5,0x61,0x9d});       // mov esp,ebp; popad; popfd
        u8(b,0xe9);                       // jmp rel32, preserving ALL registers/flags
        auto next=address+static_cast<std::uint32_t>(b.size())+4;
        u32(b,trampoline-next);           // modulo 2^32 arithmetic is intentional
        return b;
    }
}
