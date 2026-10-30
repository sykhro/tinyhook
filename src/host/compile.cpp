#include "th/protocol.hpp"
#include "log.hpp"
#include <spdlog/spdlog.h>
#include <Luau/Compiler.h>
#include <fstream>

int main(int argc,char**argv) {
    th::log::configure_console("th-compile");
    try {
        if(argc!=3){
            spdlog::error("Usage: th-compile INPUT.luau OUTPUT.luauc");
            return 1;
        }

        std::ifstream in(argv[1],std::ios::binary);
        if(!in)throw th::Error("cannot read input");

        in.seekg(0,std::ios::end);
        auto n=in.tellg();
        if(n<0||n>256*1024)throw th::Error("source exceeds 256 KiB");
        in.seekg(0);

        std::string source(static_cast<std::size_t>(n),'\0');
        if(!in.read(source.data(),n))throw th::Error("source read failed");

        Luau::CompileOptions opts;
        opts.optimizationLevel=2;
        opts.debugLevel=1;

        auto bytes=Luau::compile(source,opts);
        if(bytes.empty()||bytes.front()==0)throw th::Error(bytes.empty()?"compiler returned no bytecode":bytes.substr(1));

        std::ofstream out(argv[2],std::ios::binary|std::ios::trunc);
        if(!out)throw th::Error("cannot open output");

        out<<"THBC1:luau-0.740\n";
        out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        out.close();
        if(!out)throw th::Error("output write failed");
        return 0;
    }
    catch(const std::exception& e){
        spdlog::error("{}",e.what());
        return 1;
    }
}
