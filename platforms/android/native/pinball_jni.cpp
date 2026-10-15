#include <jni.h>
#include <SDL.h>
#include <unistd.h>
#include <string>
#include "pinball_platform.h"

extern "C" JNIEXPORT void JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativePause(JNIEnv*, jclass, jboolean paused, jint session) { platform_notify_pause(paused, static_cast<uint32_t>(session)); }
extern "C" JNIEXPORT void JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeResetInputSession(JNIEnv*, jclass, jint session) { platform_reset_input_session(static_cast<uint32_t>(session)); }
extern "C" JNIEXPORT void JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeCancelInputs(JNIEnv*, jclass) { platform_cancel_inputs(); }
extern "C" JNIEXPORT void JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeFlushSettings(JNIEnv*, jclass) { platform_flush_settings(); }
extern "C" JNIEXPORT void JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeActivateAudio(JNIEnv*, jclass) { platform_activate_audio(); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeIsEnglish(JNIEnv*, jclass) { return platform_is_english() ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_codex_spacecadetpinball_MainActivity_nativeSetAssetRoot(JNIEnv* env, jclass, jstring path) {
    if (!path) return JNI_FALSE;
    const char* root=env->GetStringUTFChars(path,nullptr);
    if (!root) return JNI_FALSE;
    const auto ok=chdir(root)==0;
    env->ReleaseStringUTFChars(path,root);
    return ok ? JNI_TRUE : JNI_FALSE;
}

static void invokeActivity(const char* name) {
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=env->GetMethodID(type,name,"()V");
    if (method) env->CallVoidMethod(activity,method);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->DeleteLocalRef(type); env->DeleteLocalRef(activity);
}
extern "C" void platform_android_ready() { invokeActivity("onNativeReady"); }
