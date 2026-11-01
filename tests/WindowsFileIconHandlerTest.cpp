// SPDX-License-Identifier: GPL-3.0-or-later
// Load the private COM DLL directly. No registration, associations or files are changed.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "FileIconHandlerWindows.h"
#include "FileTypeIcons.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QtTest>
#include <array>
#include <cstring>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <shlobj.h>

namespace {
template <typename T> struct ComPtr {
    T *value = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ~ComPtr() { if (value) value->Release(); }
    T *operator->() const { return value; }
    void **out() { return reinterpret_cast<void **>(&value); }
    void reset() { if (value) value->Release(); value = nullptr; }
};
using GetClassObject = HRESULT (WINAPI *)(REFCLSID, REFIID, void **);
using CanUnloadNow = HRESULT (WINAPI *)();
}

class WindowsFileIconHandlerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void nativeOnlyImports();
    void factoryAndObjectLifetime();
    void filenamesChooseTypeWithoutOpeningFiles();
    void buffersAndReadOnlyInterfaces();
    void cleanupTestCase();
private:
    HMODULE module = nullptr;
    GetClassObject getClassObject = nullptr;
    CanUnloadNow canUnloadNow = nullptr;
    QString executable;
};

void WindowsFileIconHandlerTest::initTestCase()
{
    const QString path = QDir::toNativeSeparators(QStringLiteral(BIT8PAD_ICON_HANDLER_DLL));
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    executable = QDir::toNativeSeparators(QFileInfo(path).absoluteDir().filePath("8bitCode.exe"));
    module = LoadLibraryExW(path.toStdWString().c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    QVERIFY2(module, qPrintable(QString("Cannot load native handler: %1").arg(GetLastError())));
    getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module, "DllGetClassObject"));
    canUnloadNow = reinterpret_cast<CanUnloadNow>(GetProcAddress(module, "DllCanUnloadNow"));
    QVERIFY(getClassObject && canUnloadNow);
    QCOMPARE(canUnloadNow(), S_OK);
}

void WindowsFileIconHandlerTest::nativeOnlyImports()
{
    // The DLL runs in Explorer, so it must not require Pad's Qt or external CRT.
    const auto *base = reinterpret_cast<const unsigned char *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    QCOMPARE(dos->e_magic, WORD(IMAGE_DOS_SIGNATURE));
    QVERIFY(dos->e_lfanew > 0 && dos->e_lfanew < 1024 * 1024);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    QCOMPARE(nt->Signature, DWORD(IMAGE_NT_SIGNATURE));
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    QVERIFY(directory.VirtualAddress < size);
    QVERIFY(quint64(directory.VirtualAddress) + directory.Size <= size);
    QSet<QByteArray> imports;
    for (DWORD offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto *entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress + offset);
        if (!entry->Name) break;
        QVERIFY(entry->Name < size);
        const char *name = reinterpret_cast<const char *>(base + entry->Name);
        const auto *end = static_cast<const char *>(std::memchr(name, 0, size - entry->Name));
        QVERIFY(end);
        const QByteArray dll = QByteArray(name, end - name).toLower();
        imports.insert(dll);
        QVERIFY2(!dll.startsWith("qt") && !dll.startsWith("msvcp") && !dll.startsWith("vcruntime") &&
            dll != "ucrtbase.dll" && !dll.startsWith("api-ms-win-crt-"), dll.constData());
    }
    QVERIFY(!imports.isEmpty());
}

