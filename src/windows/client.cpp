#include "win.hpp"
#include "log.hpp"
#include <spdlog/spdlog.h>
#include "scan.hpp"
#include "th/selection.hpp"
#include <exception>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

namespace {
    using namespace th;

    using namespace th::win;

    struct Module {
        HMODULE h{};
        explicit Module(HMODULE x):h(x){}~Module(){
            if(h)FreeLibrary(h);
        }
        Module(const Module&)=delete;
    };

    struct Remote {
        HANDLE process;
        void* p{};
        bool retain=false;

        Remote(HANDLE h,std::span<const std::uint8_t> data):process(h) {
            p=VirtualAllocEx(h,nullptr,data.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p)throw Error(error("VirtualAllocEx failed"));

            SIZE_T n=0;
            if(!WriteProcessMemory(h,p,data.data(),data.size(),&n)||n!=data.size()){
                VirtualFreeEx(h,p,0,MEM_RELEASE);
                p=nullptr;
                throw Error(error("WriteProcessMemory failed"));
            }
        }

        ~Remote(){
            if(p&&!retain)VirtualFreeEx(process,p,0,MEM_RELEASE);
        }

        Remote(const Remote&)=delete;
        Remote&operator=(const Remote&)=delete;
    };

    std::wstring exe_path(HMODULE module=nullptr) {
        std::wstring path(32768,L'\0');
        auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));

        if(!n||n>=path.size())throw Error(error("GetModuleFileName failed"));
        path.resize(n);
        return path;
    }

    MODULEENTRY32W find_module(DWORD pid,std::wstring_view name,bool required=true) {
        Handle snapshot;

        // A loader can transiently change the module list while it is snapshotted.
        for(unsigned i=0;i<8;++i){
            HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
            if(s!=INVALID_HANDLE_VALUE){
                snapshot.h=s;
                break;
            }
            if(GetLastError()!=ERROR_BAD_LENGTH)break;
            Sleep(10);
        }

        if(!snapshot)throw Error(error("module snapshot failed (check Windows PID and matching x86/x64 architecture)"));

        MODULEENTRY32W m{};
        m.dwSize=sizeof(m);
        MODULEENTRY32W result{};
        unsigned matches=0;

        if(Module32FirstW(snapshot.h,&m))do{
            if(name.empty() || _wcsicmp(m.szModule,std::wstring(name).c_str())==0){
                result=m;
                ++matches;
                if(name.empty())break;
            }
        }
        while(Module32NextW(snapshot.h,&m));

        if(matches>1)throw Error("multiple modules have the requested filename");

        if(!matches&&required)throw Error("module not found in target: "+utf8(name));
        return result;
    }

    void check_architecture(HANDLE process,DWORD pid) {
        using Fn=BOOL(WINAPI*)(HANDLE,USHORT*,USHORT*);
        const auto raw=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsWow64Process2");
        Fn f{};
        static_assert(sizeof(f)==sizeof(raw));
        std::memcpy(&f,&raw,sizeof(f)); // Win32 explicitly permits GetProcAddress -> matching function pointer.
        if(f) {
            USHORT machine{},native{};
            if(f(process,&machine,&native)) {
                if((machine?machine:native)!=pe_machine)
                    throw Error(std::string("target architecture differs: use the ")+build_arch_name+" client only with "+build_arch_name+" games");
            } else if(GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED && GetLastError()!=ERROR_NOT_SUPPORTED)
                throw Error(error("IsWow64Process2 failed"));
        }
        // Verify the actual PE as well: handles older Wine and avoids assuming
        // that host pointer width or WoW64 reporting equals the game's bitness.
        auto main=find_module(pid,L"");
        (void)pe_header(process,reinterpret_cast<std::uintptr_t>(main.modBaseAddr));
    }

    struct ResolvedHook {
        std::string name,module;
        std::uintptr_t offset{};
        HookPoint point=HookPoint::entry;
    };

    std::vector<ResolvedHook> resolve_specs(HANDLE process,DWORD pid,const std::vector<Spec>& specs) {
        if(specs.empty())throw Error("engine profile contains no hooks");
        std::vector<ResolvedHook> result;
        result.reserve(specs.size());
        for(const auto& spec:specs) {
            if(spec.arch!=build_arch)throw Error("profile architecture does not match this client/DLL build");
            auto module=find_module(pid,wide(spec.module));
            auto base=reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            auto address=resolve_in(process,base,spec);
            result.push_back({spec.name,utf8(module.szModule),address-base,spec.point});
        }
        return result;
    }

    void print_matches(const std::vector<ResolvedHook>& matches) {
        for(const auto& match:matches) {
            std::cout<<"MATCH "<<match.name<<" ["<<build_arch_name<<"] "<<match.module
                     <<" + 0x"<<std::hex<<match.offset<<std::dec<<"; "
                     <<(match.point==HookPoint::mid?"mid-function":"function-entry")<<"\n";
        }
    }

    std::vector<CatalogItem> fetch_catalog(std::uint16_t port,std::string_view token,std::string_view process_name) {
        auto socket=connect_local(port);
        send_all(socket.s,hello(3,token));
        auto query=framed(encode_catalog_query({build_arch,std::string(process_name)}));
        send_all(socket.s,query);
        return decode_catalog(recv_frame(socket.s));
    }

    Bytes fetch_config(std::uint16_t port,std::string_view token,std::uint64_t tag) {
        auto socket=connect_local(port);
        send_all(socket.s,hello(4,token));
        auto request=framed(encode_config_request(tag));
        send_all(socket.s,request);
        return recv_frame(socket.s);
    }

    struct SelectedProfile {
        CatalogItem item;
        Bytes config;
        std::vector<Spec> specs;
        std::vector<ResolvedHook> matches;
    };

    SelectedProfile detect(HANDLE process,DWORD pid,std::uint16_t port,std::string_view token,
                           std::string_view process_name,std::string_view requested) {
        auto catalog=fetch_catalog(port,token,process_name);
        if(catalog.empty())throw Error("host returned no engine profiles for this architecture");

        if(!requested.empty()) {
            auto it=std::find_if(catalog.begin(),catalog.end(),[&](const CatalogItem& item){
                return item.id==requested;
            });
            if(it==catalog.end())throw Error("requested engine profile not found: "+std::string(requested));
            try {
                auto config=fetch_config(port,token,it->tag);
                auto specs=decode_specs(config);
                auto matches=resolve_specs(process,pid,specs);
                return {*it,std::move(config),std::move(specs),std::move(matches)};
            }
            catch(const std::exception& e) {
                throw Error("requested engine profile did not match: "+std::string(requested)+": "+e.what());
            }
        }

        for(auto priority:{ProcessPriority::exact,ProcessPriority::generic,ProcessPriority::mismatch}) {
            std::vector<SelectedProfile> matched;
            for(const auto& item:catalog) {
                if(item.priority!=priority)continue;
                try {
                    auto config=fetch_config(port,token,item.tag);
                    auto specs=decode_specs(config);
                    auto matches=resolve_specs(process,pid,specs);
                    matched.push_back({item,std::move(config),std::move(specs),std::move(matches)});
                }
                catch(const std::exception& e) {
                    spdlog::debug("profile {} ({}) did not match: {}",item.id,item.engine,e.what());
                }
            }
            if(matched.size()==1)return std::move(matched.front());
            if(matched.size()>1){
                std::string ids;
                for(const auto& item:matched){
                    if(!ids.empty())ids+=", ";
                    ids+=item.item.id;
                }
                throw Error("multiple engine profiles matched at the same priority: "+ids+"; use --engine PROFILE_ID");
            }
        }
        throw Error("no engine profile matched the target; set TINYHOOK_LOG=debug to inspect rejected candidates");
    }

    std::uintptr_t remote_system_proc(DWORD pid,const char* name) {
        auto function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);
        if(!function)throw Error("missing system export");

        // Do not assume that local and remote kernel32/kernelbase addresses are equal.
        HMODULE owner{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(function),&owner))throw Error(error("cannot locate export owner"));

        auto remote=find_module(pid,std::filesystem::path(exe_path(owner)).filename().wstring());

        auto rva=reinterpret_cast<std::uintptr_t>(function)-reinterpret_cast<std::uintptr_t>(owner);

        if(rva>=remote.modBaseSize)throw Error("system module layout mismatch");
        return reinterpret_cast<std::uintptr_t>(remote.modBaseAddr)+rva;
    }

    DWORD remote_call(HANDLE process,std::uintptr_t address,void* arg,Remote* storage=nullptr) {
        Handle thread(CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(address),arg,0,nullptr));

        if(!thread)throw Error(error("CreateRemoteThread failed"));

        auto wait=WaitForSingleObject(thread.h,15000);

        if(wait!=WAIT_OBJECT_0){
            if(storage)storage->retain=true;
            throw Error("remote call did not finish; parameter memory retained for safety. Do not reattach; restart the target before retrying.");
        }

        DWORD result{};
        if(!GetExitCodeThread(thread.h,&result))throw Error(error("GetExitCodeThread failed"));
        return result;
    }

    std::uintptr_t export_rva(const std::wstring& path,const char* name) {
        Module m(LoadLibraryExW(path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));
        if(!m.h)throw Error(error("cannot inspect hook DLL"));

        auto f=GetProcAddress(m.h,name);
        if(!f)throw Error(std::string("missing hook DLL export: ")+name);

        return reinterpret_cast<std::uintptr_t>(f)-reinterpret_cast<std::uintptr_t>(m.h);
    }

    DWORD parse_pid(std::wstring_view s) {
        std::uint64_t n=0;
        if(s.empty()||s.size()>10)throw Error("invalid Windows PID");
        for(auto c:s){
            if(c<L'0'||c>L'9')throw Error("invalid Windows PID");
            n=n*10+unsigned(c-L'0');
        }
        if(!n||n>UINT32_MAX)throw Error("invalid Windows PID");
        return static_cast<DWORD>(n);
    }

    std::string process_executable(DWORD pid) {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
        if(!snapshot)throw Error(error("process snapshot failed"));
        PROCESSENTRY32W p{};
        p.dwSize=sizeof(p);
        if(Process32FirstW(snapshot.h,&p))do{
            if(p.th32ProcessID==pid)return utf8(p.szExeFile);
        }while(Process32NextW(snapshot.h,&p));
        throw Error("target process disappeared while reading its executable name");
    }

    void list() {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
        if(!snapshot)throw Error(error("process snapshot failed"));

        PROCESSENTRY32W p{};
        p.dwSize=sizeof(p);

        std::cout<<"Windows PID\tExecutable\n";

        if(Process32FirstW(snapshot.h,&p))do{
            std::cout<<p.th32ProcessID<<'\t'<<utf8(p.szExeFile)<<'\n';
        }
        while(Process32NextW(snapshot.h,&p));
    }
    using Options=std::map<std::wstring,std::wstring>;

    std::string read_token(const Options& opts) {
        std::string token;
        if(opts.contains(L"--token"))token=utf8(opts.at(L"--token"));
        else if(opts.contains(L"--token-file")){
            std::ifstream f(std::filesystem::path(opts.at(L"--token-file")),std::ios::binary);
            if(!f)throw Error("cannot read token file");
            char bytes[67]{};
            f.read(bytes,sizeof(bytes));
            token.assign(bytes,static_cast<std::size_t>(f.gcount()));
            while(!token.empty()&&(token.back()=='\r'||token.back()=='\n'))token.pop_back();
        }
        if(!valid_token(token))throw Error("supply --token-file or a 64-character lowercase hex --token");
        return token;
    }

    std::uint16_t read_port(const Options& opts) {
        auto port=opts.contains(L"--port")?parse_pid(opts.at(L"--port")):default_agent_port;
        if(port>65535)throw Error("port out of range");
        return static_cast<std::uint16_t>(port);
    }

    DWORD window_process(std::wstring_view title) {
        if(title.empty()||utf8(title).size()>1024)throw Error("invalid window title");
        struct Search {
            std::wstring_view title;
            std::vector<DWORD> pids;
            std::exception_ptr failure;
            static BOOL CALLBACK visit(HWND window,LPARAM data) {
                auto& search=*reinterpret_cast<Search*>(data);
                try {
                    if(!IsWindowVisible(window))return TRUE;
                    wchar_t name[1025]{};
                    const auto count=GetWindowTextW(window,name,1025);
                    if(count<=0||GetWindowTextLengthW(window)>1024)return TRUE;
                    if(std::wstring_view(name,static_cast<std::size_t>(count))!=search.title)return TRUE;
                    DWORD pid{};
                    GetWindowThreadProcessId(window,&pid);
                    if(pid&&std::find(search.pids.begin(),search.pids.end(),pid)==search.pids.end())search.pids.push_back(pid);
                    return TRUE;
                }
                catch(...){search.failure=std::current_exception();return FALSE;}
            }
        } search{title,{},{}};
        if(!EnumWindows(Search::visit,reinterpret_cast<LPARAM>(&search))){
            if(search.failure)std::rethrow_exception(search.failure);
            throw Error(error("EnumWindows failed"));
        }
        if(search.pids.empty())throw Error("No window with that title in this bottle. Keep the game visible and pick it again.");
        if(search.pids.size()!=1)throw Error("Multiple processes have that window title. Use --pid to choose the target.");
        return search.pids.front();
    }

    int run_action(std::wstring_view command,const Options& opts,DWORD pid) {
        if(pid==GetCurrentProcessId())throw th::Error("refusing to attach to the injector itself");

        auto rights=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ;
        if(command!=L"probe")rights|=PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE;
        Handle process(OpenProcess(rights,FALSE,pid));
        if(!process)throw th::Error(th::win::error("OpenProcess failed"));
        check_architecture(process.h,pid);

        auto path=opts.contains(L"--dll")?std::filesystem::absolute(opts.at(L"--dll")):std::filesystem::path(exe_path()).parent_path()/L"th-hook.dll";
        std::uintptr_t rva{};
        MODULEENTRY32W loaded{};
        if(command!=L"probe") {
            if(!std::filesystem::is_regular_file(path))throw th::Error("hook DLL does not exist");
            rva=export_rva(path.wstring(),command==L"attach"?"TinyHookStart":"TinyHookStop");
            loaded=find_module(pid,path.filename().wstring(),false);
        }

        if(command==L"stop") {
            if(!loaded.modBaseAddr)throw th::Error("hook DLL is not loaded");
            if(rva>=loaded.modBaseSize)throw th::Error("DLL layout mismatch");

            auto code=remote_call(process.h,reinterpret_cast<std::uintptr_t>(loaded.modBaseAddr)+rva,nullptr);

            if(code)throw th::Error("stop returned Win32 error "+std::to_string(code));
            std::cout<<"Hooks disabled. DLL retained until target exit; restart target before attaching a different script.\n";
            return 0;
        }

        if(loaded.modBaseAddr)throw th::Error("hook DLL already loaded; restart target before attaching again");

        auto token=read_token(opts);
        auto port=read_port(opts);

        th::win::Wsa wsa;
        auto process_name=process_executable(pid);
        auto requested=opts.contains(L"--engine")?th::win::utf8(opts.at(L"--engine")):std::string{};
        spdlog::info("requesting engine catalog from 127.0.0.1:{} for {}",port,process_name);
        auto selected=detect(process.h,pid,static_cast<std::uint16_t>(port),token,process_name,requested);
        spdlog::info("detected profile {} ({})",selected.item.id,selected.item.engine);
        std::cout<<"ENGINE "<<selected.item.id<<" ("<<selected.item.engine<<")\n";
        print_matches(selected.matches);
        if(command==L"probe") {
            std::cout<<"Read-only probe passed. No DLL loaded and no game memory modified.\n";
            return 0;
        }
        auto& config=selected.config;

        auto full=path.wstring();
        auto* bytes=reinterpret_cast<const std::uint8_t*>(full.c_str());

        Remote remote_path(process.h,{
            bytes,(full.size()+1)*sizeof(wchar_t)
        });

        (void)remote_call(process.h,remote_system_proc(pid,"LoadLibraryW"),remote_path.p,&remote_path);

        loaded=find_module(pid,path.filename().wstring());
        if(rva>=loaded.modBaseSize)throw th::Error("DLL layout mismatch");

        th::Bytes boot;
        th::u32(boot,static_cast<std::uint32_t>(80+config.size()));
        th::u16(boot,static_cast<std::uint16_t>(port));
        th::u16(boot,1); // bootstrap v1 carries the selected engine profile tag
        boot.insert(boot.end(),token.begin(),token.end());
        th::u64(boot,selected.item.tag);
        boot.insert(boot.end(),config.begin(),config.end());

        Remote remote_boot(process.h,boot);
        auto result=remote_call(process.h,reinterpret_cast<std::uintptr_t>(loaded.modBaseAddr)+rva,remote_boot.p,&remote_boot);

        if(result)throw th::Error("hook initialization returned Win32 error "+std::to_string(result)+"; inspect th-host diagnostics and restart target before retrying");

        std::cout<<"Attached to Windows PID "<<pid<<". Watch th-host for connection/capture diagnostics.\n";
        return 0;
    }

    int listen(const Options& opts) {
        auto token=read_token(opts);
        auto port=read_port(opts);
        Wsa wsa;
        auto socket=connect_local(port);
        send_all(socket.s,hello(selection_listener_role,token));
        std::array<std::uint8_t,4> ack{};
        recv_all(socket.s,ack);
        if(std::string_view(reinterpret_cast<const char*>(ack.data()),ack.size())!="OKAY")
            throw Error("Another window-selection client is connected. Stop it before switching bottles.");
        DWORD timeout=0;
        if(setsockopt(socket.s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout)))
            throw Error(error("cannot set selection receive timeout"));
        std::cout<<"Ready. Run th-pick on the Mac, then click the game window."<<std::endl;
        for(;;){
            auto title=decode_selection(recv_frame(socket.s));
            SelectionResult result{false,{}};
            try {
                auto pid=window_process(wide(title));
                run_action(L"attach",opts,pid);
                result={true,"Attached to "+title+" (Windows PID "+std::to_string(pid)+")."};
            }
            catch(const std::exception& e){
                result.message=e.what();
                if(result.message.size()>4096||!valid_utf8(result.message))result.message="Attachment failed; see the client log.";
                spdlog::error("{}",e.what());
            }
            send_all(socket.s,framed(encode_selection_result(result)));
        }
    }

}

