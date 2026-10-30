#include "win.hpp"
#include "log.hpp"
#include <spdlog/spdlog.h>
#include "th/gateway.hpp"
#include "th/gateway32.hpp"
#include "th/text32.hpp"
#include "scan.hpp"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <new>
#include <process.h>

namespace {
    using namespace th;

    using namespace th::win;

#ifdef _WIN64
    using NativeContext=Context;
#else
    using NativeContext=Context32;
#endif

    struct Raw {
        std::uint32_t hook{},tid{},length{};

        alignas(2) std::array<std::uint8_t,max_capture> bytes{};
    };

    struct Runtime;

    struct Hook {
        Runtime* owner{};
        std::uint32_t id{};
        Spec spec;
        void* target{};
        void* gateway{};
        void* original{};

#ifdef _WIN64
        RUNTIME_FUNCTION unwind{};
#endif
        bool created=false,registered=false;
    };

    struct Runtime {
        Wsa wsa;

        std::uint16_t port{};
        std::uint64_t profile_tag{};
        std::string token;
        std::vector<Spec> specs;

        std::array<Hook,max_hooks> hooks;

        SRWLOCK lock=SRWLOCK_INIT;
        std::array<Raw,64> queue;
        unsigned head{},tail{},count{};

        std::atomic<std::uint64_t> dropped{
            0
        };
        std::atomic<bool> active{
            false
        };

        HANDLE wake{},worker{};
        DWORD tls=TLS_OUT_OF_INDEXES;

        bool pop(Raw& r)noexcept {
            AcquireSRWLockExclusive(&lock);
            bool ok=count!=0;

            if(ok){
                r=queue[head];
                head=(head+1)%queue.size();
                --count;
            }

            ReleaseSRWLockExclusive(&lock);
            return ok;
        }

        void push(const Raw& r)noexcept {
            // The sender briefly holds this lock while copying a queue item.
            // Contention is normal, especially for consecutive name/dialogue
            // captures; it must not discard an otherwise valid capture. Neither
            // critical section performs I/O, decoding, or calls into the game.
            AcquireSRWLockExclusive(&lock);

            if(count==queue.size())dropped.fetch_add(1,std::memory_order_relaxed);

            else {
                queue[tail]=r;
                tail=(tail+1)%queue.size();
                ++count;
            }

            ReleaseSRWLockExclusive(&lock);
            SetEvent(wake);
        }
    };

    Runtime* runtime=nullptr;

    SRWLOCK lifecycle=SRWLOCK_INIT;

    // Read at most the configured bytes, in bounded chunks that never cross a page.
    // ReadProcessMemory converts bad/racing addresses into failure instead of a game AV.
    bool copy_string(std::uintptr_t p,Encoding encoding,std::uint32_t limit,Raw& r)noexcept {
        const unsigned unit=encoding==Encoding::utf16?2:1;

        if(!p||(unit==2&&(p&1)))return false;

        for(unsigned offset=0;offset<limit;) {
            auto address=p+offset;
            if(address<p)return false;

            auto n=std::min<unsigned>({
                256,limit-offset,4096-static_cast<unsigned>(address&4095)
            });
            n-=n%unit;

            if(!n||!read_at(address,r.bytes.data()+offset,n))return false;

            for(unsigned i=0;i<n;i+=unit)if(r.bytes[offset+i]==0&&(unit==1||r.bytes[offset+i+1]==0)){
                r.length=offset+i;
                return r.length!=0;
            }

            offset+=n;
        }

        return false;
        // No terminator: drop rather than split UTF-8/UTF-16 or read unbounded memory.
    }

