#include "th/gateway32.hpp"
#include "th/text32.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

#define CHECK(x) do { if (!(x)) throw th::Error("check failed: " #x); } while (false)

template<class F> void rejects(F f) {
    bool caught=false;
    try { f(); } catch(const th::Error&) { caught=true; }
    CHECK(caught);
}

int main() {
    try {
        using namespace th;
        constexpr std::uint32_t object=0xf0000000u, heap=0xf0100000u;
        std::array<std::uint8_t,24> header{};
        std::array<std::uint8_t,64> contents{}, output{};
        auto word=[&](unsigned offset,std::uint32_t n) {
            for(unsigned i=0;i<4;++i) header[offset+i]=static_cast<std::uint8_t>(n>>(8*i));
        };
        unsigned reads=0;
        bool race=false, missing_heap=false;
        auto read=[&](std::uint32_t address,void* destination,std::size_t n) noexcept {
            ++reads;
            if(address==object&&n==header.size()) {
                std::memcpy(destination,header.data(),n);
                if(race && reads>1) static_cast<std::uint8_t*>(destination)[16]^=1;
                return true;
            }
            if(address==heap&&n<=contents.size()&&!missing_heap) {
                std::memcpy(destination,contents.data(),n);return true;
            }
            return false;
        };
        std::uint32_t length=0;
        for(unsigned n=1;n<=7;++n) {
            header.fill(0); word(16,n); word(20,7);
            for(unsigned i=0;i<n;++i) {header[i*2]=0xe5;header[i*2+1]=0x65;}
            CHECK(read_msvc_wstring32(object,output,length,read));
            CHECK(length==n*2);
            CHECK(std::equal(output.begin(),output.begin()+length,header.begin()));
        }
        header.fill(0);word(0,heap);word(16,12);word(20,31);
        contents.fill(0x30); // No terminator needed: explicit character length.
        CHECK(read_msvc_wstring32(object,output,length,read));
        CHECK(length==24 && output[0]==0x30);
        CHECK(!read_msvc_wstring32(object,std::span(output).first(23),length,read));
        CHECK(length==0);
        missing_heap=true;
        CHECK(!read_msvc_wstring32(object,output,length,read));missing_heap=false;
        race=true;reads=0;
        CHECK(!read_msvc_wstring32(object,output,length,read));race=false;
        CHECK(!read_msvc_wstring32(0,output,length,read));
        CHECK(!read_msvc_wstring32(UINT32_MAX-8,output,length,read));
        word(0,UINT32_MAX-8);
        CHECK(!read_msvc_wstring32(object,output,length,read));
        word(0,heap);word(16,32);
        CHECK(!read_msvc_wstring32(object,output,length,read)); // size > capacity
        word(16,0);CHECK(!read_msvc_wstring32(object,output,length,read));
        word(16,12);word(20,UINT32_MAX);
        CHECK(!read_msvc_wstring32(object,output,length,read));
        word(16,8);word(20,7);
        CHECK(!read_msvc_wstring32(object,output,length,read));

        Spec s;
        s.name="siglus";s.arch=Architecture::x86;s.source=Register::eax;
        s.stack_offset=4;s.point=HookPoint::mid;s.layout=TextLayout::msvc_wstring32;
        s.location=Location::pattern;s.pattern="C7 47 14 07 00 00 00 C7 47 10 00 00 00 00 66 89 0F 8B CF 50 E8 ?? ?? ?? ??";
        s.hook_offset=25;s.expected="8B 1D ?? ?? ?? ??";
        const auto wire=encode_specs({s});
        const auto result=decode_specs(wire).front();
        CHECK(result.arch==s.arch&&result.source==s.source&&result.layout==s.layout);
        CHECK(result.point==s.point&&result.hook_offset==25&&result.expected==s.expected);
        s.source=Register::rcx;rejects([&]{validate(s);});s.source=Register::eax;
        s.arch=Architecture::x64;rejects([&]{validate(s);});s.arch=Architecture::x86;
        s.expected.clear();validate(s); // unique long pattern may guard an x86 mid-function hook by itself
        auto no_expected=decode_specs(encode_specs({s})).front();
        CHECK(no_expected.expected.empty()&&no_expected.point==HookPoint::mid);
        auto long_pattern=s.pattern;
        s.pattern="C7 47 14 07 00 00 00";rejects([&]{validate(s);});s.pattern=long_pattern;
        s.location=Location::rva;s.rva=0x1000;rejects([&]{validate(s);});s.location=Location::pattern;s.rva=0;
        s.expected="8B 1D ?? ?? ?? ??";
        s.stack_offset=3;rejects([&]{validate(s);});s.stack_offset=4;
        s.encoding=Encoding::utf8;rejects([&]{validate(s);});s.encoding=Encoding::utf16;
        auto wrong_version=wire;wrong_version[0]=1;
        rejects([&]{decode_specs(wrong_version);});
        Context32 c{};c.saved_esp=0xffffff00;c.eax=123;c.ecx=456;
        CHECK(c.stack()==0xffffff04&&c.get(Register::esp)==0xffffff04);
        CHECK(c.get(Register::eax)==123&&c.get(Register::ecx)==456);
        auto gateway=make_gateway32(0xf0000000,1,2,0x10000000);
        CHECK(gateway[0]==0x9c&&gateway[1]==0x60&&gateway[gateway.size()-5]==0xe9);
        Reader r(std::span<const std::uint8_t>(gateway).last(4));
        CHECK(std::uint32_t(0xf0000000u+gateway.size()+r.get32())==0x10000000u);
        std::cout<<"PASS: x86 protocol/architecture checks, high 32-bit addresses, "
                     "inline/heap UTF-16, bounds/races, gateway relative-jump wrap\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
