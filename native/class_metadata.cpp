#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <jvmti.h>
#include <cstring>
#include <string>
#include "report.hpp"

extern "C" __declspec(dllexport) DWORD WINAPI LunarClassMetadata(void* parameter) {
    if (!parameter) return 1;
    auto& out = *static_cast<MetadataReport*>(parameter);
    out.magic = reportMagic; out.status = 1; out.matches = out.truncated = 0;
    out.query[sizeof(out.query)-1] = 0; out.text[0] = 0;
    using GetVMs = jint(JNICALL*)(JavaVM**, jsize, jsize*);
    auto getVMs = reinterpret_cast<GetVMs>(GetProcAddress(GetModuleHandleW(L"jvm.dll"), "JNI_GetCreatedJavaVMs"));
    JavaVM* vm{}; jsize vmCount{}; JNIEnv* env{};
    if (!getVMs || getVMs(&vm, 1, &vmCount) != JNI_OK || vmCount != 1) return out.status;
    const auto state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    const bool attach = state == JNI_EDETACHED;
    if ((attach && vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) || (!attach && state != JNI_OK)) return out.status;
    jvmtiEnv* ti{};
    if (vm->GetEnv(reinterpret_cast<void**>(&ti), JVMTI_VERSION_1_2) != JNI_OK) {
        if (attach) vm->DetachCurrentThread(); out.status = 2; return out.status;
    }
    auto free = [&](void* p) { if (p) ti->Deallocate(static_cast<unsigned char*>(p)); };
    size_t used = 0;
    auto append = [&](const std::string& line) {
        if (used + line.size() + 1 >= sizeof(out.text)) { out.truncated = 1; return; }
        std::memcpy(out.text + used, line.data(), line.size()); used += line.size(); out.text[used] = 0;
    };
    jint count{}; jclass* classes{};
    auto error = ti->GetLoadedClasses(&count, &classes);
    if (error == JVMTI_ERROR_NONE) {
        for (jint i = 0; i < count; ++i) {
            char* signature{};
            if (ti->GetClassSignature(classes[i], &signature, nullptr) == JVMTI_ERROR_NONE && signature) {
                const bool exact = out.query[0] && std::strcmp(signature, out.query) == 0;
                const bool selected = exact || (!out.query[0] && (std::strstr(signature, "net/minecraft/") || std::strstr(signature, "Minecraft") || std::strcmp(signature, "Lave;") == 0));
                if (selected) {
                    ++out.matches; append(std::string("CLASS ") + signature + "\n");
                    if (exact) {
                        // Reflection links an unprepared class without initializing it.
                        auto classType = env->FindClass("java/lang/Class");
                        auto declared = classType ? env->GetMethodID(classType, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;") : nullptr;
                        if (declared) {
                            auto methods = env->CallObjectMethod(classes[i], declared);
                            if (env->ExceptionCheck()) { env->ExceptionClear(); append("LINK_FAILED\n"); }
                            if (methods) env->DeleteLocalRef(methods);
                        }
                        if (classType) env->DeleteLocalRef(classType);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        jint fieldsCount{}; jfieldID* fields{};
                        if (ti->GetClassFields(classes[i], &fieldsCount, &fields) == JVMTI_ERROR_NONE) {
                            for (jint f = 0; f < fieldsCount; ++f) {
                                char *name{}, *descriptor{}; jint flags{};
                                if (ti->GetFieldName(classes[i], fields[f], &name, &descriptor, nullptr) == JVMTI_ERROR_NONE &&
                                    ti->GetFieldModifiers(classes[i], fields[f], &flags) == JVMTI_ERROR_NONE)
                                    append("FIELD " + std::to_string(flags) + " " + name + " " + descriptor + "\n");
                                free(name); free(descriptor);
                            }
                            free(fields);
                        }
                        jint methodsCount{}; jmethodID* methods{};
                        if (ti->GetClassMethods(classes[i], &methodsCount, &methods) == JVMTI_ERROR_NONE) {
                            for (jint m = 0; m < methodsCount; ++m) {
                                char *name{}, *descriptor{}; jint flags{};
                                if (ti->GetMethodName(methods[m], &name, &descriptor, nullptr) == JVMTI_ERROR_NONE &&
                                    ti->GetMethodModifiers(methods[m], &flags) == JVMTI_ERROR_NONE)
                                    append("METHOD " + std::to_string(flags) + " " + name + " " + descriptor + "\n");
                                free(name); free(descriptor);
                            }
                            free(methods);
                        }
                    }
                }
                free(signature);
            }
            env->DeleteLocalRef(classes[i]);
        }
        free(classes); out.status = 0;
    } else out.status = static_cast<std::uint32_t>(error);
    ti->DisposeEnvironment();
    if (attach && vm->DetachCurrentThread() != JNI_OK) out.status = 3;
    return out.status;
}