    void __cdecl capture(Hook* hook,const NativeContext* ctx)noexcept {
        DWORD saved_error=GetLastError();
        auto& rt=*hook->owner;

        if(!rt.active.load(std::memory_order_relaxed)||TlsGetValue(rt.tls)){
            SetLastError(saved_error);
            return;
        }

        TlsSetValue(rt.tls,reinterpret_cast<void*>(1));

        const auto& s=hook->spec;
        std::uintptr_t p=0;

        if(s.source==Register::stack) {
            #ifdef _WIN64
            auto sp=static_cast<std::uintptr_t>(ctx->reg[15]);
#else
            auto sp=ctx->stack();
#endif
            auto address=plus(sp,static_cast<std::int32_t>(s.stack_offset));
            if(address)read_at(address,&p,sizeof(p));
        }

#ifdef _WIN64
        else p=static_cast<std::uintptr_t>(ctx->reg[static_cast<unsigned>(s.source)]);
#else
        else p=ctx->get(s.source);
#endif

        for(auto offset:s.deref){
            auto address=plus(p,offset);
            if(!p||!address||!read_at(address,&p,sizeof(p))){
                p=0;
                break;
            }
        }

        if(p)p=plus(p,s.add);

        Raw raw;
        raw.hook=hook->id;
        raw.tid=GetCurrentThreadId();

        bool ok=false;
        if(s.layout==TextLayout::msvc_wstring32 && p<=UINT32_MAX) {
            ok=read_msvc_wstring32(static_cast<std::uint32_t>(p),
                std::span<std::uint8_t>(raw.bytes).first(s.limit),raw.length,
                [](std::uint32_t addr,void* dst,std::size_t bytes)noexcept{return read_at(addr,dst,bytes);});
        } else if(s.layout==TextLayout::cstring)ok=copy_string(p,s.encoding,s.limit,raw);
        if(ok)rt.push(raw);

        TlsSetValue(rt.tls,nullptr);
        SetLastError(saved_error);
    }

    std::uintptr_t resolve(const Spec& s) {
        HMODULE module=nullptr;
        if(s.module.empty())module=GetModuleHandleW(nullptr);
        else {
            auto name=wide(s.module);
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,name.c_str(),&module))
                throw Error("target module is not loaded: "+s.module);
        }
        if(!module)throw Error("target module unavailable");
        return resolve_in(GetCurrentProcess(),reinterpret_cast<std::uintptr_t>(module),s);
    }

    void mh(MH_STATUS s,const char* action){
        if(s!=MH_OK)throw Error(std::string(action)+": "+MH_StatusToString(s));
    }

    std::string decode(const Raw& r,Encoding encoding) {
        if(encoding==Encoding::utf8){
            std::string s(reinterpret_cast<const char*>(r.bytes.data()),r.length);
            if(!valid_utf8(s))throw Error("invalid UTF-8 capture");
            return s;
        }

        if(encoding==Encoding::utf16)return utf8({
            reinterpret_cast<const wchar_t*>(r.bytes.data()),r.length/2
        });

        int n=MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(r.bytes.data()),static_cast<int>(r.length),nullptr,0);

        if(n<=0)throw Error("invalid CP932 capture");
        std::wstring text(static_cast<std::size_t>(n),L'\0');

        if(MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(r.bytes.data()),static_cast<int>(r.length),text.data(),n)!=n)throw Error("CP932 conversion failed");
        return utf8(text);
    }

    unsigned __stdcall sender(void* arg)noexcept {
        auto& rt=*static_cast<Runtime*>(arg);

        while(rt.active.load()) {
            try {
                spdlog::debug("connecting capture channel to 127.0.0.1:{}",rt.port);
                auto socket=connect_local(rt.port);
                send_all(socket.s,hello(2,rt.token,rt.profile_tag));
                std::array<std::uint8_t,4> ack{};
                recv_all(socket.s,ack);

                if(ack!=std::array<std::uint8_t,4>{
                    'O','K','A','Y'
                })throw Error("host did not authenticate capture connection");

                spdlog::info("capture channel authenticated; {} hook(s) enabled",rt.specs.size());
                send_all(socket.s,encode_event(Event{
                    2,0,GetCurrentProcessId(),0,"connected; "+std::to_string(rt.specs.size())+" hook(s) enabled"
                }));

                while(rt.active.load()) {
                    Raw raw;

                    while(rt.active.load()&&rt.pop(raw)) {
                        std::string text;

                        try{
                            text=decode(raw,rt.specs[raw.hook].encoding);
                        }
                        catch(...){
                            rt.dropped.fetch_add(1);
                            continue;
                        }

                        send_all(socket.s,encode_event(Event{
                            1,raw.hook,GetCurrentProcessId(),raw.tid,std::move(text)
                        }));
                    }

                    auto dropped=rt.dropped.exchange(0);
                    if(dropped)send_all(socket.s,encode_event(Event{
                        3,0,GetCurrentProcessId(),0,"dropped "+std::to_string(dropped)+" capture(s): full queue, invalid encoding, or transport failure"
                    }));

                    WaitForSingleObject(rt.wake,100);
                }
            }
            catch(const std::exception& e){
                spdlog::warn("capture transport: {}",e.what());
                rt.dropped.fetch_add(1);
                if(rt.active.load())WaitForSingleObject(rt.wake,1000);
            }
            catch(...){
                spdlog::warn("capture transport: unknown failure");
                rt.dropped.fetch_add(1);
                if(rt.active.load())WaitForSingleObject(rt.wake,1000);
            }
        }

        return 0;
    }

    void setup(Runtime& rt) {
        rt.tls=TlsAlloc();
        if(rt.tls==TLS_OUT_OF_INDEXES)throw Error("TlsAlloc failed");

        rt.wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!rt.wake)throw Error("CreateEvent failed");

