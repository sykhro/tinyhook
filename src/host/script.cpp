#include "script.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cctype>
#if TH_WITH_LUAU
#include <lua.h>
#include <lualib.h>
#if TH_WITH_COMPILER
#include <Luau/Compiler.h>
#endif
#endif
namespace th {
#if TH_WITH_LUAU
    struct Script::Impl {
        lua_State* state{};

        int callback=LUA_NOREF;

        std::chrono::steady_clock::time_point deadline;

        ~Impl(){
            if(state)lua_close(state);
        }

        static void interrupt(lua_State* L,int) {
            auto* self=static_cast<Impl*>(lua_callbacks(L)->userdata);

            if(std::chrono::steady_clock::now()>self->deadline)luaL_error(L,"script time budget exceeded");
        }

        void budget(){
            deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);
        }
    };

    namespace {
        // Read raw fields so table metatables cannot turn configuration reads into callbacks.
        void field(lua_State* L,int index,const char* name) {
            index=lua_absindex(L,index);
            lua_pushstring(L,name);
            lua_rawget(L,index);
        }

        std::string str(lua_State* L,int index,const char* name,std::string fallback={}) {
            field(L,index,name);

            if(lua_isnil(L,-1)){
                lua_pop(L,1);
                return fallback;
            }

            if(lua_type(L,-1)!=LUA_TSTRING)throw Error(std::string(name)+" must be a string");

            std::size_t n;
            auto s=lua_tolstring(L,-1,&n);
            if(n>1024)throw Error("configuration string too long");

            std::string result(s,n);
            lua_pop(L,1);
            return result;
        }

        std::vector<std::string> strings(lua_State* L,int index,const char* name) {
            field(L,index,name);
            std::vector<std::string> result;
            if(lua_isnil(L,-1)){
                lua_pop(L,1);
                return result;
            }
            if(!lua_istable(L,-1))throw Error(std::string(name)+" must be an array of strings");
            auto n=static_cast<int>(lua_objlen(L,-1));
            if(n>16)throw Error(std::string(name)+" contains too many entries");
            for(int i=1;i<=n;++i){
                lua_rawgeti(L,-1,i);
                if(lua_type(L,-1)!=LUA_TSTRING)throw Error(std::string(name)+" must contain only strings");
                std::size_t size{};
                auto value=lua_tolstring(L,-1,&size);
                std::string_view view(value,size);
                if(size==0||size>255||view.find('\0')!=std::string_view::npos||!valid_utf8(view))throw Error("invalid process name hint");
                result.emplace_back(value,size);
                lua_pop(L,1);
            }
            lua_pop(L,1);
            return result;
        }

        std::int64_t number(lua_State* L,int index,const char* name,std::int64_t fallback,std::int64_t min,std::int64_t max) {
            field(L,index,name);
            if(lua_isnil(L,-1)){
                lua_pop(L,1);
                return fallback;
            }

            if(lua_type(L,-1)!=LUA_TNUMBER)throw Error(std::string(name)+" must be a number");

            double d=lua_tonumber(L,-1);
            lua_pop(L,1);

            if(!std::isfinite(d)||std::floor(d)!=d||d<static_cast<double>(min)||d>static_cast<double>(max))throw Error(std::string(name)+" is outside its integer range");

            return static_cast<std::int64_t>(d);
        }

        Spec read_spec(lua_State* L,int index) {
            index=lua_absindex(L,index);
            if(!lua_istable(L,index))throw Error("hook must be a table");

            Spec s;
            s.name=str(L,index,"name");
            s.module=str(L,index,"module");
            s.symbol=str(L,index,"symbol");
            s.pattern=str(L,index,"pattern");
            s.expected=str(L,index,"expected");
            auto arch=str(L,index,"arch","x64");
            if(arch=="x86")s.arch=Architecture::x86;
            else if(arch!="x64")throw Error("arch must be x86 or x64");
            auto point=str(L,index,"point","entry");
            if(point=="mid")s.point=HookPoint::mid;
            else if(point!="entry")throw Error("point must be entry or mid");
            auto layout=str(L,index,"layout","cstring");
            if(layout=="msvc_wstring32")s.layout=TextLayout::msvc_wstring32;
            else if(layout!="cstring")throw Error("unknown text layout");
            s.hook_offset=static_cast<std::int32_t>(number(L,index,"hook_offset",0,INT32_MIN,INT32_MAX));

            field(L,index,"rva");
            bool has_rva=!lua_isnil(L,-1);
            lua_pop(L,1);

            if(unsigned(!s.symbol.empty())+unsigned(!s.pattern.empty())+unsigned(has_rva)!=1)throw Error("exactly one of symbol, rva, pattern is required");

            s.location=has_rva?Location::rva:(!s.pattern.empty()?Location::pattern:Location::symbol);

            s.rva=static_cast<std::uint32_t>(number(L,index,"rva",0,0,UINT32_MAX));

            auto source=str(L,index,"source",s.arch==Architecture::x86?"ecx":"rcx");
            bool found=false;

            for(unsigned i=0;i<register_names.size();++i)if(register_names[i]==source){
                s.source=static_cast<Register>(i);
                found=true;
                break;
            }

            if(!found)throw Error("unknown capture register: "+source);

            auto encoding=str(L,index,"encoding","utf16");

            if(encoding=="utf16")s.encoding=Encoding::utf16;
            else if(encoding=="utf8")s.encoding=Encoding::utf8;
            else if(encoding=="cp932")s.encoding=Encoding::cp932;
            else throw Error("unknown encoding: "+encoding);

            s.stack_offset=static_cast<std::uint32_t>(number(L,index,"stack_offset",s.arch==Architecture::x86?4:40,0,4096));

            s.limit=static_cast<std::uint32_t>(number(L,index,"max_bytes",4096,2,max_capture));

            s.add=static_cast<std::int32_t>(number(L,index,"add",0,INT32_MIN,INT32_MAX));

            field(L,index,"deref");

            if(!lua_isnil(L,-1)) {
                if(!lua_istable(L,-1))throw Error("deref must be an array");

                auto n=static_cast<int>(lua_objlen(L,-1));
                if(n>8)throw Error("pointer chain exceeds eight steps");

                for(int i=1;i<=n;++i){
                    lua_rawgeti(L,-1,i);
                    if(lua_type(L,-1)!=LUA_TNUMBER)throw Error("pointer offsets must be integers");
                    auto d=lua_tonumber(L,-1);
                    if(!std::isfinite(d)||std::floor(d)!=d||d<INT32_MIN||d>INT32_MAX)throw Error("invalid pointer offset");
                    s.deref.push_back(static_cast<std::int32_t>(d));
                    lua_pop(L,1);
                }
            }

            lua_pop(L,1);
            validate(s);
            return s;
        }
    }

    Script::Script(const std::string& filename) {
        if(filename.empty())return;

        std::ifstream in(filename,std::ios::binary);
        if(!in)throw Error("cannot open script: "+filename);

        in.seekg(0,std::ios::end);
        auto n=in.tellg();
        if(n<0||n>1024*1024)throw Error("script file exceeds 1 MiB");
        in.seekg(0);

        std::string source(static_cast<std::size_t>(n),'\0');
        if(!in.read(source.data(),n))throw Error("cannot read script");

        impl_=std::make_unique<Impl>();
        auto* L=impl_->state=luaL_newstate();
        if(!L)throw Error("cannot create Luau state");

        luaL_openlibs(L);

        luaL_sandbox(L);
        luaL_sandboxthread(L);

        lua_callbacks(L)->userdata=impl_.get();

        std::string bytecode;

        constexpr std::string_view magic="THBC1:luau-0.740\n";

        if(source.starts_with(magic))bytecode=source.substr(magic.size());

        else {
#if TH_WITH_COMPILER
            if(source.size()>256*1024)throw Error("source script exceeds 256 KiB");

            Luau::CompileOptions opts;
            opts.optimizationLevel=2;
            opts.debugLevel=1;
            bytecode=Luau::compile(source,opts);

#else
            throw Error("VM-only build: compile source with th-compile, then pass the .luauc file to --script");

#endif
        }

        if(luau_load(L,filename.c_str(),bytecode.data(),bytecode.size(),0)!=0)throw Error(lua_tostring(L,-1)?lua_tostring(L,-1):"Luau load failed");

        impl_->budget();
        lua_callbacks(L)->interrupt=&Impl::interrupt;

        auto status=lua_pcall(L,0,1,0);
        lua_callbacks(L)->interrupt=nullptr;

        if(status!=0)throw Error(lua_tostring(L,-1)?lua_tostring(L,-1):"Luau script failed");

        if(!lua_istable(L,-1))throw Error("script must return { hooks = {...}, on_text = function(...) ... end }");

        auto fallback_id=std::filesystem::path(filename).stem().string();
        id_=str(L,-1,"id",fallback_id);
        if(id_.empty()||id_.size()>128||id_.find('\0')!=id_.npos||!valid_utf8(id_))throw Error("id must be valid UTF-8 and at most 128 bytes");
        engine_=str(L,-1,"engine",id_);
        if(engine_.empty()||engine_.size()>128||engine_.find('\0')!=engine_.npos||!valid_utf8(engine_))throw Error("engine must be valid UTF-8 and at most 128 bytes");
        processes_=strings(L,-1,"processes");

        field(L,-1,"hooks");

        if(!lua_isnil(L,-1)) {
            if(!lua_istable(L,-1))throw Error("hooks must be an array");
            auto count=static_cast<int>(lua_objlen(L,-1));

            if(count>static_cast<int>(max_hooks))throw Error("too many hooks");

            for(int i=1;i<=count;++i){
                lua_rawgeti(L,-1,i);
                specs_.push_back(read_spec(L,-1));
                lua_pop(L,1);
            }
        }

        lua_pop(L,1);
        field(L,-1,"on_text");

        if(!lua_isnil(L,-1)){
            if(!lua_isfunction(L,-1))throw Error("on_text must be a function");
            impl_->callback=lua_ref(L,-1);
        }

        lua_settop(L,0);
        (void)decode_specs(encode_specs(specs_));
    }

    bool Script::supports(Architecture arch) const {
        if(specs_.empty())return false;
        for(const auto& spec:specs_)if(spec.arch!=arch)return false;
        return true;
    }

    ProcessPriority Script::process_priority(std::string_view process) const {
        if(processes_.empty())return ProcessPriority::generic;
        auto lower=[](std::string_view value){
            std::string out(value);
            for(char& c:out)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
            return out;
        };
        auto wanted=lower(process);
        for(const auto& hint:processes_)if(lower(hint)==wanted)return ProcessPriority::exact;
        return ProcessPriority::mismatch;
    }

    std::optional<std::string> Script::apply(const Event& event) {
        if(!impl_||impl_->callback==LUA_NOREF)return event.text;

        auto* L=impl_->state;
        lua_settop(L,0);
        lua_getref(L,impl_->callback);
        lua_pushlstring(L,event.text.data(),event.text.size());

        lua_createtable(L,0,4);

        auto name=event.hook<specs_.size()?std::string_view(specs_[event.hook].name):std::string_view("stdin");

        lua_pushlstring(L,name.data(),name.size());
        lua_setfield(L,-2,"hook");

        lua_pushnumber(L,event.pid);
        lua_setfield(L,-2,"pid");
        lua_pushnumber(L,event.tid);
        lua_setfield(L,-2,"tid");
        lua_pushnumber(L,event.hook);
        lua_setfield(L,-2,"hook_id");

        impl_->budget();
        lua_callbacks(L)->interrupt=&Impl::interrupt;

        auto status=lua_pcall(L,2,1,0);
        lua_callbacks(L)->interrupt=nullptr;

        if(status!=0){
            spdlog::error("Luau: {}",lua_tostring(L,-1)?lua_tostring(L,-1):"callback failed");
            lua_settop(L,0);
            return std::nullopt;
        }

        std::optional<std::string> result;

        if(lua_type(L,-1)==LUA_TSTRING){
            std::size_t n;
            const char* s=lua_tolstring(L,-1,&n);
            if(n<=max_frame)result=std::string(s,n);
            else spdlog::warn("Luau: output exceeds 32 KiB; dropped");
        }

        else if(!lua_isnil(L,-1))spdlog::warn("Luau: on_text must return a string or nil; dropped");

        lua_settop(L,0);
        return result;
    }

#else
    struct Script::Impl {};

    Script::Script(const std::string& filename){
        if(!filename.empty())throw Error("this build has TINYHOOK_WITH_LUAU=OFF; rebuild with Luau to load scripts");
    }

    bool Script::supports(Architecture) const { return false; }
    ProcessPriority Script::process_priority(std::string_view) const { return ProcessPriority::generic; }

    std::optional<std::string> Script::apply(const Event& event){
        return event.text;
    }

#endif
    Script::~Script()=default;
}
// namespace th
