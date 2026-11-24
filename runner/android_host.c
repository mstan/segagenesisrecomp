#include "android_host.h"
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL2/SDL.h>
unsigned genesis_android_fullscreen_flags(unsigned requested)
{
    (void)requested;
    return SDL_WINDOW_FULLSCREEN_DESKTOP;
}
void genesis_android_set_gameplay(int available)
{
    static int previous = -1;
    available = !!available;
    if (available == previous) return;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID method = cls ? (*env)->GetMethodID(env, cls,
        "setTouchControlsAvailable", "(Z)V") : NULL;
    if (method) {
        (*env)->CallVoidMethod(env, activity, method, available ? JNI_TRUE : JNI_FALSE);
        previous = available;
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}
#else
void genesis_android_set_gameplay(int available) { (void)available; }
unsigned genesis_android_fullscreen_flags(unsigned requested) { return requested; }
#endif
