#include "th/gateway.hpp"
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
using namespace th;

#if defined(__x86_64__)
#define WINABI __attribute__((ms_abi))
static bool observed=false;

static WINABI void capture(void* user,const Context* ctx){
    auto a=ctx->reg[static_cast<unsigned>(Register::rcx)];
    auto b=ctx->reg[static_cast<unsigned>(Register::rdx)];

    auto c=ctx->reg[static_cast<unsigned>(Register::r8)];
    auto d=ctx->reg[static_cast<unsigned>(Register::r9)];

    const auto* stack=reinterpret_cast<const std::uint64_t*>(ctx->reg[15]);

    observed=user==reinterpret_cast<void*>(0x1234)&&a==11&&b==22&&c==33&&d==44&&stack[5]==55&&stack[8]==88;
}

static WINABI std::uint64_t target(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d,std::uint64_t e,std::uint64_t f,std::uint64_t g,std::uint64_t h){
    return a+b+c+d+e+f+g+h;
}

static WINABI void fp_capture(void*,const Context*){
    // Deliberately destroy all volatile Win64 SIMD registers. An empty callback
    // would not prove that the gateway actually restores floating-point inputs.
    asm volatile(
        "pxor %%xmm0, %%xmm0\n\t"
        "pxor %%xmm1, %%xmm1\n\t"
        "pxor %%xmm2, %%xmm2\n\t"
        "pxor %%xmm3, %%xmm3\n\t"
        "pxor %%xmm4, %%xmm4\n\t"
        "pxor %%xmm5, %%xmm5"
        : : : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5"
    );
}
static WINABI double fp_target(double a,double b,double c,double d,double e){
    return a+b*2+c*3+d*4+e*5;
}

static void* gateway(std::uint64_t data,std::uint64_t cb,std::uint64_t fn){
    auto g=make_gateway(data,cb,fn);
    void* p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);

    if(p==MAP_FAILED)throw Error("mmap failed");
    // Clang function-type sanitization may inspect the 8 bytes before a call
    // destination. Leave readable padding before generated (unannotated) code.
    std::memcpy(static_cast<unsigned char*>(p)+16,g.code.data(),g.code.size());
    if(mprotect(p,4096,PROT_READ|PROT_EXEC))throw Error("mprotect failed");
    return static_cast<unsigned char*>(p)+16;
}

#endif
int main(){
    try{
#if defined(__x86_64__)
        auto* p=gateway(0x1234,reinterpret_cast<std::uint64_t>(&capture),reinterpret_cast<std::uint64_t>(&target));

        auto fn=reinterpret_cast<decltype(&target)>(p);

        for(int i=0;i<10000;++i)if(fn(11,22,33,44,55,66,77,88)!=396||!observed)throw Error("Win64 register/stack preservation failed");

        munmap(static_cast<unsigned char*>(p)-16,4096);
        p=gateway(0,reinterpret_cast<std::uint64_t>(&fp_capture),reinterpret_cast<std::uint64_t>(&fp_target));

        auto fp=reinterpret_cast<decltype(&fp_target)>(p);
        if(fp(1,2,3,4,5)!=55)throw Error("Win64 XMM preservation failed");
        munmap(static_cast<unsigned char*>(p)-16,4096);

        std::cout<<"10,000 Win64 ABI calls + XMM/stack argument preservation passed (Linux executable-memory harness)\n";

#endif
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
