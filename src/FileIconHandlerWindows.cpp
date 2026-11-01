// SPDX-License-Identifier: GPL-3.0-or-later
// Tiny Explorer extension: it selects an embedded icon by suffix. It never
// opens the document, starts Pad, reads settings, or links against Qt.
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <new>
#include <iterator>
#include "FileIconHandlerWindows.h"
#include "FileTypeIconMapWindows.h"

namespace {
HMODULE moduleHandle = nullptr;
volatile LONG objects = 0;
volatile LONG serverLocks = 0;

int resourceForFile(LPCOLESTR file, size_t length)
{
    const wchar_t *suffix = nullptr;
    for (size_t i = 0; i < length; ++i) {
        if (file[i] == L'/' || file[i] == L'\\') suffix = nullptr;
        else if (file[i] == L'.') suffix = file + i + 1;
    }
    if (!suffix || !*suffix) return FileTypeIconMapWindows::GenericResourceId;
    for (const auto &entry : FileTypeIconMapWindows::Extensions) {
        const wchar_t *actual = suffix;
        const wchar_t *expected = entry.suffix;
        while (*actual && *expected) {
            const wchar_t lower = *actual >= L'A' && *actual <= L'Z' ? *actual + (L'a' - L'A') : *actual;
            if (lower != *expected) break;
            ++actual;
            ++expected;
        }
        if (!*actual && !*expected) return entry.resourceId;
    }
    return FileTypeIconMapWindows::GenericResourceId;
}

class IconHandler final : public IPersistFile, public IExtractIconW
{
public:
    IconHandler() { InterlockedIncrement(&objects); }
    ~IconHandler() { InterlockedDecrement(&objects); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid == IID_IUnknown || iid == IID_IPersist || iid == IID_IPersistFile)
            *output = static_cast<IPersistFile *>(this);
        else if (iid == IID_IExtractIconW)
            *output = static_cast<IExtractIconW *>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&references)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG remaining = InterlockedDecrement(&references);
        if (!remaining) delete this;
        return ULONG(remaining);
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID *id) override
    {
        if (!id) return E_POINTER;
        *id = FileIconHandlerWindows::ClassId;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Load(LPCOLESTR path, DWORD) override
    {
        resourceId = FileTypeIconMapWindows::GenericResourceId;
        if (!path) return E_POINTER;
        size_t length = 0;
        while (length < 32768 && path[length]) ++length;
        if (!length || length == 32768) return E_INVALIDARG;
        resourceId = resourceForFile(path, length);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Save(LPCOLESTR, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SaveCompleted(LPCOLESTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurFile(LPOLESTR *path) override
    {
        if (!path) return E_POINTER;
        *path = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIconLocation(UINT, LPWSTR output, UINT capacity,
        int *index, UINT *flags) override
    {
        if (output && capacity) output[0] = L'\0';
        if (!output || !index || !flags) return E_POINTER;
        *index = 0;
        *flags = 0;
        if (!capacity) return E_INVALIDARG;
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(moduleHandle, executable, DWORD(std::size(executable)));
        if (!length || length >= std::size(executable)) return S_FALSE;
        size_t directoryLength = length;
        while (directoryLength && executable[directoryLength - 1] != L'\\' && executable[directoryLength - 1] != L'/')
            --directoryLength;
        if (!directoryLength) return S_FALSE;
        constexpr wchar_t name[] = L"8bitCode.exe";
        constexpr size_t nameLength = std::size(name) - 1;
        const size_t resultLength = directoryLength + nameLength;
        if (resultLength + 1 > std::size(executable)) return S_FALSE;
        if (resultLength + 1 > capacity) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        for (size_t i = 0; i < directoryLength; ++i) output[i] = executable[i];
        for (size_t i = 0; i <= nameLength; ++i) output[directoryLength + i] = name[i];
        *index = -resourceId;
        // Do not use GIL_PERCLASS: Applications\8bitCode.exe may handle many
        // different extensions. The returned EXE/resource pair is cacheable.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Extract(LPCWSTR, UINT, HICON *largeIcon, HICON *smallIcon, UINT) override
    {
        if (largeIcon) *largeIcon = nullptr;
        if (smallIcon) *smallIcon = nullptr;
        return S_FALSE; // Windows extracts the returned resource from the EXE.
    }
private:
    volatile LONG references = 1;
    int resourceId = FileTypeIconMapWindows::GenericResourceId;
};

class ClassFactory final : public IClassFactory
{
public:
    ClassFactory() { InterlockedIncrement(&objects); }
    ~ClassFactory() { InterlockedDecrement(&objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *output = static_cast<IClassFactory *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&references)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG remaining = InterlockedDecrement(&references);
        if (!remaining) delete this;
        return ULONG(remaining);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto *object = new (std::nothrow) IconHandler;
        if (!object) return E_OUTOFMEMORY;
        const HRESULT result = object->QueryInterface(iid, output);
        object->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&serverLocks);
        else {
            LONG current = InterlockedCompareExchange(&serverLocks, 0, 0);
            do {
                if (current <= 0) return E_UNEXPECTED;
                const LONG observed = InterlockedCompareExchange(&serverLocks, current - 1, current);
                if (observed == current) break;
                current = observed;
            } while (true);
        }
        return S_OK;
    }
private:
    volatile LONG references = 1;
};
}

extern "C" HRESULT STDMETHODCALLTYPE DllGetClassObject(REFCLSID clsid, REFIID iid, void **output)
{
    if (!output) return E_POINTER;
    *output = nullptr;
    if (clsid != FileIconHandlerWindows::ClassId) return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new (std::nothrow) ClassFactory;
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT result = factory->QueryInterface(iid, output);
    factory->Release();
    return result;
}

extern "C" HRESULT STDMETHODCALLTYPE DllCanUnloadNow()
{
    return !InterlockedCompareExchange(&objects, 0, 0) && !InterlockedCompareExchange(&serverLocks, 0, 0)
        ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) moduleHandle = instance;
    return TRUE;
}
