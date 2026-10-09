#include "session.h"

namespace {

JavaVM *g_vm = nullptr;

struct ThreadAttachment {
    bool attached = false;
    ~ThreadAttachment() {
        if (attached && g_vm) g_vm->DetachCurrentThread();
    }
};

thread_local ThreadAttachment t_attachment;

}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

ThreadEnv::ThreadEnv() {
    if (!g_vm) return;
    void *raw = nullptr;
    jint status = g_vm->GetEnv(&raw, JNI_VERSION_1_6);
    if (status == JNI_OK) {
        env_ = static_cast<JNIEnv *>(raw);
        return;
    }
    if (status != JNI_EDETACHED) return;
    JNIEnv *attachedEnv = nullptr;
    if (g_vm->AttachCurrentThread(&attachedEnv, nullptr) == JNI_OK) {
        t_attachment.attached = true;
        env_ = attachedEnv;
    }
}

bool ProgressSink::init(JNIEnv *env, jobject callback) {
    if (!env || !callback) return false;
    jclass cls = env->GetObjectClass(callback);
    if (!cls) return false;
    method_ = env->GetMethodID(cls, "onProgress", "(ILjava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (!method_) {
        env->ExceptionClear();
        return false;
    }
    callback_ = env->NewGlobalRef(callback);
    last_ = std::chrono::steady_clock::now();
    return callback_ != nullptr;
}

ProgressSink::~ProgressSink() {
    if (!callback_) return;
    ThreadEnv threadEnv;
    if (threadEnv.get()) threadEnv.get()->DeleteGlobalRef(callback_);
    callback_ = nullptr;
}

void ProgressSink::report(uint64_t done, uint64_t total, const std::u16string &name, bool force) {
    if (!callback_) return;
    int percent = total ? static_cast<int>((done > total ? total : done) * 100 / total) : 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        bool timeUp = (now - last_) >= std::chrono::milliseconds(500);
        if (!force && percent == lastPercent_ && !timeUp) return;
        lastPercent_ = percent;
        last_ = now;
    }
    ThreadEnv threadEnv;
    JNIEnv *env = threadEnv.get();
    if (!env) return;
    jstring text = env->NewString(reinterpret_cast<const jchar *>(name.data()),
                                  static_cast<jsize>(name.size()));
    if (!text) {
        env->ExceptionClear();
        return;
    }
    env->CallVoidMethod(callback_, method_, static_cast<jint>(percent), text);
    env->DeleteLocalRef(text);
    if (env->ExceptionCheck()) env->ExceptionClear();
}
