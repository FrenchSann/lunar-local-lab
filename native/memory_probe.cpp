#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <jni.h>
#include "report.hpp"

// No work in DllMain, no hooks, no persistent worker, no game-memory writes.
extern "C" __declspec(dllexport) DWORD WINAPI LunarReadOnlyProbe(void* parameter) {
    if (!parameter) return 1;
    auto& r = *static_cast<ProbeReport*>(parameter);
    r = {}; r.magic = reportMagic; r.status = 1;
    const auto module = GetModuleHandleW(L"jvm.dll");
    if (!module) return r.status;
    const auto base = reinterpret_cast<const unsigned char*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    r.dosSignature = dos->e_magic;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return r.status;
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    r.peSignature = pe->Signature; r.machine = pe->FileHeader.Machine;
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return r.status;
    using GetVMs = jint(JNICALL*)(JavaVM**, jsize, jsize*);
    const auto getVMs = reinterpret_cast<GetVMs>(GetProcAddress(module, "JNI_GetCreatedJavaVMs"));
    if (!getVMs) { r.status = 2; return r.status; }
    JavaVM* vm = nullptr; jsize count = 0;
    if (getVMs(&vm, 1, &count) != JNI_OK || !vm || count != 1) { r.status = 3; return r.status; }
    r.vmCount = static_cast<std::uint32_t>(count);
    JNIEnv* env = nullptr;
    const auto state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) { r.status = 4; return r.status; }
        r.attachedHere = 1;
    } else if (state != JNI_OK) { r.status = 5; return r.status; }
    r.jniVersion = static_cast<std::uint32_t>(env->GetVersion());
    if (r.attachedHere) {
        if (vm->DetachCurrentThread() != JNI_OK) { r.status = 6; return r.status; }
        r.detached = 1;
    }
    r.status = 0;
    return 0;
}