#ifndef _WIN64
        if(!IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE))throw Error("x86 gateway requires FXSAVE/SSE support");
#endif
        mh(MH_Initialize(),"MH_Initialize");

        for(std::size_t i=0;i<rt.specs.size();++i) {
            auto& h=rt.hooks[i];
            h.owner=&rt;
            h.id=static_cast<std::uint32_t>(i);
            h.spec=rt.specs[i];
            h.target=reinterpret_cast<void*>(resolve(h.spec));
            spdlog::info("resolved hook '{}' at {}",h.spec.name,h.target);

            h.gateway=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!h.gateway)throw Error("gateway allocation failed");

            DWORD old{};
            if(!VirtualProtect(h.gateway,4096,PAGE_EXECUTE_READ,&old))throw Error("gateway protection failed");

            mh(MH_CreateHook(h.target,h.gateway,&h.original),"MH_CreateHook");
            h.created=true;

            #ifdef _WIN64
            auto gateway=make_gateway(reinterpret_cast<std::uint64_t>(&h),reinterpret_cast<std::uint64_t>(&capture),reinterpret_cast<std::uint64_t>(h.original));

            const auto& code=gateway.code;
#else
            auto code=make_gateway32(reinterpret_cast<std::uintptr_t>(h.gateway),
                reinterpret_cast<std::uintptr_t>(&h),reinterpret_cast<std::uintptr_t>(&capture),
                reinterpret_cast<std::uintptr_t>(h.original));
#endif
            if(code.size()>4096)throw Error("gateway exceeds allocated page");
            if(!VirtualProtect(h.gateway,4096,PAGE_READWRITE,&old))throw Error("gateway protection failed");
            std::memcpy(h.gateway,code.data(),code.size());

            if(!VirtualProtect(h.gateway,4096,PAGE_EXECUTE_READ,&old))throw Error("gateway protection failed");
            FlushInstructionCache(GetCurrentProcess(),h.gateway,code.size());
#ifdef _WIN64

            h.unwind.BeginAddress=0;
            h.unwind.EndAddress=gateway.unwind_offset;
            h.unwind.UnwindData=gateway.unwind_offset;

            if(!RtlAddFunctionTable(&h.unwind,1,reinterpret_cast<DWORD64>(h.gateway)))throw Error("RtlAddFunctionTable failed");
            h.registered=true;
