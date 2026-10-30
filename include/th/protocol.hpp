#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace th {
    using Bytes = std::vector<std::uint8_t>;

    constexpr std::size_t max_hooks = 16, max_profiles = 64, max_capture = 8192, max_frame = 32768;

    constexpr std::size_t token_size = 64, hello_size = 4 + 1 + token_size;

    constexpr std::uint16_t default_ws_port = 9001, default_agent_port = 9002;

    struct Error : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    inline void u8(Bytes& b, std::uint8_t x) {
        b.push_back(x);
    }

    inline void u16(Bytes& b, std::uint16_t x) {
        for (unsigned i=0;i<2;++i) b.push_back(static_cast<std::uint8_t>(x >> (i*8)));
    }

    inline void u32(Bytes& b, std::uint32_t x) {
        for (unsigned i=0;i<4;++i) b.push_back(static_cast<std::uint8_t>(x >> (i*8)));
    }

    inline void u64(Bytes& b, std::uint64_t x) {
        for (unsigned i=0;i<8;++i) b.push_back(static_cast<std::uint8_t>(x >> (i*8)));
    }

    inline void text(Bytes& b, std::string_view s) {
        if (s.size()>max_frame) throw Error("string too large");

        u32(b,static_cast<std::uint32_t>(s.size()));
        b.insert(b.end(),s.begin(),s.end());
    }

    class Reader {
        std::span<const std::uint8_t> b_;
        std::size_t p_{};

        public:
        explicit Reader(std::span<const std::uint8_t> b): b_(b) {}
        std::size_t remaining() const {
            return b_.size()-p_;
        }

        std::uint64_t number(unsigned n) {
            if(n>8 || remaining()<n) throw Error("truncated protocol message");

            std::uint64_t x=0;
            for(unsigned i=0;i<n;++i) x|=std::uint64_t(b_[p_++])<<(i*8);
            return x;
        }

        std::uint8_t get8() {
            return static_cast<std::uint8_t>(number(1));
        }

        std::uint16_t get16() {
            return static_cast<std::uint16_t>(number(2));
        }

        std::uint32_t get32() {
            return static_cast<std::uint32_t>(number(4));
        }

        std::string string(std::size_t cap=max_frame) {
            auto n=get32();
            if(n>cap || remaining()<n) throw Error("invalid string length");

            std::string s(reinterpret_cast<const char*>(b_.data()+p_),n);
            p_+=n;
            return s;
        }

        void end() const {
            if(remaining()) throw Error("trailing protocol bytes");
        }
    };

    inline Bytes framed(std::span<const std::uint8_t> payload) {
        if(payload.size()>max_frame) throw Error("frame too large");

        Bytes b;
        b.reserve(payload.size()+4);
        u32(b,static_cast<std::uint32_t>(payload.size()));

        b.insert(b.end(),payload.begin(),payload.end());
        return b;
    }

    inline bool valid_token(std::string_view s) {
        if(s.size()!=token_size) return false;

        for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;

        return true;
    }

    inline bool equal_token(std::string_view a, std::string_view b) {
        if(a.size()!=b.size()) return false;

        unsigned x=0;
        for(std::size_t i=0;i<a.size();++i) x|=static_cast<unsigned char>(a[i]^b[i]);
        return x==0;
    }

    inline std::uint64_t config_tag(std::span<const std::uint8_t> bytes) {
        // Non-cryptographic identity check only; authentication uses the independent token.
        std::uint64_t h=14695981039346656037ull;
        for(auto b:bytes){
            h^=b;
            h*=1099511628211ull;
        }
        return h;
    }

    inline std::uint64_t profile_tag(std::string_view id,std::span<const std::uint8_t> config) {
        // Distinguish profiles that intentionally share the same hook configuration but
        // use different Luau filters. This is still only an identity tag, never auth.
        std::uint64_t h=14695981039346656037ull;
        for(unsigned char c:id){
            h^=c;
            h*=1099511628211ull;
        }
        h^=0;
        h*=1099511628211ull;
        for(auto b:config){
            h^=b;
            h*=1099511628211ull;
        }
        return h;
    }

    inline Bytes hello(std::uint8_t role, std::string_view token, std::uint64_t tag=0) {
        if(!valid_token(token)) throw Error("token must be 64 lowercase hex characters");

        Bytes b{
            'T','H','K','2',role
        };
        b.insert(b.end(),token.begin(),token.end());
        if(role==2)u64(b,tag);
        return b;
    }

    enum class Encoding : std::uint8_t {
        utf8=1, utf16=2, cp932=3
    };

    enum class Location : std::uint8_t {
        symbol=1, rva=2, pattern=3
    };

    // The register order is also the order of the capture gateway's saved context.
    enum class Register : std::uint8_t {
        r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax,rsp,stack,
        eax,ecx,edx,ebx,ebp,esi,edi,esp
    };

    inline constexpr std::array<std::string_view,25> register_names={
        "r15","r14","r13","r12","r11","r10","r9","r8","rdi","rsi","rbp","rbx","rdx","rcx","rax","rsp","stack",
        "eax","ecx","edx","ebx","ebp","esi","edi","esp"
    };

    enum class Architecture : std::uint8_t { x86=1, x64=2 };

    enum class ProcessPriority : std::uint8_t {
        mismatch=0,
        generic=1,
        exact=2
    };

    struct CatalogQuery {
        Architecture arch=Architecture::x64;
        std::string process;
    };

    struct CatalogItem {
        std::string id,engine;
        std::uint64_t tag{};
        ProcessPriority priority=ProcessPriority::generic;
    };

    inline Bytes encode_catalog_query(const CatalogQuery& q) {
        if(q.arch!=Architecture::x86&&q.arch!=Architecture::x64)throw Error("invalid catalog architecture");
        if(q.process.empty()||q.process.size()>255||q.process.find('\0')!=q.process.npos)throw Error("invalid process name");
        Bytes b;
        u8(b,1);
        u8(b,static_cast<std::uint8_t>(q.arch));
        text(b,q.process);
        return b;
    }

    inline CatalogQuery decode_catalog_query(std::span<const std::uint8_t> b) {
        Reader r(b);
        if(r.get8()!=1)throw Error("unsupported catalog query version");
        CatalogQuery q;
        q.arch=static_cast<Architecture>(r.get8());
        q.process=r.string(255);
        r.end();
        if(q.arch!=Architecture::x86&&q.arch!=Architecture::x64)throw Error("invalid catalog architecture");
        if(q.process.empty())throw Error("empty process name");
        return q;
    }

    inline Bytes encode_catalog(std::span<const CatalogItem> items) {
        if(items.size()>max_profiles)throw Error("too many engine profiles");
        Bytes b;
        u8(b,1);
        u32(b,static_cast<std::uint32_t>(items.size()));
        for(const auto& item:items){
            if(item.id.empty()||item.id.size()>128||item.id.find('\0')!=item.id.npos)throw Error("invalid profile id");
            if(item.engine.empty()||item.engine.size()>128||item.engine.find('\0')!=item.engine.npos)throw Error("invalid engine name");
            if(item.priority<ProcessPriority::mismatch||item.priority>ProcessPriority::exact)throw Error("invalid process priority");
            text(b,item.id);
            text(b,item.engine);
            u64(b,item.tag);
            u8(b,static_cast<std::uint8_t>(item.priority));
        }
        if(b.size()>max_frame)throw Error("engine catalog too large");
        return b;
    }

    inline std::vector<CatalogItem> decode_catalog(std::span<const std::uint8_t> b) {
        Reader r(b);
        if(r.get8()!=1)throw Error("unsupported engine catalog version");
        auto n=r.get32();
        if(n>max_profiles)throw Error("too many engine profiles");
        std::vector<CatalogItem> result;
        result.reserve(n);
        for(std::uint32_t i=0;i<n;++i){
            CatalogItem item;
            item.id=r.string(128);
            item.engine=r.string(128);
            item.tag=r.number(8);
            item.priority=static_cast<ProcessPriority>(r.get8());
            if(item.id.empty()||item.engine.empty()||item.priority<ProcessPriority::mismatch||item.priority>ProcessPriority::exact)throw Error("invalid engine profile metadata");
            for(const auto& old:result)if(old.id==item.id||old.tag==item.tag)throw Error("duplicate engine profile identity");
            result.push_back(std::move(item));
        }
        r.end();
        return result;
    }

    inline Bytes encode_config_request(std::uint64_t tag) {
        Bytes b;
        u8(b,1);
        u64(b,tag);
        return b;
    }

    inline std::uint64_t decode_config_request(std::span<const std::uint8_t> b) {
        Reader r(b);
        if(r.get8()!=1)throw Error("unsupported configuration request version");
        auto tag=r.number(8);
        r.end();
        return tag;
    }

    enum class HookPoint : std::uint8_t { entry=0, mid=1 };
    enum class TextLayout : std::uint8_t { cstring=0, msvc_wstring32=1 };

    struct Spec {
        std::string name, module, symbol, pattern, expected;

        Architecture arch=Architecture::x64;
        HookPoint point=HookPoint::entry;
        TextLayout layout=TextLayout::cstring;
        std::int32_t hook_offset{};

        Location location=Location::symbol;

        Encoding encoding=Encoding::utf16;

        Register source=Register::rcx;

        std::uint32_t rva{}, stack_offset=40, limit=4096;

        std::int32_t add{};

        std::vector<std::int32_t> deref;
    };

    inline std::vector<int> parse_pattern(std::string_view s) {
        auto hex=[](char c)->int {
            if(c>='0'&&c<='9') return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            if(c>='A'&&c<='F')return c-'A'+10;
            return -1;
        };

        std::vector<int> result;

        while(!s.empty()) {
            auto p=s.find_first_not_of(" \t\r\n");
            if(p==s.npos) break;
            s.remove_prefix(p);

            auto e=s.find_first_of(" \t\r\n");
            auto t=s.substr(0,e);

            if(t=="?"||t=="??") result.push_back(-1);

            else if(t.size()==2&&hex(t[0])>=0&&hex(t[1])>=0) result.push_back(hex(t[0])*16+hex(t[1]));

            else throw Error("pattern requires two hex digits or ??, separated by spaces");

            if(result.size()>256) throw Error("pattern exceeds 256 bytes");

            if(e==s.npos) break;
            s.remove_prefix(e);
        }

        bool concrete=false;
        for(auto x:result) concrete|=x>=0;

        if(!concrete) throw Error("empty or all-wildcard pattern");
        return result;
    }

    inline bool pattern_at(std::span<const std::uint8_t> b, std::span<const int> p) {
        if(b.size()<p.size()) return false;

        for(std::size_t i=0;i<p.size();++i) if(p[i]>=0&&b[i]!=p[i]) return false;
        return true;
    }

    inline void validate(const Spec& s) {
        auto clean=[](std::string_view x,std::size_t n){
            return x.size()<=n && x.find('\0')==x.npos;
        };

        if(s.name.empty()||!clean(s.name,128)||!clean(s.module,255)||!clean(s.symbol,255)||!clean(s.pattern,1024)||!clean(s.expected,1024)) throw Error("invalid hook name/module/selector");

        if(s.limit<2||s.limit>max_capture||s.deref.size()>8||s.stack_offset>4096||s.stack_offset%(s.arch==Architecture::x86?4:8)) throw Error("invalid capture limit, pointer chain, or stack offset");

        if(s.source>Register::esp||s.encoding<Encoding::utf8||s.encoding>Encoding::cp932) throw Error("invalid source/encoding");

        if(s.arch!=Architecture::x86 && s.arch!=Architecture::x64) throw Error("invalid target architecture");
        if(s.source!=Register::stack &&
           ((s.arch==Architecture::x86 && s.source<Register::eax) ||
            (s.arch==Architecture::x64 && s.source>Register::rsp)))
            throw Error("capture register does not match target architecture");
        if(s.point!=HookPoint::entry && s.point!=HookPoint::mid) throw Error("invalid hook point");
        if(s.point==HookPoint::mid) {
            if(s.arch!=Architecture::x86) throw Error("mid-function hooks currently require x86");
            // A unique executable-section byte signature is sufficient to identify a
            // mid-function site. `expected` remains available as an optional second
            // guard, but requiring bytes *after* the signature made valid engine
            // revisions fail closed even when the actual hook signature still matched.
            if(s.expected.empty() && s.location!=Location::pattern)
                throw Error("mid-function hooks without expected bytes require a pattern selector");
            if(s.expected.empty()) {
                const auto parsed=parse_pattern(s.pattern);
                std::size_t concrete=0;
                for(auto byte:parsed) concrete+=byte>=0;
                if(concrete<12) throw Error("mid-function pattern needs at least 12 concrete bytes when expected is omitted");
            }
        }
        if(s.layout!=TextLayout::cstring && s.layout!=TextLayout::msvc_wstring32) throw Error("invalid text layout");
        if(s.layout==TextLayout::msvc_wstring32 && (s.arch!=Architecture::x86 || s.encoding!=Encoding::utf16))
            throw Error("msvc_wstring32 requires x86 and UTF-16");
        if(!s.expected.empty()) (void)parse_pattern(s.expected);

        if(s.location==Location::symbol) {
            if(s.symbol.empty()||s.rva||!s.pattern.empty()) throw Error("symbol selector must be exclusive");
        }

        else if(s.location==Location::rva) {
            if(!s.symbol.empty()||!s.pattern.empty()) throw Error("RVA selector must be exclusive");
        }

        else if(s.location==Location::pattern) {
            if(!s.symbol.empty()||s.rva) throw Error("pattern selector must be exclusive");
            (void)parse_pattern(s.pattern);
        }

        else throw Error("invalid selector");

        if(s.encoding==Encoding::utf16 && s.limit%2) throw Error("UTF-16 max_bytes must be even");
    }

    inline Bytes encode_specs(const std::vector<Spec>& specs) {
        if(specs.size()>max_hooks) throw Error("too many hooks");

        Bytes b;
        u32(b,2);
        u32(b,static_cast<std::uint32_t>(specs.size()));

        for(const auto& s:specs) {
            validate(s);
            text(b,s.name);
            text(b,s.module);
            text(b,s.symbol);
            text(b,s.pattern);
            text(b,s.expected);
            u8(b,static_cast<std::uint8_t>(s.arch));
            u8(b,static_cast<std::uint8_t>(s.point));
            u8(b,static_cast<std::uint8_t>(s.layout));
            u8(b,0);
            u32(b,std::bit_cast<std::uint32_t>(s.hook_offset));

            u8(b,static_cast<std::uint8_t>(s.location));
            u8(b,static_cast<std::uint8_t>(s.encoding));
            u8(b,static_cast<std::uint8_t>(s.source));
            u8(b,static_cast<std::uint8_t>(s.deref.size()));

            u32(b,s.rva);
            u32(b,s.stack_offset);
            u32(b,s.limit);
            u32(b,std::bit_cast<std::uint32_t>(s.add));

            for(auto x:s.deref)u32(b,std::bit_cast<std::uint32_t>(x));
        }

        if(b.size()>max_frame-128)throw Error("configuration too large");
        return b;
    }

    inline std::vector<Spec> decode_specs(std::span<const std::uint8_t> b) {
        Reader r(b);
        if(r.get32()!=2)throw Error("unsupported configuration version");

        auto n=r.get32();
        if(n>max_hooks)throw Error("too many hooks");
        std::vector<Spec> specs;

        for(std::uint32_t i=0;i<n;++i) {
            Spec s;
            s.name=r.string(128);
            s.module=r.string(255);
            s.symbol=r.string(255);
            s.pattern=r.string(1024);
            s.expected=r.string(1024);
            s.arch=static_cast<Architecture>(r.get8());
            s.point=static_cast<HookPoint>(r.get8());
            s.layout=static_cast<TextLayout>(r.get8());
            if(r.get8())throw Error("nonzero reserved configuration byte");
            s.hook_offset=std::bit_cast<std::int32_t>(r.get32());

            s.location=static_cast<Location>(r.get8());
            s.encoding=static_cast<Encoding>(r.get8());
            s.source=static_cast<Register>(r.get8());
            auto nd=r.get8();

            if(nd>8)throw Error("pointer chain too long");

            s.rva=r.get32();
            s.stack_offset=r.get32();
            s.limit=r.get32();
            s.add=std::bit_cast<std::int32_t>(r.get32());

            for(unsigned j=0;j<nd;++j)s.deref.push_back(std::bit_cast<std::int32_t>(r.get32()));

            validate(s);
            for(const auto& old:specs)if(old.name==s.name)throw Error("duplicate hook name");
            specs.push_back(std::move(s));
        }

        r.end();
        return specs;
    }

    struct Event {
        std::uint8_t kind=1;
        std::uint32_t hook{},pid{},tid{};
        std::string text;
    };

    inline Bytes encode_event(const Event& e) {
        Bytes b;
        u8(b,e.kind);
        u32(b,e.hook);
        u32(b,e.pid);
        u32(b,e.tid);
        text(b,e.text);
        return framed(b);
    }

    inline Event decode_event(std::span<const std::uint8_t> b) {
        Reader r(b);
        Event e;
        e.kind=r.get8();
        e.hook=r.get32();
        e.pid=r.get32();
        e.tid=r.get32();
        e.text=r.string();
        r.end();
        if(e.kind<1||e.kind>3)throw Error("invalid event kind");
        return e;
    }

    inline bool valid_utf8(std::string_view s) {
        for(std::size_t i=0;i<s.size();) {
            auto c=static_cast<unsigned char>(s[i++]);
            if(c<0x80)continue;

            unsigned n=0;
            std::uint32_t value=0,min=0;

            if(c>=0xc2&&c<=0xdf){
                n=1;
                value=c&31;
                min=0x80;
            }

            else if(c>=0xe0&&c<=0xef){
                n=2;
                value=c&15;
                min=0x800;
            }

            else if(c>=0xf0&&c<=0xf4){
                n=3;
                value=c&7;
                min=0x10000;
            }
            else return false;

            if(s.size()-i<n)return false;

            while(n--){
                auto d=static_cast<unsigned char>(s[i++]);
                if((d&0xc0)!=0x80)return false;
                value=(value<<6)|(d&63);
            }

            if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
        }

        return true;
    }
}
// namespace th
