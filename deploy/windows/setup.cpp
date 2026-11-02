#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <string>

namespace {
bool silent = false;
int failure(const wchar_t* text, DWORD code) {
    if (!silent) {
        const auto message = std::wstring(text) + L"\nWindows error: " + std::to_wstring(code);
        MessageBoxW(nullptr, message.c_str(), L"8bitCode setup", MB_OK | MB_ICONERROR);
    }
    return code ? static_cast<int>(code) : ERROR_INSTALL_FAILURE;
}
int writeMsi(const std::wstring& path) {
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) return failure(L"The embedded installer is missing.", GetLastError());
    const auto loaded = LoadResource(nullptr, resource);
    const auto data = loaded ? LockResource(loaded) : nullptr;
    const auto size = SizeofResource(nullptr, resource);
    if (!data || !size) return failure(L"Cannot read the embedded installer.", ERROR_INVALID_DATA);
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return failure(L"Cannot create the installer file.", GetLastError());
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    const auto error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) { DeleteFileW(path.c_str()); return failure(L"Cannot extract the installer.", error); }
    return ERROR_SUCCESS;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return failure(L"Cannot read setup arguments.", GetLastError());
    std::wstring options = L" /norestart";
    std::wstring extract;
    bool verifyOnly = false;
    for (int i = 1; i < count; ++i) {
        const std::wstring argument = arguments[i];
        if (argument == L"/quiet") { silent = true; options += L" /quiet"; }
        else if (argument == L"/passive") options += L" /passive";
        else if (argument == L"/norestart") {}
        else if (argument == L"--verify" && count == 2) { verifyOnly = true; silent = true; }
        else if (argument == L"--extract-msi" && count == 3 && i == 1) {
            extract = arguments[++i]; silent = true;
        } else { LocalFree(arguments); return failure(L"Unsupported setup argument.", ERROR_INVALID_PARAMETER); }
    }
    LocalFree(arguments);
    // A read-only verification path: extract without invoking Windows Installer.
    // CREATE_NEW above refuses to overwrite an existing file.
    if (!extract.empty()) return writeMsi(extract);

    wchar_t temp[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH) return failure(L"Cannot find the temporary directory.", ERROR_BAD_PATHNAME);
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) return failure(L"Cannot create a setup identifier.", ERROR_GEN_FAILURE);
    wchar_t token[40]{};
    StringFromGUID2(id, token, 40);
    const auto directory = std::wstring(temp) + L"8bitCode-setup-" + token;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Only the owner and SYSTEM can access the extracted MSI; permissions also
    // apply to its child file. The random directory must be newly created.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr))
        return failure(L"Cannot protect the setup directory.", GetLastError());
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const bool created = CreateDirectoryW(directory.c_str(), &security) != FALSE;
    const auto directoryError = GetLastError();
    LocalFree(descriptor);
    if (!created) return failure(L"Cannot create the protected setup directory.", directoryError);
    const auto msi = directory + L"\\8bitCode.msi";
    int result = writeMsi(msi);
    if (!result && !verifyOnly) {
        wchar_t system[MAX_PATH]{};
        const auto systemLength = GetSystemDirectoryW(system, MAX_PATH);
        if (!systemLength || systemLength >= MAX_PATH) result = failure(L"Cannot find Windows Installer.", ERROR_BAD_PATHNAME);
        else {
            // An absolute system executable; no PATH lookup, shell command, or
            // arbitrary MSI properties supplied by the caller.
            const auto executable = std::wstring(system) + L"\\msiexec.exe";
            auto command = L"\"" + executable + L"\" /i \"" + msi + L"\"" + options;
            STARTUPINFOW startup{sizeof(STARTUPINFOW)};
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                                nullptr, directory.c_str(), &startup, &process))
                result = failure(L"Cannot start Windows Installer.", GetLastError());
            else {
                const auto waited = WaitForSingleObject(process.hProcess, INFINITE);
                DWORD code = ERROR_INSTALL_FAILURE;
                if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &code))
                    result = failure(L"Cannot read the installer result.", GetLastError());
                else {
                    result = static_cast<int>(code);
                    if (code != ERROR_SUCCESS && code != ERROR_SUCCESS_REBOOT_REQUIRED && code != ERROR_INSTALL_USEREXIT)
                        failure(L"Windows Installer could not complete setup.", code);
                }
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
            }
        }
    }
    DeleteFileW(msi.c_str()); RemoveDirectoryW(directory.c_str());
    return result;
}
