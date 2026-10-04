#include <jni.h>
#include "storage.h"
#include <string>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#define TAG "StarZipNative"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

using Clock = std::chrono::steady_clock;

enum Result { RES_OK = 0, RES_CANCELLED = 1, RES_ERROR = 2 };

std::atomic<bool> g_isCancelled(false);

class ProgressReporter {
public:
    ProgressReporter(JNIEnv *env, jobject cb) : env_(env), cb_(cb) {
        jclass cls = env->GetObjectClass(cb);
        method_ = env->GetMethodID(cls, "onProgress", "(ILjava/lang/String;)V");
        env->DeleteLocalRef(cls);
        if (!method_) env->ExceptionClear();
        last_ = Clock::now();
    }

    bool valid() const { return method_ != nullptr; }

    void report(uint64_t done, uint64_t total, const char *name, bool force = false) {
        if (!method_) return;
        int pct = total ? (int) ((done * 100) / total) : 0;
        auto now = Clock::now();
        bool timeUp = (now - last_) >= std::chrono::milliseconds(500);
        if (!force && pct == lastPct_ && !timeUp) return;
        lastPct_ = pct;
        last_ = now;
        jstring js = env_->NewStringUTF(name);
        env_->CallVoidMethod(cb_, method_, (jint) pct, js);
        env_->DeleteLocalRef(js);
        if (env_->ExceptionCheck()) env_->ExceptionClear();
    }

private:
    JNIEnv *env_;
    jobject cb_;
    jmethodID method_ = nullptr;
    int lastPct_ = -1;
    Clock::time_point last_;
};

}

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_starrift_starzip_NativeBridge_nativeVersion(JNIEnv *env, jobject) {
    return env->NewStringUTF("StarZip native OK (step 2)");
}

JNIEXPORT void JNICALL
Java_com_starrift_starzip_NativeBridge_nativeCancelOperation(JNIEnv *, jobject) {
    g_isCancelled.store(true);
}

JNIEXPORT jint JNICALL
Java_com_starrift_starzip_NativeBridge_nativeSelfTest(JNIEnv *env, jobject,
                                                      jint totalMb, jint chunkMb,
                                                      jobject callback) {
    g_isCancelled.store(false);

    ProgressReporter progress(env, callback);
    if (!progress.valid()) { LOGE("onProgress not found"); return RES_ERROR; }

    if (chunkMb < 1) chunkMb = 1;
    if (chunkMb > 16) chunkMb = 16;
    const uint64_t chunk = (uint64_t) chunkMb * 1024 * 1024;
    const uint64_t total = (uint64_t) (totalMb < 1 ? 1 : totalMb) * 1024 * 1024;

    std::vector<uint8_t> buf(chunk);
    uint64_t done = 0;

    while (done < total) {
        if (g_isCancelled.load()) return RES_CANCELLED;
        std::memset(buf.data(), (int) (done & 0xFF), buf.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        done += chunk;
        progress.report(done < total ? done : total, total, "selftest.bin");
    }
    progress.report(total, total, "selftest.bin", true);
    return RES_OK;
}


JNIEXPORT jint JNICALL
Java_com_starrift_starzip_NativeBridge_nativeCopy(JNIEnv *env, jobject,
                                                  jstring inPath, jint inFd,
                                                  jstring outPath, jint outFd,
                                                  jint chunkMb, jobject callback) {
    g_isCancelled.store(false);

    ProgressReporter progress(env, callback);
    if (!progress.valid()) return RES_ERROR;

    if (chunkMb < 1) chunkMb = 1;
    if (chunkMb > 16) chunkMb = 16;

    NativeFile in;
    NativeFile out;
    std::string outPathStr;
    bool opened;

    if (inPath) {
        const char *p = env->GetStringUTFChars(inPath, nullptr);
        opened = p && in.openPath(p, false);
        if (p) env->ReleaseStringUTFChars(inPath, p);
    } else {
        opened = in.openFd(inFd, false);
    }
    if (!opened) { LOGE("cannot open input"); return RES_ERROR; }

    if (outPath) {
        const char *p = env->GetStringUTFChars(outPath, nullptr);
        if (p) outPathStr = p;
        opened = p && out.openPath(p, true);
        if (p) env->ReleaseStringUTFChars(outPath, p);
    } else {
        opened = out.openFd(outFd, true);
    }
    if (!opened) { LOGE("cannot open output"); return RES_ERROR; }

    auto discardOutput = [&]() {
        if (!outPathStr.empty()) {
            out.close();
            unlink(outPathStr.c_str());
        } else {
            if (out.fd() >= 0) (void) ftruncate(out.fd(), 0);
            out.close();
        }
    };

    const int64_t totalSigned = in.size();
    const uint64_t total = totalSigned > 0 ? (uint64_t) totalSigned : 1;
    std::vector<uint8_t> buf((size_t) chunkMb * 1024 * 1024);
    uint64_t done = 0;

    while (true) {
        if (g_isCancelled.load()) {
            discardOutput();
            return RES_CANCELLED;
        }
        size_t n = in.read(buf.data(), buf.size());
        if (n == 0) {
            if (in.hasError()) {
                discardOutput();
                return RES_ERROR;
            }
            break;
        }
        if (out.write(buf.data(), n) != n) {
            discardOutput();
            return RES_ERROR;
        }
        done += n;
        progress.report(done < total ? done : total, total, "copy");
    }

    if (!out.flush()) {
        discardOutput();
        return RES_ERROR;
    }
    progress.report(total, total, "copy", true);
    return RES_OK;
}

}
