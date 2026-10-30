#pragma once
#include "protocol.hpp"

namespace th {
    struct Context {
        std::uint64_t reg[16];
    };

    static_assert(sizeof(Context)==128, "sizeof(Context) must be 128 bytes");

    // Windows x64 function-entry ABI ONLY. No mid-instruction/mid-function hooks.
    // 32-byte shadow space; XMM0..5 at +32; GPR snapshot at +128; 8-byte alignment pad.
    // The original return address and stack arguments remain in place above this frame.
    constexpr std::uint32_t gateway_stack=264;

    struct Gateway {
        Bytes code;
        std::uint32_t unwind_offset{};
    };

    inline Gateway make_gateway(std::uint64_t userdata, std::uint64_t callback, std::uint64_t trampoline) {
        Gateway g;
        auto& b=g.code;

        auto emit=[&](std::initializer_list<std::uint8_t> x){
            b.insert(b.end(),x);
        };

        auto mem=[&](std::uint8_t reg,std::uint32_t offset,bool load){
            u8(b,reg>=8?0x4c:0x48);
            u8(b,load?0x8b:0x89);
            u8(b,static_cast<std::uint8_t>(0x84|((reg&7)<<3)));
            u8(b,0x24);
            u32(b,offset);
        };

        // sub rsp, 264: fixed, seven-byte prologue, represented by UNWIND_INFO below.
        emit({
            0x48,0x81,0xec
        });
        u32(b,gateway_stack);

        constexpr std::array<std::uint8_t,15> regs={
            15,14,13,12,11,10,9,8,7,6,5,3,2,1,0
        };

        for(std::size_t i=0;i<regs.size();++i)mem(regs[i],128+static_cast<std::uint32_t>(i)*8,false);

        // lea rax, [rsp+264]; mov [rsp+248], rax  (original RSP)
        emit({
            0x48,0x8d,0x84,0x24
        });
        u32(b,gateway_stack);
        mem(0,248,false);

        for(unsigned i=0;i<6;++i){
            emit({
                0xf3,0x0f,0x7f,static_cast<std::uint8_t>(0x84|(i<<3)),0x24
            });
            u32(b,32+i*16);
        }

        emit({
            0x48,0xb9
        });
        u64(b,userdata);
        // RCX = Hook*
        emit({
            0x48,0x8d,0x94,0x24
        });
        u32(b,128);
        // RDX = Context*
        emit({
            0x48,0xb8
        });
        u64(b,callback);
        emit({
            0xff,0xd0
        });

        for(unsigned i=0;i<6;++i){
            emit({
                0xf3,0x0f,0x6f,static_cast<std::uint8_t>(0x84|(i<<3)),0x24
            });
            u32(b,32+i*16);
        }

        for(std::size_t i=0;i<regs.size();++i)mem(regs[i],128+static_cast<std::uint32_t>(i)*8,true);

        emit({
            0x48,0x81,0xc4
        });
        u32(b,gateway_stack);

        // A memory-indirect tail jump preserves every general-purpose register.
        emit({
            0xff,0x25,0,0,0,0
        });
        u64(b,trampoline);

        while(b.size()%4)u8(b,0x90);

        g.unwind_offset=static_cast<std::uint32_t>(b.size());

        // UNWIND_INFO version 1; 7-byte prolog; 2 slots; UWOP_ALLOC_LARGE (264/8).
        emit({
            1,7,2,0,7,1,33,0
        });

        return g;
    }
}
// namespace th