#endif

            mh(MH_QueueEnableHook(h.target),"MH_QueueEnableHook");
        }

        rt.active.store(true);

        // Enable as a batch; preserve all allocations on any post-enable failure.
        mh(MH_ApplyQueued(),"MH_ApplyQueued");

        rt.worker=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,sender,&rt,0,nullptr));
        if(!rt.worker)throw Error("_beginthreadex failed");
    }

    DWORD stop_runtime()noexcept {
        if(!runtime)return ERROR_NOT_FOUND;
        auto& rt=*runtime;
        rt.active.store(false);
        SetEvent(rt.wake);
        DWORD error=ERROR_SUCCESS;

        for(auto& h:rt.hooks)if(h.created){
            auto status=MH_DisableHook(h.target);
            if(status!=MH_OK&&status!=MH_ERROR_DISABLED)error=ERROR_GEN_FAILURE;
        }

        if(rt.worker&&WaitForSingleObject(rt.worker,5000)!=WAIT_OBJECT_0)error=ERROR_TIMEOUT;

        // Intentionally retain DLL, contexts, gateways, unwind tables and trampolines until
        // process exit: a callback may still be returning. Never free code under a game thread.
        return error;
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI TinyHookStart(void* data)noexcept {
    try { th::log::configure_debugger("th-hook"); } catch(...) {}
    AcquireSRWLockExclusive(&lifecycle);

    if(runtime){
        ReleaseSRWLockExclusive(&lifecycle);
        return ERROR_ALREADY_INITIALIZED;
    }

    DWORD result=ERROR_SUCCESS;

    try {
        // Bootstrap: total u32, capture-port u16, reserved u16, token[64], specs bytes.
        std::uint32_t total=0;
        if(!read_at(reinterpret_cast<std::uintptr_t>(data),&total,4)||total<80||total>th::max_frame)throw th::Error("invalid bootstrap size");

        th::Bytes b(total);
        if(!read_at(reinterpret_cast<std::uintptr_t>(data),b.data(),b.size()))throw th::Error("invalid bootstrap address");

        th::Reader r(b);
        (void)r.get32();
        auto port=r.get16();
        auto bootstrap_version=r.get16();
        if(bootstrap_version>1||port==0)throw th::Error("invalid bootstrap header");

        std::string token(reinterpret_cast<const char*>(b.data()+8),th::token_size);
        if(!th::valid_token(token))throw th::Error("invalid bootstrap token");

        std::size_t config_offset=72;
        std::uint64_t selected_tag{};
        if(bootstrap_version==1){
            if(total<88)throw th::Error("invalid bootstrap v1 size");
            th::Reader tag_reader(std::span<const std::uint8_t>(b).subspan(72,8));
            selected_tag=tag_reader.number(8);
            config_offset=80;
        }
        auto specs=th::decode_specs(std::span<const std::uint8_t>(b).subspan(config_offset));
        if(specs.empty())throw th::Error("host script defines no hooks");
        if(bootstrap_version==0)selected_tag=th::config_tag(th::encode_specs(specs));

        HMODULE self{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&TinyHookStart),&self))throw th::Error("cannot pin hook DLL");

        runtime=new Runtime;
        runtime->port=port;
        runtime->profile_tag=selected_tag;
        runtime->token=std::move(token);
        runtime->specs=std::move(specs);
        setup(*runtime);
        spdlog::info("hook runtime started for PID {}",GetCurrentProcessId());
    }
    catch(const std::exception& e) {
        spdlog::error("hook setup failed: {}",e.what());

        // Best-effort diagnostic reaches the native host, never a GUI dialog.
        if(runtime){
            try{
                auto s=th::win::connect_local(runtime->port);
                th::win::send_all(s.s,th::hello(2,runtime->token,runtime->profile_tag));
                std::array<std::uint8_t,4> ack{};
                th::win::recv_all(s.s,ack);
                th::win::send_all(s.s,th::encode_event(th::Event{
                    3,0,GetCurrentProcessId(),0,std::string("hook setup failed: ")+e.what()
                }));
            }
            catch(...){}stop_runtime();
        }

        result=ERROR_DLL_INIT_FAILED;
    }
    catch(...){
        spdlog::error("hook setup failed: unknown exception");
        if(runtime)stop_runtime();
        result=ERROR_DLL_INIT_FAILED;
    }

    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}

extern "C" __declspec(dllexport) DWORD WINAPI TinyHookStop(void*)noexcept {
    AcquireSRWLockExclusive(&lifecycle);
    auto result=stop_runtime();
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}

// Keep CRT thread notifications enabled (/MT); do no application work under loader lock.
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){
    return TRUE;
}