void WindowsFileIconHandlerTest::factoryAndObjectLifetime()
{
    void *unsupported = reinterpret_cast<void *>(1);
    QCOMPARE(getClassObject(CLSID_NULL, IID_IClassFactory, &unsupported), CLASS_E_CLASSNOTAVAILABLE);
    QVERIFY(!unsupported);
    QCOMPARE(getClassObject(FileIconHandlerWindows::ClassId, IID_IClassFactory, nullptr), E_POINTER);

    ComPtr<IClassFactory> factory;
    QCOMPARE(getClassObject(FileIconHandlerWindows::ClassId, IID_IClassFactory, factory.out()), S_OK);
    QCOMPARE(canUnloadNow(), S_FALSE);
    QCOMPARE(factory->LockServer(TRUE), S_OK);
    QCOMPARE(factory->LockServer(FALSE), S_OK);
    QCOMPARE(factory->CreateInstance(factory.value, IID_IPersistFile, &unsupported), CLASS_E_NOAGGREGATION);
    QVERIFY(!unsupported);

    ComPtr<IPersistFile> persisted;
    QCOMPARE(factory->CreateInstance(nullptr, IID_IPersistFile, persisted.out()), S_OK);
    ComPtr<IExtractIconW> extractor;
    QCOMPARE(persisted->QueryInterface(IID_IExtractIconW, extractor.out()), S_OK);
    ComPtr<IUnknown> identity1, identity2;
    QCOMPARE(persisted->QueryInterface(IID_IUnknown, identity1.out()), S_OK);
    QCOMPARE(extractor->QueryInterface(IID_IUnknown, identity2.out()), S_OK);
    QCOMPARE(identity1.value, identity2.value);
    unsupported = reinterpret_cast<void *>(1);
    QCOMPARE(persisted->QueryInterface(IID_IClassFactory, &unsupported), E_NOINTERFACE);
    QVERIFY(!unsupported);
    CLSID actual{};
    QCOMPARE(persisted->GetClassID(&actual), S_OK);
    QVERIFY(IsEqualCLSID(actual, FileIconHandlerWindows::ClassId));

    // A server lock outlives the factory and all instance interface references.
    QCOMPARE(factory->LockServer(TRUE), S_OK);
    identity1.reset(); identity2.reset(); extractor.reset(); persisted.reset(); factory.reset();
    QCOMPARE(canUnloadNow(), S_FALSE);
    QCOMPARE(getClassObject(FileIconHandlerWindows::ClassId, IID_IClassFactory, factory.out()), S_OK);
    QCOMPARE(factory->LockServer(FALSE), S_OK);
    factory.reset();
    QCOMPARE(canUnloadNow(), S_OK);
}

void WindowsFileIconHandlerTest::filenamesChooseTypeWithoutOpeningFiles()
{
    ComPtr<IClassFactory> factory;
    QCOMPARE(getClassObject(FileIconHandlerWindows::ClassId, IID_IClassFactory, factory.out()), S_OK);
    ComPtr<IPersistFile> persisted;
    QCOMPARE(factory->CreateInstance(nullptr, IID_IPersistFile, persisted.out()), S_OK);
    ComPtr<IExtractIconW> extractor;
    QCOMPARE(persisted->QueryInterface(IID_IExtractIconW, extractor.out()), S_OK);
    const auto checkLocation = [&](int expectedId) {
        std::array<wchar_t, 32768> path{};
        int index = 0;
        UINT flags = 0xffffffff;
        QCOMPARE(extractor->GetIconLocation(0, path.data(), UINT(path.size()), &index, &flags), S_OK);
        QCOMPARE(QString::fromWCharArray(path.data()), executable);
        QCOMPARE(index, -expectedId);
        QCOMPARE(flags, UINT(0));
    };
    checkLocation(FileTypeIcons::forExtension({}).resourceId);
    const QList<QPair<QString, QString>> examples{
        {QStringLiteral("C:/8bitPad-no-such-fixture/directory.py/query.SQL"), "sql"},
        {QStringLiteral("C:/8bitPad-no-such-fixture/ödev ŞĞİ.py"), "py"},
        {QStringLiteral("C:\\8bitPad-no-such-fixture\\directory.sql\\file-without-extension"), ""},
        {QStringLiteral("C:/8bitPad-no-such-fixture/file.custom-private-extension"), ""},
        {QStringLiteral("C:/8bitPad-no-such-fixture/file.sql."), ""},
        {QStringLiteral("C:/8bitPad-no-such-fixture/note.json"), "json"}
    };
    for (const auto &[filename, extension] : examples) {
        QVERIFY(!QFileInfo::exists(filename));
        QCOMPARE(persisted->Load(filename.toStdWString().c_str(), STGM_READ), S_OK);
        checkLocation(FileTypeIcons::forExtension(extension).resourceId);
    }
    for (const auto &type : FileTypeIcons::catalog()) {
        for (const QString &extension : type.extensions) {
            const QString filename = "C:/8bitPad-no-such-fixture/file." + extension.toUpper();
            QCOMPARE(persisted->Load(filename.toStdWString().c_str(), STGM_READ), S_OK);
            checkLocation(type.resourceId);
        }
    }
    std::array<wchar_t, 32768> path{};
    int index = 0;
    UINT flags = 0;
    QCOMPARE(extractor->GetIconLocation(GIL_DEFAULTICON, path.data(), UINT(path.size()), &index, &flags), S_OK);
    QCOMPARE(index, -FileTypeIcons::catalog().last().resourceId);
}

