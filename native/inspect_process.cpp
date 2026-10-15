#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include "report.hpp"

namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
[[noreturn]] void fail(const char* operation) { throw std::runtime_error(std::string(operation) + ": Win32 " + std::to_string(GetLastError())); }
std::vector<MODULEENTRY32W> modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("Module snapshot");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<MODULEENTRY32W> result;
    if (!Module32FirstW(snapshot.value, &entry)) fail("Module32First");
    do { result.push_back(entry); } while (Module32NextW(snapshot.value, &entry));
    return result;
}
MODULEENTRY32W findModule(DWORD pid, const wchar_t* name) {
    for (const auto& m : modules(pid)) if (_wcsicmp(m.szModule, name) == 0) return m;
    throw std::runtime_error("Required module absent");
}
template<class T> T read(HANDLE process, const void* address) {
    T value{}; SIZE_T copied = 0;
    if (!ReadProcessMemory(process, address, &value, sizeof(value), &copied) || copied != sizeof(value)) fail("ReadProcessMemory");
    return value;
}
IMAGE_NT_HEADERS64 peHeader(HANDLE process, const BYTE* base) {
    const auto dos = read<IMAGE_DOS_HEADER>(process, base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 4096) throw std::runtime_error("Invalid DOS header");
    const auto pe = read<IMAGE_NT_HEADERS64>(process, base + dos.e_lfanew);
    if (pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Expected x64 PE image");
    return pe;
}
// Resolve forwarded system exports relative to their actual owning module.
// Never assume a local function address is the remote process's address.
LPTHREAD_START_ROUTINE remoteSystemExport(HANDLE process, DWORD pid, const char* name) {
    auto function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    if (!function) fail("GetProcAddress");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(function), &owner)) fail("Owning module");
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(owner, path, MAX_PATH)) fail("Module path");
    const auto remote = findModule(pid, std::filesystem::path(path).filename().c_str());
    const auto localHeader = peHeader(GetCurrentProcess(), reinterpret_cast<BYTE*>(owner));
    const auto remoteHeader = peHeader(process, remote.modBaseAddr);
    if (localHeader.FileHeader.TimeDateStamp != remoteHeader.FileHeader.TimeDateStamp ||
        localHeader.OptionalHeader.SizeOfImage != remoteHeader.OptionalHeader.SizeOfImage ||
        _wcsicmp(path, remote.szExePath) != 0) throw std::runtime_error("System module identity mismatch");
    const auto offset = reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(owner);
    if (offset >= remote.modBaseSize) throw std::runtime_error("Export outside module");
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(remote.modBaseAddr + offset);
}
struct RemoteBuffer {
    HANDLE process; void* address;
    RemoteBuffer(HANDLE p, SIZE_T size) : process(p), address(VirtualAllocEx(p, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
        if (!address) fail("VirtualAllocEx");
    }
    ~RemoteBuffer() { if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE); }
    RemoteBuffer(const RemoteBuffer&) = delete;
    RemoteBuffer& operator=(const RemoteBuffer&) = delete;
};
DWORD invoke(HANDLE process, LPTHREAD_START_ROUTINE function, void* parameter, RemoteBuffer* buffer = nullptr) {
    Handle thread(CreateRemoteThread(process, nullptr, 0, function, parameter, 0, nullptr));
    if (!thread.value) fail("CreateRemoteThread");
    const auto wait = WaitForSingleObject(thread.value, 10000);
    if (wait != WAIT_OBJECT_0) {
        // Do not free memory the unfinished thread may still use. Never terminate it.
        if (buffer) buffer->address = nullptr;
        throw std::runtime_error("Remote thread completion unconfirmed; allocation/module retained until process exit. No forced unload.");
    }
    DWORD result = 0;
    if (!GetExitCodeThread(thread.value, &result)) fail("GetExitCodeThread");
    return result;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || argc > 4 || (std::wstring(argv[2]) != L"--read-header" && std::wstring(argv[2]) != L"--inject-diagnostic" && std::wstring(argv[2]) != L"--class-metadata" && std::wstring(argv[2]) != L"--game-control"))
            throw std::runtime_error("Usage: inspect_process PID --read-header|--inject-diagnostic|--class-metadata [signature]|--game-control install|status|arm|stop");
        const bool game = std::wstring(argv[2]) == L"--game-control";
        const bool metadata = game || std::wstring(argv[2]) == L"--class-metadata";
        if (game && argc != 4) throw std::runtime_error("Game operation required");
        if (argc == 4 && !metadata) throw std::runtime_error("Unexpected argument");
        static_assert(sizeof(void*) == 8, "Build x64 only");
        size_t consumed = 0; const auto parsed = std::stoul(argv[1], &consumed);
        if (!parsed || consumed != std::wstring(argv[1]).size()) throw std::runtime_error("Invalid PID");
        const auto pid = static_cast<DWORD>(parsed);
        const bool inject = std::wstring(argv[2]) != L"--read-header";
        DWORD access = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
        if (inject) access |= PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE;
        Handle process(OpenProcess(access, FALSE, pid));
        if (!process.value) fail("OpenProcess (no privilege escalation attempted)");
        USHORT machine = 0, native = 0;
        if (!IsWow64Process2(process.value, &machine, &native)) fail("IsWow64Process2");
        if (machine != IMAGE_FILE_MACHINE_UNKNOWN || native != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Target must be native x64");
        wchar_t executable[32768]{}; DWORD size = 32768;
        if (!QueryFullProcessImageNameW(process.value, 0, executable, &size)) fail("Image path");
        const auto filename = std::filesystem::path(executable).filename().wstring();
        if (_wcsicmp(filename.c_str(), L"java.exe") && _wcsicmp(filename.c_str(), L"javaw.exe")) throw std::runtime_error("Target is not a Java executable");
        const auto jvm = findModule(pid, L"jvm.dll");
        const auto header = peHeader(process.value, jvm.modBaseAddr);
        std::cout << "PID " << pid << ": ReadProcessMemory verified jvm.dll MZ/PE, machine=0x" << std::hex
            << header.FileHeader.Machine << ", imageSize=0x" << header.OptionalHeader.SizeOfImage << std::dec << '\n';
        if (!inject) return 0;
        wchar_t self[32768]{};
        if (!GetModuleFileNameW(nullptr, self, 32768)) fail("Inspector path");
        const auto dll = std::filesystem::canonical(std::filesystem::path(self).parent_path() / L"lunar_memory_probe.dll");
        bool resident = false;
        for (const auto& m : modules(pid)) if (_wcsicmp(m.szModule, L"lunar_memory_probe.dll") == 0) {
            if (_wcsicmp(m.szExePath,dll.c_str()) != 0) throw std::runtime_error("Different probe build is already resident");
            resident = true;
        }
        const auto load = remoteSystemExport(process.value, pid, "LoadLibraryW");
        const auto unload = remoteSystemExport(process.value, pid, "FreeLibrary");
        // Inspect the built library's export without running its initialization locally.
        auto local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!local) fail("Inspect probe exports");
        auto entry = GetProcAddress(local, game ? "LunarGameControl" : metadata ? "LunarClassMetadata" : "LunarReadOnlyProbe");
        if (!entry) { FreeLibrary(local); throw std::runtime_error("Missing diagnostic export"); }
        const auto localHeader = peHeader(GetCurrentProcess(), reinterpret_cast<BYTE*>(local));
        const auto offset = reinterpret_cast<std::uintptr_t>(entry) - reinterpret_cast<std::uintptr_t>(local);
        FreeLibrary(local);
        RemoteBuffer path(process.value, (dll.wstring().size() + 1) * sizeof(wchar_t));
        SIZE_T copied = 0; const auto bytes = (dll.wstring().size() + 1) * sizeof(wchar_t);
        if (!WriteProcessMemory(process.value, path.address, dll.c_str(), bytes, &copied) || copied != bytes) fail("Write diagnostic library path");
        if (!resident) invoke(process.value, load, path.address, &path);
        const auto remote = findModule(pid, L"lunar_memory_probe.dll");
        const auto remoteHeader = peHeader(process.value, remote.modBaseAddr);
        if (_wcsicmp(remote.szExePath, dll.c_str()) != 0 || offset >= remote.modBaseSize ||
            remoteHeader.FileHeader.TimeDateStamp != localHeader.FileHeader.TimeDateStamp ||
            remoteHeader.OptionalHeader.SizeOfImage != localHeader.OptionalHeader.SizeOfImage)
            throw std::runtime_error("Loaded library identity mismatch; retained, no export called");
        RemoteBuffer result(process.value, metadata ? sizeof(MetadataReport) : sizeof(ProbeReport));
        auto meta = std::make_unique<MetadataReport>();
        if (metadata) {
            if (argc == 4) {
                const std::wstring query = argv[3];
                if (query.size() >= sizeof(meta->query)) throw std::runtime_error("Query too long");
                for (size_t i = 0; i < query.size(); ++i) {
                    if (query[i] < 32 || query[i] > 126) throw std::runtime_error("ASCII class signature required");
                    meta->query[i] = static_cast<char>(query[i]);
                }
            }
            if (!WriteProcessMemory(process.value, result.address, meta.get(), sizeof(*meta), &copied) || copied != sizeof(*meta)) fail("Write metadata request");
        }
        const auto status = invoke(process.value, reinterpret_cast<LPTHREAD_START_ROUTINE>(remote.modBaseAddr + offset), result.address, &result);
        const auto report = read<ProbeReport>(process.value, result.address);
        if (metadata && (!ReadProcessMemory(process.value, result.address, meta.get(), sizeof(*meta), &copied) || copied != sizeof(*meta))) fail("Read metadata");
        // The export has returned, owns no callbacks and created no worker threads.
        if (!game && !resident) {
            if (!invoke(process.value, unload, remote.modBaseAddr)) throw std::runtime_error("FreeLibrary failed");
            for (const auto& m : modules(pid)) if (_wcsicmp(m.szModule, L"lunar_memory_probe.dll") == 0) throw std::runtime_error("Probe still resident after release");
        }
        if (game) {
            meta->text[sizeof(meta->text)-1] = 0;
            std::cout << meta->text << "\nNative bridge remains resident until JVM exit.\n";
        }
        if (status || report.magic != reportMagic || report.status) throw std::runtime_error("Diagnostic failed, status=" + std::to_string(report.status));
        if (metadata) {
            if(game)return 0;
            meta->text[sizeof(meta->text)-1] = 0;
            std::cout << "Matches=" << meta->matches << ", truncated=" << meta->truncated << '\n' << meta->text;
            return meta->truncated ? 2 : 0;
        }
        std::cout << "Injected diagnostic completed: VM count=" << report.vmCount << ", JNI=0x" << std::hex << report.jniVersion
            << ", MZ=0x" << report.dosSignature << ", PE=0x" << report.peSignature << std::dec
            << ", attachedHere=" << report.attachedHere << ", detached=" << report.detached
            << (resident ? ". Existing resident bridge retained.\n" : ". DLL unloaded after thread completion.\n");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
