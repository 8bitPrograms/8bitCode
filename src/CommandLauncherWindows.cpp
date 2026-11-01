// SPDX-License-Identifier: GPL-3.0-or-later
// Native short command. Forward the original argument tail without a shell.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return ERROR_BAD_PATHNAME;
    std::wstring target(module.data(), length);
    target.resize(target.find_last_of(L"\\/") + 1);
    target += L"8bitCode.exe";
    // The executable token cannot contain an embedded quote in a Windows path.
    const wchar_t *tail = GetCommandLineW();
    while (*tail == L' ' || *tail == L'\t') ++tail;
    if (*tail == L'"') {
        ++tail;
        while (*tail && *tail != L'"') ++tail;
        if (*tail) ++tail;
    } else {
        while (*tail && *tail != L' ' && *tail != L'\t') ++tail;
    }
    std::wstring command = L"\"" + target + L"\" " + tail;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    // Inherit the caller's cwd; never change it to the installation directory.
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &child)) {
        const DWORD error = GetLastError();
        const std::wstring message = L"8bitCode: could not start 8bitCode.exe (Windows error " + std::to_wstring(error) + L").\r\n";
        const HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
        DWORD written = 0;
        WriteConsoleW(stderrHandle, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
        return static_cast<int>(error);
    }
    CloseHandle(child.hThread);
    // Keep the launcher alive through GUI startup and surface early loader errors.
    // A secondary instance exits after forwarding; it does not have a GUI loop.
    WaitForInputIdle(child.hProcess, 5000);
    DWORD status = STILL_ACTIVE;
    GetExitCodeProcess(child.hProcess, &status);
    CloseHandle(child.hProcess);
    return status == STILL_ACTIVE ? 0 : static_cast<int>(status);
}
