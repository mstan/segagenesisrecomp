#include "android_host.h"
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL2/SDL.h>
#if RECOMP_LAUNCHER
#include "common/launcher_files.h"

static int pick_android_document(const char *title, const char *const *patterns,
                                 int count, char *out, size_t out_cap)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env) return -1;
    if ((*env)->PushLocalFrame(env, 8) < 0) return -1;

    int result = -1;
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) goto done;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID pick = cls ? (*env)->GetMethodID(env, cls, "openDocument",
                                              "(Ljava/lang/String;[Ljava/lang/String;)[B") : NULL;
    if (!pick) goto done;

    jstring label = (*env)->NewStringUTF(env, title ? title : "Select file");
    jclass string = (*env)->FindClass(env, "java/lang/String");
    if (!label || !string) goto done;
    if (!patterns || count < 0) count = 0;
    jobjectArray filters = (*env)->NewObjectArray(env, count, string, NULL);
    if (!filters) goto done;
    for (int i = 0; i < count; ++i) {
        if (!patterns[i]) continue;
        jstring suffix = (*env)->NewStringUTF(env, patterns[i]);
        if (!suffix) goto done;
        (*env)->SetObjectArrayElement(env, filters, i, suffix);
        (*env)->DeleteLocalRef(env, suffix);
        if ((*env)->ExceptionCheck(env)) goto done;
    }
    jbyteArray selection = (jbyteArray)(*env)->CallObjectMethod(env, activity, pick, label, filters);
    if ((*env)->ExceptionCheck(env)) goto done;
    result = 0;
    if (selection) {
        const jsize size = (*env)->GetArrayLength(env, selection);
        if (size > 0 && (size_t)size < out_cap) {
            (*env)->GetByteArrayRegion(env, selection, 0, size, (jbyte *)out);
            out[size] = '\0';
            result = 1;
        }
    }

done:
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        result = -1;
    }
    (*env)->PopLocalFrame(env, NULL);
    return result;
}

void genesis_android_register_document_picker(void)
{
    launcher_set_android_open_file_picker(pick_android_document);
}
#endif
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