int wmain(int argc,wchar_t**argv) {
    th::log::configure_console("th-client");
    SetConsoleOutputCP(CP_UTF8);

    try {
        if(argc<2||std::wstring_view(argv[1])==L"--help") {
            std::cout<<"th-client list\n"
            "th-client probe --pid WINDOWS_PID --token-file FILE [--port 9002] [--engine PROFILE_ID]\n"
            "th-client attach --pid WINDOWS_PID [--dll th-hook.dll] --token-file FILE [--port 9002] [--engine PROFILE_ID]\n"
            "th-client stop --pid WINDOWS_PID [--dll th-hook.dll]\n"
            "th-client listen --token-file FILE [--dll th-hook.dll] [--port 9002] [--engine PROFILE_ID]\n"
            "Use --window TITLE instead of --pid to select by window title.\n"
            "Use the same Wine/CrossOver bottle as the game. This client targets "<<build_arch_name<<".\n";
            return argc<2?1:0;
        }

        std::wstring command=argv[1];
        if(command==L"list"){
            if(argc!=2)throw th::Error("list takes no options");
            list();
            return 0;
        }

        if(command!=L"attach"&&command!=L"stop"&&command!=L"probe"&&command!=L"listen")throw th::Error("unknown command");

        std::map<std::wstring,std::wstring> opts;

        for(int i=2;i<argc;++i){
            std::wstring key=argv[i];
            if(key!=L"--pid"&&key!=L"--dll"&&key!=L"--token-file"&&key!=L"--token"&&key!=L"--port"&&key!=L"--engine"&&key!=L"--window")throw th::Error("unknown option");
            if(++i>=argc||!opts.emplace(key,argv[i]).second)throw th::Error("missing/duplicate option");
        }

        if(command==L"listen"){
            if(opts.contains(L"--pid")||opts.contains(L"--window"))throw Error("listen takes no target; choose it with th-pick");
            return listen(opts);
        }
        if(opts.contains(L"--pid")==opts.contains(L"--window"))throw Error("choose --pid WINDOWS_PID or --window TITLE");
        auto target=opts.contains(L"--pid")?parse_pid(opts.at(L"--pid")):window_process(opts.at(L"--window"));
        return run_action(command,opts,target);
    }
    catch(const std::exception& e){
        spdlog::error("{}",e.what());
        return 1;
    }
}
