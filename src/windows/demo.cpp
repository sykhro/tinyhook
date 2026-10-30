#include "win.hpp"
#include "log.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>

// Deliberate exported function-entry fixture. Not a hook for any commercial game.
extern "C" __declspec(dllexport) __declspec(noinline) void th_demo_text(const wchar_t* text) {
    static volatile LONG calls=0;
    InterlockedIncrement(&calls);

    if(text&&*text)spdlog::info("fixture emitted a UTF-16 line (call {})",calls);
}

int main() {
    th::log::configure_console("th-demo");
    SetConsoleOutputCP(CP_UTF8);

    spdlog::info("TinyHook fixture. Windows PID: {}",GetCurrentProcessId());

    spdlog::info("Leave running; attach with th-client using scripts/demo.luau (x64) or scripts/demo-x86.luau (x86). Ctrl+C exits.");

    HWND window=CreateWindowExW(0,L"STATIC",L"TinyHook fixture 日本語",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,420,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 1;

    const wchar_t* lines[]={
        L"これはテストです。",L"日本語の文章を取得しました。",L"同じ文章です。",L"同じ文章です。",L"次の文章へ進みましょう。"
    };

    unsigned i=0;
    auto next=GetTickCount64();
    while(IsWindow(window)){
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT)return 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        auto now=GetTickCount64();
        if(now>=next){th_demo_text(lines[i++%5]);next=now+1500;}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,static_cast<DWORD>(next-now),QS_ALLINPUT);
    }
    return 0;
}
