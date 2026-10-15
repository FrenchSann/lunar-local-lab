#include <jni.h>
#include <thread>

namespace {
class JvmContext {
public:
    explicit JvmContext(JavaVM* vm) : vm_(vm) {
        const auto status = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_8);
        if (status == JNI_EDETACHED) {
            attached_ = vm_->AttachCurrentThread(reinterpret_cast<void**>(&env_), nullptr) == JNI_OK;
            if (!attached_) env_ = nullptr;
        } else if (status != JNI_OK) env_ = nullptr;
    }
    ~JvmContext() { if (attached_) vm_->DetachCurrentThread(); }
    JvmContext(const JvmContext&) = delete;
    JvmContext& operator=(const JvmContext&) = delete;
    JNIEnv* env() const { return env_; }
private:
    JavaVM* vm_;
    JNIEnv* env_{}; // Constructed, used, destroyed on this thread only.
    bool attached_{};
};
// Read-only resolver using a caller-provided loader and Java binary class name.
// Local references are bounded by the caller's PushLocalFrame/PopLocalFrame.
jclass resolve(JNIEnv* env, jobject loader, const char* name) {
    auto type = env->GetObjectClass(loader);
    if (env->ExceptionCheck() || !type) return nullptr;
    const auto method = env->GetMethodID(type, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (env->ExceptionCheck() || !method) return nullptr;
    auto text = env->NewStringUTF(name);
    if (env->ExceptionCheck() || !text) return nullptr;
    auto result = static_cast<jclass>(env->CallObjectMethod(loader, method, text));
    return env->ExceptionCheck() ? nullptr : result;
}
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_8; }
extern "C" JNIEXPORT jint JNICALL Java_lab_Probe_run(JNIEnv* env, jclass, jobject loader) {
    if (!loader) return 0;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK) return 0;
    auto global = env->NewGlobalRef(loader);
    if (env->ExceptionCheck() || !global) return 0;
    int result = 0;
    // Synchronous ownership: joined before return, no worker survives the call.
    try {
        std::thread worker([&] {
            void* unused = nullptr;
            if (vm->GetEnv(&unused, JNI_VERSION_1_8) == JNI_EDETACHED) result |= 1;
            {
                JvmContext ctx(vm);
                auto local = ctx.env();
                if (!local) return;
                if (local->PushLocalFrame(16) != JNI_OK) { if(local->ExceptionCheck()) local->ExceptionClear(); return; }
                if (resolve(local, global, "lab.Probe")) result |= 2;
                if (local->ExceptionCheck()) local->ExceptionClear();
                if (!resolve(local, global, "lab.IntentionallyMissing") && local->ExceptionCheck()) {
                    local->ExceptionClear(); result |= 4; // Expected negative diagnostic only.
                }
                local->PopLocalFrame(nullptr);
            }
            if (vm->GetEnv(&unused, JNI_VERSION_1_8) == JNI_EDETACHED) result |= 8;
        });
        worker.join();
    } catch (...) { env->DeleteGlobalRef(global); return 0; }
    env->DeleteGlobalRef(global);
    { JvmContext borrowed(vm); if (borrowed.env() == env) result |= 16; }
    void* stillAttached = nullptr;
    if (vm->GetEnv(&stillAttached, JNI_VERSION_1_8) == JNI_OK && stillAttached == env) result |= 32;
    return result;
}
