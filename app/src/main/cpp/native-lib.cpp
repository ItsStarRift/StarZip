#include <jni.h>
extern "C" JNIEXPORT jstring JNICALL
Java_com_starrift_starzip_NativeBridge_nativeVersion(JNIEnv *env, jobject) {
    return env->NewStringUTF("StarZip native OK");
}
