#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <cctype>
#include <map>

namespace th::ws {
    inline std::string base64(std::span<const std::uint8_t> in) {
        constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;

        for(std::size_t i=0;i<in.size();i+=3) {
            auto n=std::min<std::size_t>(3,in.size()-i);
            std::uint32_t v=std::uint32_t(in[i])<<16;

            if(n>1)v|=std::uint32_t(in[i+1])<<8;
            if(n>2)v|=in[i+2];

            out+=alphabet[(v>>18)&63];
            out+=alphabet[(v>>12)&63];
            out+=n>1?alphabet[(v>>6)&63]:'=';
            out+=n>2?alphabet[v&63]:'=';
        }

        return out;
    }

    // SHA-1 is used ONLY for the RFC 6455 upgrade handshake, never for authentication.
    inline std::array<std::uint8_t,20> sha1(std::string_view input) {
        Bytes b(input.begin(),input.end());
        auto bits=std::uint64_t(b.size())*8;
        u8(b,0x80);

        while(b.size()%64!=56)u8(b,0);

        for(int i=7;i>=0;--i)u8(b,static_cast<std::uint8_t>(bits>>(i*8)));

        std::array<std::uint32_t,5> h{
            0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0
        };

        for(std::size_t p=0;p<b.size();p+=64) {
            std::array<std::uint32_t,80>w{};

            for(unsigned i=0;i<16;++i)for(unsigned j=0;j<4;++j)w[i]=(w[i]<<8)|b[p+i*4+j];

            for(unsigned i=16;i<80;++i)w[i]=std::rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);

            auto a=h[0],c=h[2],d=h[3],e=h[4],v=h[1];

            for(unsigned i=0;i<80;++i) {
                std::uint32_t f,k;

                if(i<20){
                    f=(v&c)|(~v&d);
                    k=0x5a827999;
                }

                else if(i<40){
                    f=v^c^d;
                    k=0x6ed9eba1;
                }

                else if(i<60){
                    f=(v&c)|(v&d)|(c&d);
                    k=0x8f1bbcdc;
                }

                else {
                    f=v^c^d;
                    k=0xca62c1d6;
                }

                auto t=std::rotl(a,5)+f+e+k+w[i];
                e=d;
                d=c;
                c=std::rotl(v,30);
                v=a;
                a=t;
            }

            h[0]+=a;
            h[1]+=v;
            h[2]+=c;
            h[3]+=d;
            h[4]+=e;
        }

        std::array<std::uint8_t,20> out{};
        for(unsigned i=0;i<5;++i)for(unsigned j=0;j<4;++j)out[i*4+j]=static_cast<std::uint8_t>(h[i]>>(24-j*8));
        return out;
    }

    inline std::string accept_key(std::string_view key) {
        return base64(sha1(std::string(key)+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    }

    inline std::string lower(std::string s) {
        for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    inline std::string_view trim(std::string_view s) {
        while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);

        while(!s.empty()&&(s.back()==' '||s.back()=='\t'))s.remove_suffix(1);
        return s;
    }

    inline bool has_token(std::string_view s,std::string_view token) {
        while(!s.empty()){
            auto p=s.find(',');
            if(lower(std::string(trim(s.substr(0,p))))==token)return true;
            if(p==s.npos)break;
            s.remove_prefix(p+1);
        }
        return false;
    }

    inline bool valid_key(std::string_view s) {
        constexpr std::string_view chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        if(s.size()!=24||s.substr(22)!="==")return false;

        for(unsigned i=0;i<22;++i)if(chars.find(s[i])==chars.npos)return false;

        return (chars.find(s[21])&15)==0;
    }

    inline std::string upgrade(std::string_view request,std::string_view token,const std::vector<std::string>& origins) {
        auto e=request.find("\r\n");
        if(e==request.npos)throw Error("bad HTTP request");

        auto expected="GET /?token="+std::string(token)+" HTTP/1.1";

        if(!equal_token(request.substr(0,e),expected))throw Error("invalid websocket path/token");

        request.remove_prefix(e+2);
        std::map<std::string,std::string> h;

        while(!request.empty()) {
            e=request.find("\r\n");
            if(e==request.npos)throw Error("bad HTTP header");

            auto line=request.substr(0,e);
            request.remove_prefix(e+2);
            if(line.empty())break;

            auto colon=line.find(':');
            if(colon==line.npos||colon==0)throw Error("bad HTTP header");

            auto name=lower(std::string(line.substr(0,colon)));
            auto value=std::string(trim(line.substr(colon+1)));

            if(!h.emplace(name,value).second)throw Error("duplicate HTTP header");
        }

        if(h["host"].empty()||lower(h["upgrade"])!="websocket"||!has_token(h["connection"],"upgrade")||h["sec-websocket-version"]!="13"||!valid_key(h["sec-websocket-key"]))throw Error("invalid websocket upgrade");

        // No-Origin clients still require the token. Browsers must match this allow-list.
        if(h.contains("origin")&&std::find(origins.begin(),origins.end(),h["origin"])==origins.end())throw Error("websocket origin not allowed");

        return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+accept_key(h["sec-websocket-key"])+"\r\n\r\n";
    }

    inline Bytes frame(std::uint8_t opcode,std::span<const std::uint8_t> p) {
        if(p.size()>max_frame)throw Error("websocket payload too large");

        Bytes b;
        u8(b,static_cast<std::uint8_t>(0x80|opcode));

        if(p.size()<126)u8(b,static_cast<std::uint8_t>(p.size()));

        else {
            u8(b,126);
            u8(b,static_cast<std::uint8_t>(p.size()>>8));
            u8(b,static_cast<std::uint8_t>(p.size()));
        }

        b.insert(b.end(),p.begin(),p.end());
        return b;
    }

    inline Bytes text_frame(std::string_view text) {
        return frame(1,{
            reinterpret_cast<const std::uint8_t*>(text.data()),text.size()
        });
    }

    struct Control {
        bool complete=false;
        std::uint8_t opcode{};
        Bytes payload;
        std::size_t consumed{};
    };

    inline Control control(std::span<const std::uint8_t> b) {
        if(b.size()<2)return {};

        // This is a receive-only application endpoint. Data/fragment frames are rejected.
        const auto op=b[0]&15;

        if((b[0]&0xf0)!=0x80||!(b[1]&0x80)||(op!=8&&op!=9&&op!=10)||(b[1]&127)>125)throw Error("unsupported or invalid client websocket frame");

        auto len=std::size_t(b[1]&127);
        if(b.size()<6+len)return {};

        Control c{
            true,static_cast<std::uint8_t>(op),{},6+len
        };
        c.payload.resize(len);

        for(std::size_t i=0;i<len;++i)c.payload[i]=b[6+i]^b[2+i%4];

        if(op==8) {
            if(len==1)throw Error("invalid websocket close");

            if(len>=2) {
                unsigned code=unsigned(c.payload[0])*256+c.payload[1];

                if(code<1000||code>=5000||code==1004||code==1005||code==1006||code==1015||(code>=1016&&code<3000))throw Error("invalid close status");

                if(!valid_utf8({
                    reinterpret_cast<const char*>(c.payload.data()+2),len-2
                }))throw Error("invalid close reason");
            }
        }

        return c;
    }
}
// namespace th::ws
