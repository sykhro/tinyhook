// Executes actual i386 gateway bytes in Linux x86-64 compatibility mode.
// No 32-bit libc or Wine is involved. This is a CPU-state test, not injection proof.
#include "th/gateway32.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace th;
struct Image {
    std::uint8_t* mem{};
    static constexpr std::size_t size=0x20000;
    Image() {
        void* p=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
        if(p==MAP_FAILED)throw Error("low address mmap unavailable");
        mem=static_cast<std::uint8_t*>(p);
    }
    ~Image(){if(mem)munmap(mem,size);}
    std::uint32_t at(std::size_t off)const{return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(mem+off));}
    void copy(std::size_t off,const Bytes& b){if(off+b.size()>size)throw Error("image overflow");std::memcpy(mem+off,b.data(),b.size());}
    std::uint32_t word(std::size_t off)const{std::uint32_t n;std::memcpy(&n,mem+off,4);return n;}
};
struct Code {
    Bytes b;
    std::uint32_t address;
    explicit Code(std::uint32_t addr):address(addr){}
    void op(std::initializer_list<std::uint8_t> bytes){b.insert(b.end(),bytes);}
    void n(std::uint32_t value){u32(b,value);}
    void mov(unsigned reg,std::uint32_t value){u8(b,static_cast<std::uint8_t>(0xb8+reg));n(value);}
    void branch(std::uint8_t op,std::uint32_t target){u8(b,op);n(target-(address+static_cast<std::uint32_t>(b.size())+4));}
    void far64(std::uint32_t target){op({0xea});n(target);op({0x33,0});}
};
void require(bool b,const char* what){if(!b)throw Error(what);}
void run(unsigned alignment,unsigned pop_bytes,bool mid) {
    Image image;
    constexpr std::size_t wrapper=0x40, gateway=0x100, callback=0x400, trampoline=0x800,
                          fixture=0x1000, resume64=0x1800, data=0x3000;
    const auto a=[&](std::size_t x){return image.at(x);};
    const auto saved_rsp=a(data), final_esp=a(data+0x10), user=a(data+0x20), flags=a(data+0x24),
               context_ptr=a(data+0x28), callback_sp=a(data+0x2c), observed=a(data+0x40),
               resumed=a(data+0x80), before_fx=a(data+0x200), after_fx=a(data+0x400),
               arguments=a(data+0x600), mxcsr=a(data+0x680), stack=a(0x1f000);

    Code entry(a(wrapper));
    entry.op({0x53,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}); // preserve SysV nonvolatiles
    entry.op({0x48,0xb8});u64(entry.b,saved_rsp);entry.op({0x48,0x89,0x20});
    entry.op({0x8c,0xd9,0x66,0x89,0x48,0x08,0x8c,0xc1,0x66,0x89,0x48,0x0a}); // preserve DS/ES
    entry.mov(4,stack);entry.op({0x6a,0x23,0x48,0xb8});u64(entry.b,a(fixture));
    entry.op({0x50,0x48,0xcb}); // retfq -> Linux user compatibility CS
    image.copy(wrapper,entry.b);

    Code back(a(resume64));
    back.op({0xfc,0x48,0xb8});u64(back.b,saved_rsp);back.op({0x48,0x8b,0x20});
    back.op({0x66,0x8b,0x48,0x08,0x8e,0xd9,0x66,0x8b,0x48,0x0a,0x8e,0xc1}); // restore DS/ES
    back.op({0x41,0x5f,0x41,0x5e,0x41,0x5d,0x41,0x5c,0x5d,0x5b,0xc3});
    image.copy(resume64,back.b);

    Code cb(a(callback));
    cb.op({0x55,0x53,0x56,0x57}); // cdecl nonvolatile registers
    cb.op({0x8b,0x44,0x24,0x14,0xa3});cb.n(user);
    cb.op({0x9c,0x58,0xa3});cb.n(flags);
    cb.op({0x8d,0x44,0x24,0x10,0xa3});cb.n(callback_sp);
    cb.op({0x8b,0x74,0x24,0x18,0x89,0x35});cb.n(context_ptr);
    cb.mov(7,observed);cb.mov(1,9);cb.op({0xf3,0xa5}); // copy Context32, relying on CLD
    cb.op({0x8b,0x44,0x24,0x18,0x8b,0x70,0x0c,0x83,0xc6,0x04});
    cb.mov(7,arguments);cb.mov(1,4);cb.op({0xf3,0xa5});
    cb.op({0xdb,0xe3,0xd9,0xee}); // fninit; fldz: destroy x87 state
    for(unsigned i=0;i<8;++i)cb.op({0x66,0x0f,0xef,static_cast<std::uint8_t>(0xc0+9*i)});
    cb.op({0xc7,0x05});cb.n(mxcsr);cb.n(0x3f80);
    cb.op({0x0f,0xae,0x15});cb.n(mxcsr);
    cb.op({0x5f,0x5e,0x5b,0x5d});
    cb.mov(0,0xaaaaaaaa);cb.mov(1,0xbbbbbbbb);cb.mov(2,0xcccccccc);
    cb.op({0xf8,0xc3});
    image.copy(callback,cb.b);
    image.copy(gateway,make_gateway32(a(gateway),0x12345678,a(callback),a(trampoline)));

    Code target(a(trampoline));
    target.op({0x9c,0x60,0x8b,0xec,0xfc,0x8b,0xf5});
    target.mov(7,resumed);target.mov(1,9);target.op({0xf3,0xa5});
    target.op({0x0f,0xae,0x05});target.n(after_fx);
    target.op({0x8b,0xe5,0x61,0x9d});
    if(mid) {
        target.op({0x89,0x25});target.n(final_esp);target.far64(a(resume64));
    } else if(pop_bytes) {target.op({0xc2});u16(target.b,static_cast<std::uint16_t>(pop_bytes));}
    else target.op({0xc3});
    image.copy(trampoline,target.b);

    Code game(a(fixture));
    game.op({0x66,0xb8,0x2b,0x00,0x8e,0xd8,0x8e,0xc0}); // Linux user DS/ES for 32-bit memory instructions
    game.op({0x83,0xec,static_cast<std::uint8_t>(alignment)});
    for(auto arg:{0xcccc1111u,0xbbbb2222u,0xaaaa3333u}){game.op({0x68});game.n(arg);}
    game.op({0xdb,0xe3,0xd9,0xe8,0xd9,0xeb}); // fninit; fld1; fldpi
    for(unsigned i=0;i<8;++i)game.op({0x66,0x0f,0x76,static_cast<std::uint8_t>(0xc0+9*i)});
    game.op({0x0f,0xae,0x05});game.n(before_fx);
    for(unsigned r=0;r<8;++r)if(r!=4)game.mov(r,(r+1)*0x11111111u);
    game.op({0x68});game.n(0xcd7);game.op({0x9d}); // CF/PF/AF/ZF/SF/DF/OF live at hook
    game.branch(mid?0xe9:0xe8,a(gateway));
    game.op({0x89,0x25});game.n(final_esp);game.far64(a(resume64));
    image.copy(fixture,game.b);
    if(mprotect(image.mem,0x2000,PROT_READ|PROT_EXEC))throw Error("mprotect failed");
    auto fn=reinterpret_cast<void(*)()>(image.mem+wrapper);
    fn();

    const auto sp=stack-alignment-12-(mid?0:4);
    const std::uint32_t expected[]={0x88888888,0x77777777,0x66666666,sp-4,0x44444444,0x33333333,0x22222222,0x11111111};
    for(unsigned i=0;i<8;++i){
        require(image.word(data+0x40+i*4)==expected[i],"observer GPR/ESP mismatch");
        require(image.word(data+0x80+i*4)==expected[i],"restored GPR/ESP mismatch");
    }
    require(image.word(data+0x20)==0x12345678,"observer userdata mismatch");
    require(image.word(data+0x28)==sp-36,"Context32 address mismatch");
    require((image.word(data+0x24)&0x400)==0,"callback direction flag was not cleared");
    require((image.word(data+0x2c)&15)==12,"callback stack is not ABI-aligned");
    require((image.word(data+0x60)&0xcd5)==0xcd5,"observed flags mismatch");
    require((image.word(data+0xa0)&0xcd5)==0xcd5,"restored flags mismatch");
    require(image.word(data+0x10)==stack-alignment-12+(mid?0:pop_bytes),"stack cleanup mismatch");
    const auto first_arg=data+0x600+(mid?0:4);
    require(image.word(first_arg)==0xaaaa3333&&image.word(first_arg+4)==0xbbbb2222&&image.word(first_arg+8)==0xcccc1111,"stack argument mismatch");
    require(!std::memcmp(image.mem+data+0x200,image.mem+data+0x400,288),"x87/XMM/MXCSR state not preserved");
}
}
int main() {
    try {
        for(unsigned repeat=0;repeat<100;++repeat)for(unsigned alignment:{0u,4u,8u,12u}) {
            run(alignment,0,false);  // cdecl
            run(alignment,12,false); // stdcall callee cleanup
            run(alignment,4,false);  // fastcall ECX/EDX plus stack argument
            run(alignment,0,true);   // arbitrary mid-function ESP, no call return address
        }
        std::cout<<"PASS: 1,600 actual i386 gateway executions in Linux compatibility mode; "
                    "cdecl/stdcall/fastcall/mid-point, GPR/ESP/EFLAGS, x87, XMM0-7, MXCSR and stack alignment\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