void WindowsFileIconHandlerTest::buffersAndReadOnlyInterfaces()
{
    ComPtr<IClassFactory> factory;
    QCOMPARE(getClassObject(FileIconHandlerWindows::ClassId, IID_IClassFactory, factory.out()), S_OK);
    ComPtr<IPersistFile> persisted;
    QCOMPARE(factory->CreateInstance(nullptr, IID_IPersistFile, persisted.out()), S_OK);
    ComPtr<IExtractIconW> extractor;
    QCOMPARE(persisted->QueryInterface(IID_IExtractIconW, extractor.out()), S_OK);
    QCOMPARE(persisted->Load(nullptr, STGM_READ), E_POINTER);
    QCOMPARE(persisted->Load(L"", STGM_READ), E_INVALIDARG);
    const std::wstring oversized(32768, L'x');
    QCOMPARE(persisted->Load(oversized.c_str(), STGM_READ), E_INVALIDARG);
    std::array<wchar_t, 8> buffer;
    buffer.fill(L'X');
    int index = 987;
    UINT flags = 987;
    QCOMPARE(extractor->GetIconLocation(0, nullptr, 8, &index, &flags), E_POINTER);
    QCOMPARE(extractor->GetIconLocation(0, buffer.data(), 8, nullptr, &flags), E_POINTER);
    QCOMPARE(extractor->GetIconLocation(0, buffer.data(), 8, &index, nullptr), E_POINTER);
    QVERIFY(FAILED(extractor->GetIconLocation(0, buffer.data(), 0, &index, &flags)));
    QCOMPARE(extractor->GetIconLocation(0, buffer.data(), 2, &index, &flags), HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    QCOMPARE(buffer[0], wchar_t(0));
    for (size_t i = 1; i < buffer.size(); ++i) QCOMPARE(buffer[i], L'X');

    QCOMPARE(persisted->IsDirty(), S_FALSE);
    QCOMPARE(persisted->Save(L"C:/must-not-be-created.txt", FALSE), E_NOTIMPL);
    QCOMPARE(persisted->SaveCompleted(nullptr), E_NOTIMPL);
    LPOLESTR current = reinterpret_cast<LPOLESTR>(1);
    QCOMPARE(persisted->GetCurFile(&current), E_NOTIMPL);
    QVERIFY(!current);
    QCOMPARE(persisted->GetCurFile(nullptr), E_POINTER);
    HICON largeIcon = reinterpret_cast<HICON>(1), smallIcon = reinterpret_cast<HICON>(1);
    QCOMPARE(extractor->Extract(L"ignored", 205, &largeIcon, &smallIcon, MAKELONG(32, 16)), S_FALSE);
    QVERIFY(!largeIcon && !smallIcon);
    QCOMPARE(extractor->Extract(nullptr, 0, nullptr, nullptr, 0), S_FALSE);
}

void WindowsFileIconHandlerTest::cleanupTestCase()
{
    if (canUnloadNow) QCOMPARE(canUnloadNow(), S_OK);
    if (module) FreeLibrary(module);
    module = nullptr;
}

QTEST_GUILESS_MAIN(WindowsFileIconHandlerTest)
#include "WindowsFileIconHandlerTest.moc"
