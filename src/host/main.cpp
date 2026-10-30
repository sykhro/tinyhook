#include "script.hpp"
#include "log.hpp"
#include <spdlog/spdlog.h>
#include "th/websocket.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
    using namespace th;

    using Clock=std::chrono::steady_clock;

    volatile std::sig_atomic_t running=1;

    void stop(int){
        running=0;
    }

    struct Fd {
        int n=-1;

        explicit Fd(int x=-1):n(x){}
        ~Fd(){
            if(n>=0)::close(n);
        }

        Fd(const Fd&)=delete;
        Fd& operator=(const Fd&)=delete;

        Fd(Fd&& x) noexcept:n(x.n){
            x.n=-1;
        }

        Fd& operator=(Fd&& x)noexcept{
            if(this!=&x){
                if(n>=0)::close(n);
                n=x.n;
                x.n=-1;
            }
            return *this;
        }
    };

    void nonblock(int fd) {
        int flags=fcntl(fd,F_GETFL,0);

        if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0||fcntl(fd,F_SETFD,FD_CLOEXEC)<0)throw Error("fcntl failed");
    }

    Fd listener(std::uint16_t port) {
        Fd fd(socket(AF_INET,SOCK_STREAM,0));
        if(fd.n<0)throw Error("socket failed");

        int one=1;
        setsockopt(fd.n,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));

        sockaddr_in a{};
        a.sin_family=AF_INET;
        a.sin_port=htons(port);
        a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

        if(bind(fd.n,reinterpret_cast<sockaddr*>(&a),sizeof(a))||listen(fd.n,16))throw Error("cannot bind 127.0.0.1:"+std::to_string(port)+": "+strerror(errno));

        nonblock(fd.n);
        return fd;
    }

    std::string random_token() {
        Fd fd(open("/dev/urandom",O_RDONLY|O_CLOEXEC));
        if(fd.n<0)throw Error("cannot open OS random source");

        std::array<std::uint8_t,32> bytes{};
        std::size_t p=0;

        while(p<bytes.size()){
            auto n=read(fd.n,bytes.data()+p,bytes.size()-p);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw Error("OS random read failed");
            p+=static_cast<std::size_t>(n);
        }

        std::string out;
        for(auto x:bytes){
            out+="0123456789abcdef"[x>>4];
            out+="0123456789abcdef"[x&15];
        }
        return out;
    }

    std::string token_file(const std::string& path) {
        Fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));

        if(fd.n>=0) {
            struct stat st{};
            if(fstat(fd.n,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))throw Error("token file must be owned by you and have mode 600");

            char data[67]{};
            auto n=read(fd.n,data,sizeof(data));
            if(n<0)throw Error("token read failed");

            std::string token(data,static_cast<std::size_t>(n));
            while(!token.empty()&&(token.back()=='\n'||token.back()=='\r'))token.pop_back();

            if(!valid_token(token))throw Error("invalid token file");
            return token;
        }

        if(errno!=ENOENT)throw Error("cannot open token file");

        auto token=random_token();
        Fd out(open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));

        if(out.n<0)throw Error("cannot create token file");

        auto line=token+"\n";
        std::size_t p=0;

        while(p<line.size()){
            auto n=write(out.n,line.data()+p,line.size()-p);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw Error("cannot write token file");
            p+=static_cast<std::size_t>(n);
        }
        return token;
    }

    std::uint16_t port_number(std::string_view s) {
        unsigned n=0;
        if(s.empty()||s.size()>5)throw Error("invalid port");
        for(char c:s){
            if(c<'0'||c>'9')throw Error("invalid port");
            n=n*10+unsigned(c-'0');
        }
        if(n<1||n>65535)throw Error("port must be 1..65535");
        return static_cast<std::uint16_t>(n);
    }

    struct Options {
        std::uint16_t ws=default_ws_port,agent=default_agent_port;

        std::string script,script_dir,token,path=".tinyhook-token";

        bool stdin_enabled=false,stdout_enabled=false,check=false;

        std::vector<std::string> origins{
            "https://aurorawright.github.io","https://renji-xd.github.io"
        };
    };

    Options options(int argc,char**argv) {
        Options o;

        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            auto value=[&](){
                if(++i>=argc)throw Error("missing value for "+a);
                return std::string(argv[i]);
            };

            if(a=="--script")o.script=value();
            else if(a=="--script-dir")o.script_dir=value();
            else if(a=="--token")o.token=value();
            else if(a=="--token-file")o.path=value();

            else if(a=="--ws-port")o.ws=port_number(value());
            else if(a=="--agent-port")o.agent=port_number(value());

            else if(a=="--allow-origin")o.origins.push_back(value());
            else if(a=="--stdin")o.stdin_enabled=true;
            else if(a=="--stdout")o.stdout_enabled=true;
            else if(a=="--check")o.check=true;

            else if(a=="--help") {
                std::cout<<"th-host [--script FILE | --script-dir DIR] [--token-file FILE] [--ws-port 9001]\n"
                "        [--agent-port 9002] [--stdin] [--stdout] [--check]\n"
                "        [--allow-origin ORIGIN] [--token 64_HEX_CHARS]\n"
                "--script keeps single-profile behavior. --script-dir exposes an engine catalog for auto detection.\n"
                "A compiler-enabled host reads .luau (preferred) and .luauc; VM-only builds read .luauc.\n";
                std::exit(0);
            }

            else throw Error("unknown option: "+a);
        }

        if(!o.script.empty()&&!o.script_dir.empty())throw Error("--script and --script-dir are mutually exclusive");
        if(o.ws==o.agent)throw Error("WebSocket and agent ports must differ");
        return o;
    }

    struct Profile {
        std::unique_ptr<Script> script;
        Bytes config;
        std::uint64_t tag{};
        std::string source;
    };

    std::vector<std::filesystem::path> directory_scripts(const std::string& directory) {
        namespace fs=std::filesystem;
        fs::path root(directory);
        if(!fs::is_directory(root))throw Error("script directory does not exist: "+directory);

        // A compiler-enabled developer host prefers source over bytecode with the
        // same stem. VM-only hosts deliberately see only precompiled bytecode.
        std::map<std::string,std::pair<int,fs::path>> chosen;
        for(const auto& entry:fs::directory_iterator(root)){
            if(!entry.is_regular_file())continue;
            auto ext=entry.path().extension().string();
            int rank=0;
            if(ext==".luauc")rank=1;
#if TH_WITH_COMPILER
            else if(ext==".luau")rank=2;
#endif
            else continue;
            auto key=entry.path().stem().string();
            auto it=chosen.find(key);
            if(it==chosen.end()||rank>it->second.first)chosen[key]={rank,entry.path()};
        }
        std::vector<fs::path> result;
        result.reserve(chosen.size());
        for(auto& [_,item]:chosen)result.push_back(std::move(item.second));
        if(result.size()>max_profiles)throw Error("script directory contains too many engine profiles");
        return result;
    }

    enum class State {
        http,auth,catalog,config_request,producer,viewer,closing
    };

    struct Peer {
        Fd fd;
        State state;
        Bytes input,output;
        std::size_t sent{};
        bool dead=false;
        Profile* profile{};

        Clock::time_point since=Clock::now();

        Peer(int n,State s):fd(n),state(s){}
        void queue(std::span<const std::uint8_t> bytes) {
            if(dead)return;

            if(output.size()-sent+bytes.size()>256*1024){
                dead=true;
                spdlog::warn("slow peer disconnected (output buffer limit)");
                return;
            }

            if(sent){
                output.erase(output.begin(),output.begin()+static_cast<std::ptrdiff_t>(sent));
                sent=0;
            }

            output.insert(output.end(),bytes.begin(),bytes.end());
        }

        void queue(std::string_view s){
            queue({
                reinterpret_cast<const std::uint8_t*>(s.data()),s.size()
            });
        }

        void close_after_flush(){
            state=State::closing;
            since=Clock::now();
            input.clear();
        }

        void consume(std::size_t n){
            input.erase(input.begin(),input.begin()+static_cast<std::ptrdiff_t>(n));
        }
    };

    struct Server {
        Options opts;
        std::vector<Profile> profiles;
        Bytes empty_config=encode_specs({});
        std::vector<std::unique_ptr<Peer>> peers;

        std::string stdin_buffer;
        int stdin_flags=-1;

        explicit Server(Options o):opts(std::move(o)){
            std::vector<std::filesystem::path> files;
            if(!opts.script.empty())files.emplace_back(opts.script);
            else if(!opts.script_dir.empty())files=directory_scripts(opts.script_dir);

            for(const auto& file:files){
                Profile profile;
                profile.source=file.string();
                profile.script=std::make_unique<Script>(profile.source);
                profile.config=encode_specs(profile.script->specs());
                profile.tag=profile_tag(profile.script->id(),profile.config);
                for(const auto& old:profiles){
                    if(old.script->id()==profile.script->id())throw Error("duplicate profile id: "+profile.script->id());
                    if(old.tag==profile.tag)throw Error("engine profile identity collision");
                }
                profiles.push_back(std::move(profile));
            }
        }

        ~Server(){
            if(stdin_flags>=0)fcntl(STDIN_FILENO,F_SETFL,stdin_flags);
        }

        Profile* find_profile(std::uint64_t tag) {
            for(auto& profile:profiles)if(profile.tag==tag)return &profile;
            return nullptr;
        }

        std::vector<CatalogItem> catalog(const CatalogQuery& query) {
            std::vector<CatalogItem> items;
            for(auto& profile:profiles){
                if(!profile.script->supports(query.arch))continue;
                items.push_back({
                    profile.script->id(),profile.script->engine(),profile.tag,
                    profile.script->process_priority(query.process)
                });
            }
            std::sort(items.begin(),items.end(),[](const CatalogItem& a,const CatalogItem& b){
                if(a.priority!=b.priority)return a.priority>b.priority;
                return a.id<b.id;
            });
            return items;
        }

        Script* stdin_script() {
            return profiles.size()==1?profiles.front().script.get():nullptr;
        }

        void publish(const Event& e,Script* script) {
            if(e.kind!=1){
                if(e.kind==2) spdlog::info("agent[{}] {}",e.pid,e.text);
                else spdlog::warn("agent[{}] {}",e.pid,e.text);
                return;
            }

            if(!valid_utf8(e.text))throw Error("invalid UTF-8 event");
            spdlog::trace("capture pid={} tid={} hook={}: {}",e.pid,e.tid,e.hook,e.text);

            std::optional<std::string> result=e.text;
            if(script){
                if(!script->specs().empty()&&e.hook>=script->specs().size())throw Error("unknown hook id");
                result=script->apply(e);
            }
            if(!result||result->empty()){
                spdlog::trace("filtered pid={} tid={} hook={}",e.pid,e.tid,e.hook);
                return;
            }

            if(result->size()>max_frame||!valid_utf8(*result)){
                spdlog::warn("invalid/oversize script output dropped");
                return;
            }

            auto bytes=ws::text_frame(*result);

            for(auto& p:peers)if(p->state==State::viewer)p->queue(bytes);

            if(opts.stdout_enabled)std::cout<<*result<<std::endl;
        }

        bool framed_input(Peer& p,std::span<const std::uint8_t>& payload) {
            if(p.input.size()<4)return false;
            Reader r(p.input);
            auto n=r.get32();
            if(n>max_frame)throw Error("agent request frame too large");
            if(p.input.size()<4+n)return false;
            payload=std::span<const std::uint8_t>(p.input).subspan(4,n);
            return true;
        }

        void process(Peer& p) {
            if(p.dead)return;

            try {
                if(p.state==State::http) {
                    std::string_view request(reinterpret_cast<const char*>(p.input.data()),p.input.size());
                    auto end=request.find("\r\n\r\n");

                    if(end==request.npos){
                        if(request.size()>8192)throw Error("HTTP header too large");
                        return;
                    }

                    if(end+4>8192)throw Error("HTTP header too large");

                    p.queue(ws::upgrade(request.substr(0,end+4),opts.token,opts.origins));
                    p.consume(end+4);
                    p.state=State::viewer;
                    spdlog::info("WebSocket viewer authenticated");
                }

                if(p.state==State::auth) {
                    if(p.input.size()<hello_size)return;

                    if(std::string_view(reinterpret_cast<const char*>(p.input.data()),4)!="THK2"||!equal_token(std::string_view(reinterpret_cast<const char*>(p.input.data()+5),token_size),opts.token))throw Error("agent authentication failed");

                    auto role=p.input[4];

                    if(role==2){
                        if(p.input.size()<hello_size+8)return;
                        Reader encoded_tag(std::span<const std::uint8_t>(p.input).subspan(hello_size,8));
                        auto tag=encoded_tag.number(8);
                        p.profile=find_profile(tag);
                        // Single-profile producers may send a configuration-only tag.
                        if(!p.profile&&profiles.size()==1&&tag==config_tag(profiles.front().config))p.profile=&profiles.front();
                        if(!p.profile&&profiles.empty()&&tag==config_tag(empty_config)){
                            // Pass-through integration/diagnostic producer.
                        }
                        else if(!p.profile)throw Error("capture profile mismatch; restart target after changing engine profiles");
                        p.consume(hello_size+8);
                        spdlog::info("capture producer authenticated{}",p.profile?" for "+p.profile->script->id():std::string{});
                        p.queue("OKAY");
                        p.state=State::producer;
                    }
                    else {
                        p.consume(hello_size);
                        if(role==1){
                            const auto& config=profiles.size()==1?profiles.front().config:empty_config;
                            if(profiles.size()>1)throw Error("direct configuration request is ambiguous; use catalog detection");
                            spdlog::info("legacy config client authenticated; sending {} byte configuration",config.size());
                            p.queue(framed(config));
                            p.close_after_flush();
                            return;
                        }
                        if(role==3){
                            p.state=State::catalog;
                            p.since=Clock::now();
                        }
                        else if(role==4){
                            p.state=State::config_request;
                            p.since=Clock::now();
                        }
                        else throw Error("unknown agent role");
                    }
                }

                for(unsigned budget=0;budget<32 && !p.input.empty();++budget) {
                    if(p.state==State::catalog) {
                        std::span<const std::uint8_t> payload;
                        if(!framed_input(p,payload))break;
                        auto query=decode_catalog_query(payload);
                        p.consume(4+payload.size());
                        auto items=catalog(query);
                        spdlog::info("catalog query for {} ({}) -> {} candidate(s)",query.process,query.arch==Architecture::x86?"x86":"x64",items.size());
                        p.queue(framed(encode_catalog(items)));
                        p.close_after_flush();
                        return;
                    }
                    if(p.state==State::config_request) {
                        std::span<const std::uint8_t> payload;
                        if(!framed_input(p,payload))break;
                        auto tag=decode_config_request(payload);
                        p.consume(4+payload.size());
                        auto* profile=find_profile(tag);
                        if(!profile)throw Error("unknown engine profile");
                        spdlog::debug("sending profile {} configuration ({} bytes)",profile->script->id(),profile->config.size());
                        p.queue(framed(profile->config));
                        p.close_after_flush();
                        return;
                    }
                    if(p.state==State::producer) {
                        if(p.input.size()<4)break;
                        Reader r(p.input);
                        auto n=r.get32();
                        if(n>max_frame||n<17)throw Error("invalid agent frame length");

                        if(p.input.size()<4+n)break;
                        auto event=decode_event(std::span<const std::uint8_t>(p.input).subspan(4,n));
                        p.consume(4+n);
                        publish(event,p.profile?p.profile->script.get():nullptr);
                    }
                    else if(p.state==State::viewer) {
                        auto c=ws::control(p.input);
                        if(!c.complete)break;
                        p.consume(c.consumed);

                        if(c.opcode==9)p.queue(ws::frame(10,c.payload));

                        if(c.opcode==8){
                            p.queue(ws::frame(8,c.payload));
                            p.close_after_flush();
                            break;
                        }
                    }
                    else break;
                }
            }
            catch(const std::exception& e) {
                spdlog::warn("peer rejected: {}",e.what());

                if(p.state==State::http){
                    p.queue("HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    p.close_after_flush();
                }

                else if(p.state==State::viewer){
                    const std::array<std::uint8_t,2> code{
                        3,234
                    };
                    p.queue(ws::frame(8,code));
                    p.close_after_flush();
                }

                else p.dead=true;
            }
        }

        void stdin_read() {
            char b[4096];
            auto n=read(STDIN_FILENO,b,sizeof(b));

            if(n==0){
                if(!stdin_buffer.empty()){
                    publish(Event{
                        1,0,0,0,stdin_buffer
                    },stdin_script());
                    stdin_buffer.clear();
                }
                opts.stdin_enabled=false;
                return;
            }

            if(n<0){
                if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)opts.stdin_enabled=false;
                return;
            }

            stdin_buffer.append(b,static_cast<std::size_t>(n));

            for(;;){
                auto pos=stdin_buffer.find('\n');
                if(pos==std::string::npos)break;
                std::string line=stdin_buffer.substr(0,pos);
                stdin_buffer.erase(0,pos+1);
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line.size()<=max_frame&&valid_utf8(line))publish(Event{
                    1,0,0,0,std::move(line)
                },stdin_script());
            }

            if(stdin_buffer.size()>max_frame){
                stdin_buffer.clear();
                spdlog::warn("stdin line exceeds limit; stdin disabled");
                opts.stdin_enabled=false;
            }
        }

        void run() {
            if(opts.check){
                std::size_t hooks=0;
                for(const auto& profile:profiles){
                    hooks+=profile.script->specs().size();
                    std::cout<<profile.script->id()<<"\t"<<profile.script->engine()<<"\t"<<profile.script->specs().size()<<" hook(s)\n";
                }
                std::cout<<profiles.size()<<" valid engine profile(s), "<<hooks<<" hook definition(s)\n";
                return;
            }

            if(opts.token.empty())opts.token=token_file(opts.path);
            if(!valid_token(opts.token))throw Error("invalid token");

            auto w=listener(opts.ws),a=listener(opts.agent);

            std::size_t hooks=0;
            for(const auto& profile:profiles)hooks+=profile.script->specs().size();
            spdlog::info("TinyHook (experimental), {} engine profile(s), {} hook definition(s)",profiles.size(),hooks);
            for(const auto& profile:profiles)spdlog::debug("profile {}: {} from {}",profile.script->id(),profile.script->engine(),profile.source);
            spdlog::info("WebSocket URL: ws://127.0.0.1:{}/?token={}",opts.ws,opts.token);
            spdlog::info("Capture/config: 127.0.0.1:{} (authenticated)",opts.agent);

            if(opts.stdin_enabled){
                stdin_flags=fcntl(STDIN_FILENO,F_GETFL,0);
                if(stdin_flags<0||fcntl(STDIN_FILENO,F_SETFL,stdin_flags|O_NONBLOCK)<0)throw Error("cannot make stdin nonblocking");
            }

            while(running) {
                for(auto& p:peers)process(*p);

                std::vector<pollfd> fds{
                    {
                        w.n,POLLIN,0
                    },{
                        a.n,POLLIN,0
                    },{
                        opts.stdin_enabled?STDIN_FILENO:-1,POLLIN,0
                    }
                };

                auto old_count=peers.size();

                for(auto& p:peers){
                    short events=p->state==State::closing?0:POLLIN;
                    if(p->output.size()>p->sent)events|=POLLOUT;
                    fds.push_back({
                        p->dead?-1:p->fd.n,events,0
                    });
                }

                int status=poll(fds.data(),static_cast<nfds_t>(fds.size()),25);
                if(status<0){
                    if(errno==EINTR)continue;
                    throw Error("poll failed");
                }

                if(fds[2].revents&(POLLIN|POLLHUP))stdin_read();

                for(std::size_t i=0;i<old_count;++i) {
                    auto& p=*peers[i];
                    auto re=fds[i+3].revents;
                    if(p.dead)continue;

                    if(re&(POLLERR|POLLNVAL)){
                        p.dead=true;
                        continue;
                    }

                    if(re&POLLIN) {
                        std::uint8_t b[8192];
                        auto n=recv(p.fd.n,b,sizeof(b),0);

                        if(n>0){
                            p.input.insert(p.input.end(),b,b+n);
                            if(p.input.size()>max_frame*2+4)p.dead=true;
                            else process(p);
                        }

                        else if(n==0)p.dead=true;
                        else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)p.dead=true;
                    }

                    if(!p.dead&&(re&POLLOUT)&&p.sent<p.output.size()) {
                        auto n=send(p.fd.n,p.output.data()+p.sent,p.output.size()-p.sent,0);

                        if(n>0)p.sent+=static_cast<std::size_t>(n);
                        else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)p.dead=true;

                        if(p.sent==p.output.size()){
                            p.output.clear();
                            p.sent=0;
                        }
                    }

                    if((re&POLLHUP)&&!(re&POLLIN))p.dead=true;

                    auto age=Clock::now()-p.since;

                    if((p.state==State::http||p.state==State::auth||p.state==State::catalog||p.state==State::config_request)&&age>std::chrono::seconds(5))p.dead=true;

                    if(p.state==State::closing&&(p.output.empty()||age>std::chrono::seconds(2)))p.dead=true;
                }

                peers.erase(std::remove_if(peers.begin(),peers.end(),[](auto& p){
                    return p->dead;
                }),peers.end());

                for(unsigned i=0;i<2;++i)if(fds[i].revents&POLLIN) {
                    for(unsigned budget=0;budget<8;++budget){
                        int fd=accept(fds[i].fd,nullptr,nullptr);
                        if(fd<0)break;
                        if(peers.size()>=32){
                            close(fd);
                            continue;
                        }
                        try{
                            nonblock(fd);
                            peers.push_back(std::make_unique<Peer>(fd,i==0?State::http:State::auth));
                        }
                        catch(...){
                            close(fd);
                            throw;
                        }
                    }
                }
            }
        }
    };
}

int main(int argc,char**argv) {
    th::log::configure_console("th-host");
    std::signal(SIGPIPE,SIG_IGN);
    std::signal(SIGINT,stop);
    std::signal(SIGTERM,stop);

    try {
        Server server(options(argc,argv));
        server.run();
        return 0;
    }
    catch(const std::exception& e){
        spdlog::error("{}",e.what());
        return 1;
    }
}
