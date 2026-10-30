#include "th/protocol.hpp"
#include "th/websocket.hpp"
#include "th/gateway.hpp"
#include <iostream>
#define REQUIRE(x) do {if(!(x))throw th::Error("check failed: " #x);}while(false)
template<class F>void fails(F f){
    bool caught=false;
    try{
        f();
    }
    catch(const th::Error&){
        caught=true;
    }
    REQUIRE(caught);
}

int main(){
    try{
        using namespace th;

        REQUIRE(ws::accept_key("dGhlIHNhbXBsZSBub25jZQ==")=="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

        REQUIRE(ws::base64(ws::sha1("abc"))=="qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");

        REQUIRE(ws::base64(ws::sha1(""))=="2jmj7l5rSw0yVb/vlWAYkK/YBwk=");

        REQUIRE(valid_utf8("日本語 🦊"));
        REQUIRE(!valid_utf8("\xc0\x80"));
        REQUIRE(!valid_utf8("\xed\xa0\x80"));
        REQUIRE(!valid_utf8("\xf4\x90\x80\x80"));

        Spec s;
        s.name="dialogue";
        s.module="th-demo.exe";
        s.symbol="th_demo_text";
        s.deref={
            16,-8
        };
        s.add=-4;

        auto wire=encode_specs({
            s
        });
        auto decoded=decode_specs(wire);
        REQUIRE(decoded.size()==1);
        REQUIRE(decoded[0].deref[1]==-8);
        REQUIRE(decoded[0].add==-4);

        for(std::size_t i=0;i<wire.size();++i)fails([&]{
            decode_specs(std::span<const std::uint8_t>(wire).first(i));
        });

        wire.push_back(0);
        fails([&]{
            decode_specs(wire);
        });

        const auto clean_config=encode_specs({s});
        REQUIRE(profile_tag("engine-a",clean_config)!=profile_tag("engine-b",clean_config));
        auto query=decode_catalog_query(encode_catalog_query({Architecture::x86,"SiglusEngine.exe"}));
        REQUIRE(query.arch==Architecture::x86&&query.process=="SiglusEngine.exe");
        std::vector<CatalogItem> catalog{
            {"siglusengine4-x86","SiglusEngine4",profile_tag("siglusengine4-x86",clean_config),ProcessPriority::exact},
            {"generic","Generic",profile_tag("generic",clean_config),ProcessPriority::generic}
        };
        auto decoded_catalog=decode_catalog(encode_catalog(catalog));
        REQUIRE(decoded_catalog.size()==2);
        REQUIRE(decoded_catalog[0].id=="siglusengine4-x86");
        REQUIRE(decoded_catalog[0].priority==ProcessPriority::exact);
        REQUIRE(decode_config_request(encode_config_request(decoded_catalog[0].tag))==decoded_catalog[0].tag);
        fails([&]{
            encode_specs({
                s,s
            });
            decode_specs(encode_specs({
                s,s
            }));
        });

        auto p=parse_pattern("48 8b ?? 00");
        const std::array<std::uint8_t,4>b{
            0x48,0x8b,0xff,0
        };
        REQUIRE(pattern_at(b,p));
        fails([]{
            parse_pattern("?? ?");
        });
        fails([]{
            parse_pattern("4G");
        });

        Event e{
            1,3,123,456,"日本語\nhello"
        };
        auto eb=encode_event(e);
        auto er=decode_event(std::span<const std::uint8_t>(eb).subspan(4));
        REQUIRE(er.text==e.text&&er.pid==123&&er.hook==3);

        std::string token(64,'a');
        auto req="GET /?token="+token+" HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive, Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: https://aurorawright.github.io\r\n\r\n";

        REQUIRE(ws::upgrade(req,token,{
            "https://aurorawright.github.io"
        }).find("101")!=std::string::npos);
        fails([&]{
            ws::upgrade(req,token,{});
        });
        fails([&]{
            ws::upgrade(req,std::string(64,'b'),{
                "https://aurorawright.github.io"
            });
        });

        const Bytes ping{
            0x89,0x82,1,2,3,4,static_cast<std::uint8_t>('o'^1),static_cast<std::uint8_t>('k'^2)
        };

        for(std::size_t i=0;i<ping.size();++i)REQUIRE(!ws::control(std::span<const std::uint8_t>(ping).first(i)).complete);

        auto c=ws::control(ping);
        REQUIRE(c.complete&&c.opcode==9&&std::string(c.payload.begin(),c.payload.end())=="ok");

        fails([]{
            ws::control(Bytes{
                0x89,0x00
            });
        });
        fails([]{
            ws::control(Bytes{
                0x81,0x80
            });
        });

        auto gateway=make_gateway(1,2,3);
        REQUIRE(gateway.code.size()<1024);
        REQUIRE(gateway.code[gateway.unwind_offset]==1);

        std::cout<<"protocol, UTF-8, SHA-1/RFC6455, configuration, pattern and gateway layout checks passed\n";

        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
