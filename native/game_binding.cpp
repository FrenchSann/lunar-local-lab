#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <jvmti.h>
#include <memory>
#include <mutex>
#include <cstring>
#include <cmath>
#include "report.hpp"
#include "lunar/core.hpp"
#include "game_bridge_bytes.hpp"

namespace {
std::mutex engineMutex;
std::unique_ptr<lunar::ActionCoordinator> engine;
int configuredPeriod = 0;
jint JNICALL plan(JNIEnv*, jclass, jlong generation, jlong now, jboolean eligible, jboolean armed, jboolean physical, jboolean block, jint period) {
    try {
        std::lock_guard lock(engineMutex);
        if (period < 20 || period > 1000) return 0;
        if (!engine || configuredPeriod != period || !armed) {
            lunar::Configuration config; config.repeat = lunar::Millis{period};
            engine = std::make_unique<lunar::ActionCoordinator>(config); configuredPeriod = period;
        }
        lunar::Snapshot s; s.context = {static_cast<std::uint64_t>(generation), "", false, eligible != 0, eligible != 0, false};
        s.captured = lunar::Time{lunar::Millis{now}}; s.physicalLeft = physical != 0; s.block = block != 0;
        auto actions = engine->tick(s);
        if (!eligible || !armed) return 0;
        engine->arm(s.context);
        auto more = engine->tick(s); actions.insert(actions.end(), more.begin(), more.end());
        int result = 0;
        for (const auto& a : actions) {
            if (a.kind == lunar::ActionKind::Click) result |= 1;
            if (a.kind == lunar::ActionKind::BeginMining) result |= 2;
            if (a.kind == lunar::ActionKind::EndMining) result |= 4;
        }
        return result;
    } catch (...) { return 0; }
}
lunar::Armor unpack(const jint* x) { return {x[0],x[1],x[2],x[3],x[4],x[5]}; }
jint JNICALL chooseArmor(JNIEnv* env, jclass, jintArray current, jintArray candidates, jdouble threshold) {
    if (!current || !candidates || env->GetArrayLength(current) != 6) return 0;
    const auto size = env->GetArrayLength(candidates); if (size % 6 || size > 4096) return 0;
    try {
        jint worn[6]{}; std::vector<jint> packed(size);
        env->GetIntArrayRegion(current,0,6,worn); if(env->ExceptionCheck())return 0;
        env->GetIntArrayRegion(candidates,0,size,packed.data()); if(env->ExceptionCheck())return 0;
        std::vector<lunar::Armor> items;
        for (jsize i=0;i<size;i+=6) items.push_back(unpack(packed.data()+i));
        auto selected = lunar::replacement(unpack(worn), items, threshold); return selected ? selected->id : 0;
    } catch (...) { return 0; }
}
jdoubleArray JNICALL select(JNIEnv* env, jclass, jdoubleArray geometry, jdouble reach, jdouble barrier) {
    if (!geometry || !std::isfinite(reach) || reach<=0 || reach>6 || std::isnan(barrier) || barrier<0) return nullptr;
    const auto size=env->GetArrayLength(geometry);if(size<6 || (size-6)%6 || size>24582)return nullptr;
    try {
        std::vector<jdouble> data(size);env->GetDoubleArrayRegion(geometry,0,size,data.data());if(env->ExceptionCheck())return nullptr;
        lunar::Vec3 eye{data[0],data[1],data[2]},direction{data[3],data[4],data[5]};
        std::vector<lunar::Target> targets;
        for(jsize i=6;i<size;i+=6)targets.push_back({(i-6)/6,{{data[i],data[i+1],data[i+2]},{data[i+3],data[i+4],data[i+5]}}});
        auto selected=lunar::selectTarget(eye,direction,reach,targets,{});
        if(!selected || selected->entryDistance>=barrier)return nullptr;
        auto result=env->NewDoubleArray(2);if(!result)return nullptr;
        const double values[]{static_cast<double>(selected->id),selected->entryDistance};env->SetDoubleArrayRegion(result,0,2,values);return result;
    }catch(...){return nullptr;}
}
}
// This mode intentionally keeps the DLL resident until JVM exit, including after stop.
// Registered Java callbacks can therefore never jump into an unloaded native image.
extern "C" __declspec(dllexport) DWORD WINAPI LunarGameControl(void* parameter) {
    if(!parameter)return 1;
    auto& out=*static_cast<MetadataReport*>(parameter);out.magic=reportMagic;out.status=1;out.text[0]=0;
    out.query[sizeof(out.query)-1]=0;
    if(std::strcmp(out.query,"status")&&std::strcmp(out.query,"install")&&std::strcmp(out.query,"stop")&&std::strcmp(out.query,"arm"))return out.status;
    using GetVMs=jint(JNICALL*)(JavaVM**,jsize,jsize*);
    auto getVMs=reinterpret_cast<GetVMs>(GetProcAddress(GetModuleHandleW(L"jvm.dll"),"JNI_GetCreatedJavaVMs"));
    JavaVM* vm{};jsize count{};JNIEnv* env{};
    if(!getVMs||getVMs(&vm,1,&count)!=JNI_OK||count!=1)return out.status;
    auto state=vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_8);bool attached=state==JNI_EDETACHED;
    if((attached&&vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env),nullptr)!=JNI_OK)||(!attached&&state!=JNI_OK))return out.status;
    if(env->PushLocalFrame(64)!=JNI_OK){if(env->ExceptionCheck())env->ExceptionClear();if(attached)vm->DetachCurrentThread();return out.status;}
    jvmtiEnv* ti{};jclass mc{},bridge{};
    if(vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)==JNI_OK){
        jclass* classes{};jint size{};
        if(ti->GetLoadedClasses(&size,&classes)==JVMTI_ERROR_NONE){
            for(jint i=0;i<size;++i){char* signature{};
                if(ti->GetClassSignature(classes[i],&signature,nullptr)==JVMTI_ERROR_NONE){
                    if(!std::strcmp(signature,"Lnet/minecraft/client/Minecraft;"))mc=static_cast<jclass>(env->NewLocalRef(classes[i]));
                    if(!std::strcmp(signature,"Llab/lunar/GameBridge;"))bridge=static_cast<jclass>(env->NewLocalRef(classes[i]));
                    ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
                }env->DeleteLocalRef(classes[i]);
            }ti->Deallocate(reinterpret_cast<unsigned char*>(classes));
        }ti->DisposeEnvironment();
    }
    if(mc&&!bridge){
        auto type=env->FindClass("java/lang/Class");
        auto getLoader=type?env->GetMethodID(type,"getClassLoader","()Ljava/lang/ClassLoader;"):nullptr;
        if(getLoader&&!env->ExceptionCheck()){
            auto loader=env->CallObjectMethod(mc,getLoader);
            if(loader&&!env->ExceptionCheck())bridge=env->DefineClass("lab/lunar/GameBridge",loader,reinterpret_cast<const jbyte*>(gameBridgeBytes),sizeof(gameBridgeBytes));
        }
    }
    if(bridge&&!env->ExceptionCheck()){
        JNINativeMethod natives[]{
            {const_cast<char*>("plan"),const_cast<char*>("(JJZZZZI)I"),reinterpret_cast<void*>(&plan)},
            {const_cast<char*>("chooseArmor"),const_cast<char*>("([I[ID)I"),reinterpret_cast<void*>(&chooseArmor)},
            {const_cast<char*>("select"),const_cast<char*>("([DDD)[D"),reinterpret_cast<void*>(&select)}
        };
        if(env->RegisterNatives(bridge,natives,3)==JNI_OK){
            auto command=env->GetStaticMethodID(bridge,"command","(Ljava/lang/String;)Ljava/lang/String;");
            if(command&&!env->ExceptionCheck()){
                auto input=env->NewStringUTF(out.query);
                if(input&&!env->ExceptionCheck()){
                    auto reply=static_cast<jstring>(env->CallStaticObjectMethod(bridge,command,input));
                    if(reply&&!env->ExceptionCheck()){
                        const char* text=env->GetStringUTFChars(reply,nullptr);
                        if(text){strncpy_s(out.text,text,_TRUNCATE);env->ReleaseStringUTFChars(reply,text);out.status=0;}
                    }
                }
            }
        }
    }
    if(env->ExceptionCheck()){
        env->ExceptionClear();strcpy_s(out.text,"Bridge call failed; application actions were not confirmed.");out.status=2;
    }else if(!mc){strcpy_s(out.text,"Minecraft class not found in this JVM.");out.status=3;}
    env->PopLocalFrame(nullptr);
    if(attached&&vm->DetachCurrentThread()!=JNI_OK)out.status=4;
    return out.status;
}
