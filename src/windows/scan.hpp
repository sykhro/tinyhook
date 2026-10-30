#pragma once
#include "win.hpp"

namespace th::win {
    inline constexpr Architecture build_arch=sizeof(void*)==4?Architecture::x86:Architecture::x64;
    inline constexpr const char* build_arch_name=sizeof(void*)==4?"x86":"x64";
    inline constexpr WORD pe_machine=sizeof(void*)==4?IMAGE_FILE_MACHINE_I386:IMAGE_FILE_MACHINE_AMD64;
    inline constexpr WORD pe_magic=sizeof(void*)==4?IMAGE_NT_OPTIONAL_HDR32_MAGIC:IMAGE_NT_OPTIONAL_HDR64_MAGIC;

    inline bool read_memory(HANDLE process,std::uintptr_t address,void* data,std::size_t size)noexcept {
        if(!address || (size && address>UINTPTR_MAX-(size-1)))return false;
        SIZE_T n=0;
        return ReadProcessMemory(process,reinterpret_cast<void*>(address),data,size,&n)&&n==size;
    }
    inline IMAGE_NT_HEADERS pe_header(HANDLE process,std::uintptr_t base) {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS nt{};
        if(!read_memory(process,base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE ||
           dos.e_lfanew<0||dos.e_lfanew>1024*1024 || base>UINTPTR_MAX-static_cast<std::uint32_t>(dos.e_lfanew) ||
           !read_memory(process,base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE ||
           nt.FileHeader.Machine!=pe_machine || nt.OptionalHeader.Magic!=pe_magic ||
           nt.OptionalHeader.SizeOfImage<4096 || base>UINTPTR_MAX-nt.OptionalHeader.SizeOfImage)
            throw Error(std::string("expected a valid ")+build_arch_name+" PE module");
        return nt;
    }
    inline std::uintptr_t find_pattern(HANDLE process,std::uintptr_t base,const IMAGE_NT_HEADERS& nt,std::string_view pattern) {
        const auto bytes=parse_pattern(pattern);
        std::uintptr_t found=0;

        IMAGE_DOS_HEADER dos{};
        if(!read_memory(process,base,&dos,sizeof(dos)))throw Error("cannot read PE header");

        const auto sections=base+static_cast<std::uintptr_t>(dos.e_lfanew)+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;

        if(nt.FileHeader.NumberOfSections>96)throw Error("too many PE sections");

        for(unsigned section=0;section<nt.FileHeader.NumberOfSections;++section) {
            IMAGE_SECTION_HEADER sh{};
            if(!read_memory(process,sections+section*sizeof(sh),&sh,sizeof(sh)))throw Error("cannot read PE section");

            if(!(sh.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;

            if(sh.VirtualAddress>=nt.OptionalHeader.SizeOfImage||sh.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-sh.VirtualAddress)throw Error("invalid PE section range");

            auto begin=base+sh.VirtualAddress,end=begin+sh.Misc.VirtualSize,pos=begin;
            Bytes tail;

            while(pos<end) {
                MEMORY_BASIC_INFORMATION m{};
                if(!VirtualQueryEx(process,reinterpret_cast<void*>(pos),&m,sizeof(m)))break;

                auto region_end=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
                if(region_end<=pos)break;
                region_end=std::min(region_end,end);

                if(m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))){
                    pos=region_end;
                    tail.clear();
                    continue;
                }

                while(pos<region_end) {
                    auto n=std::min<std::size_t>(65536,region_end-pos);
                    Bytes block=tail;
                    block.resize(tail.size()+n);

                    if(!read_memory(process,pos,block.data()+tail.size(),n)){
                        tail.clear();
                        pos+=n;
                        continue;
                    }

                    auto block_base=pos-tail.size();

                    for(std::size_t i=0;i+bytes.size()<=block.size();++i)if(pattern_at(std::span<const std::uint8_t>(block).subspan(i),bytes)){
                        auto match=block_base+i;
                        if(found&&found!=match)throw Error("pattern is ambiguous (more than one match)");
                        found=match;
                    }

                    auto keep=std::min(bytes.size()-1,block.size());
                    tail.assign(block.end()-static_cast<std::ptrdiff_t>(keep),block.end());
                    pos+=n;
                }
            }
        }

        if(!found)throw Error("pattern not found in executable PE sections");
        return found;
    }


    inline std::uintptr_t remote_export(HANDLE process,std::uintptr_t base,const IMAGE_NT_HEADERS& nt,std::string_view symbol) {
        if(nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT)throw Error("no export directory");
        auto dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        auto at=[&](DWORD rva,void* out,std::size_t size) {
            if(rva>=nt.OptionalHeader.SizeOfImage || size>nt.OptionalHeader.SizeOfImage-rva ||
               !read_memory(process,base+rva,out,size))throw Error("invalid export directory");
        };
        if(!dir.VirtualAddress || dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY) ||
           dir.VirtualAddress>=nt.OptionalHeader.SizeOfImage || dir.Size>nt.OptionalHeader.SizeOfImage-dir.VirtualAddress)
            throw Error("no valid export directory");
        IMAGE_EXPORT_DIRECTORY ed{};at(dir.VirtualAddress,&ed,sizeof(ed));
        if(ed.NumberOfNames>65536 || ed.NumberOfFunctions>65536)throw Error("export table too large");
        for(DWORD i=0;i<ed.NumberOfNames;++i) {
            DWORD name_rva{};
            if(ed.AddressOfNames>UINT32_MAX-i*4)throw Error("invalid export name table");
            at(ed.AddressOfNames+i*4,&name_rva,4);
            std::string name;
            for(unsigned j=0;j<=255;++j) {
                char c{};
                if(name_rva>UINT32_MAX-j)throw Error("invalid export name");
                at(name_rva+j,&c,1);
                if(!c)break;
                name+=c;
            }
            if(name!=symbol)continue;
            WORD ordinal{};DWORD address{};
            if(ed.AddressOfNameOrdinals>UINT32_MAX-i*2)throw Error("invalid export ordinal");
            at(ed.AddressOfNameOrdinals+i*2,&ordinal,2);
            if(ordinal>=ed.NumberOfFunctions || ed.AddressOfFunctions>UINT32_MAX-ordinal*4u)throw Error("invalid export ordinal");
            at(ed.AddressOfFunctions+ordinal*4u,&address,4);
            if(!address || address>=nt.OptionalHeader.SizeOfImage)throw Error("invalid export address");
            if(address>=dir.VirtualAddress && address-dir.VirtualAddress<dir.Size)throw Error("forwarded hook exports are not supported");
            return base+address;
        }
        throw Error("export not found: "+std::string(symbol));
    }

    inline std::uintptr_t resolve_in(HANDLE process,std::uintptr_t base,const Spec& spec) {
        validate(spec);
        if(spec.arch!=build_arch)throw Error("script architecture differs from client/DLL architecture");
        auto nt=pe_header(process,base);
        std::uintptr_t address{};
        if(spec.location==Location::symbol)address=remote_export(process,base,nt,spec.symbol);
        else if(spec.location==Location::rva) {
            if(spec.rva>=nt.OptionalHeader.SizeOfImage)throw Error("RVA outside module");
            address=base+spec.rva;
        } else address=find_pattern(process,base,nt,spec.pattern);
        address=plus(address,spec.hook_offset);
        if(!address || address<base || address-base>=nt.OptionalHeader.SizeOfImage)throw Error("hook offset outside module");
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQueryEx(process,reinterpret_cast<void*>(address),&info,sizeof(info)) ||
           info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
            throw Error("hook target is not executable");
        if(!spec.expected.empty()) {
            auto expected=parse_pattern(spec.expected);
            Bytes bytes(expected.size());
            if(bytes.size()>nt.OptionalHeader.SizeOfImage-(address-base) ||
               !read_memory(process,address,bytes.data(),bytes.size()) || !pattern_at(bytes,expected))
                throw Error("expected patch-site instructions do not match; refusing to hook");
        }
        return address;
    }
}
