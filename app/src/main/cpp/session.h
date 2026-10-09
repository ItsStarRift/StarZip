#pragma once
#include <jni.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

constexpr jint kResultOk = 0;
constexpr jint kResultCancelled = 1;
constexpr jint kResultError = 2;

class ThreadEnv {
public:
    ThreadEnv();
    JNIEnv *get() const { return env_; }

private:
    JNIEnv *env_ = nullptr;
};

class ProgressSink {
public:
    ProgressSink() = default;
    ~ProgressSink();
    ProgressSink(const ProgressSink &) = delete;
    ProgressSink &operator=(const ProgressSink &) = delete;

    bool init(JNIEnv *env, jobject callback);
    void report(uint64_t done, uint64_t total, const std::u16string &name, bool force);

private:
    jobject callback_ = nullptr;
    jmethodID method_ = nullptr;
    int lastPercent_ = -1;
    std::chrono::steady_clock::time_point last_;
    std::mutex mutex_;
};

template <class F>
jint GuardedCall(F &&body) {
    try {
        return body();
    } catch (...) {
        return kResultError;
    }
}
