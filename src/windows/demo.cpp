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

    const wchar_t* lines[]={
        L"これはテストです。",L"日本語の文章を取得しました。",L"同じ文章です。",L"同じ文章です。",L"次の文章へ進みましょう。"
    };

    for(unsigned i=0;;++i){
        th_demo_text(lines[i%5]);
        Sleep(1500);
    }
}
