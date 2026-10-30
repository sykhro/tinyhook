#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include "th/protocol.hpp"
#include <algorithm>
#include <cstring>

namespace th::win {
    struct Handle {
        HANDLE h{};
        explicit Handle(HANDLE value=nullptr):h(value){}
        ~Handle(){
            if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        }

        Handle(const Handle&)=delete;
        Handle& operator=(const Handle&)=delete;

        Handle(Handle&& x)noexcept:h(x.h){
            x.h=nullptr;
        }

        explicit operator bool()const{
            return h&&h!=INVALID_HANDLE_VALUE;
        }
    };

    struct Wsa {
        Wsa(){
            WSADATA data{};
            if(WSAStartup(MAKEWORD(2,2),&data))throw Error("WSAStartup failed");
        }

        ~Wsa(){
            WSACleanup();
        }

        Wsa(const Wsa&)=delete;
        Wsa&operator=(const Wsa&)=delete;
    };

    struct Socket {
        SOCKET s=INVALID_SOCKET;
        explicit Socket(SOCKET value=INVALID_SOCKET):s(value){}
        ~Socket(){
            if(s!=INVALID_SOCKET)closesocket(s);
        }

        Socket(const Socket&)=delete;
        Socket&operator=(const Socket&)=delete;

        Socket(Socket&& x)noexcept:s(x.s){
            x.s=INVALID_SOCKET;
        }
    };

    inline Socket connect_local(std::uint16_t port) {
        Socket s(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        if(s.s==INVALID_SOCKET)throw Error("socket failed");

        DWORD timeout=2000;
        setsockopt(s.s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        setsockopt(s.s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));

        sockaddr_in addr{};
        addr.sin_family=AF_INET;
        addr.sin_port=htons(port);
        addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

        if(connect(s.s,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)))throw Error("cannot connect to host on 127.0.0.1:"+std::to_string(port));
        return s;
    }

    inline void send_all(SOCKET s,std::span<const std::uint8_t> b) {
        while(!b.empty()){
            int n=send(s,reinterpret_cast<const char*>(b.data()),static_cast<int>(b.size()),0);
            if(n<=0)throw Error("capture connection closed/send timed out");
            b=b.subspan(static_cast<std::size_t>(n));
        }
    }

    inline void recv_all(SOCKET s,std::span<std::uint8_t> b) {
        while(!b.empty()){
            int n=recv(s,reinterpret_cast<char*>(b.data()),static_cast<int>(b.size()),0);
            if(n<=0)throw Error("capture connection closed/receive timed out");
            b=b.subspan(static_cast<std::size_t>(n));
        }
    }

    inline Bytes recv_frame(SOCKET s) {
        std::array<std::uint8_t,4> header{};
        recv_all(s,header);
        Reader r(header);
        auto n=r.get32();
        if(n>max_frame)throw Error("frame too large");
        Bytes b(n);
        recv_all(s,b);
        return b;
    }

    inline std::wstring wide(std::string_view s) {
        if(s.empty())return {};

        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
        if(n<=0)throw Error("invalid UTF-8");

        std::wstring w(static_cast<std::size_t>(n),L'\0');
        if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n)!=n)throw Error("UTF-8 conversion failed");
        return w;
    }

    inline std::string utf8(std::wstring_view s) {
        if(s.empty())return {};

        int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
        if(n<=0)throw Error("invalid UTF-16");

        std::string b(static_cast<std::size_t>(n),'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),b.data(),n,nullptr,nullptr)!=n)throw Error("UTF-16 conversion failed");
        return b;
    }

    inline bool read_at(std::uintptr_t address,void* destination,std::size_t size)noexcept {
        SIZE_T n=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),destination,size,&n)&&n==size;
    }

    inline std::uintptr_t plus(std::uintptr_t p,std::int32_t offset)noexcept {
        if(offset>=0){
            auto n=static_cast<std::uintptr_t>(offset);
            if(p>UINTPTR_MAX-n)return 0;
            return p+n;
        }

        auto n=static_cast<std::uintptr_t>(-std::int64_t(offset));
        return p<n?0:p-n;
    }

    inline bool executable(std::uintptr_t p)noexcept {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;

        return (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
    }

    inline std::string error(const char* what){
        return std::string(what)+" (Win32 error "+std::to_string(GetLastError())+")";
    }
}
