#include <jni.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "archive_session.h"

namespace {

std::mutex g_tableMutex;
std::map<jlong, std::shared_ptr<ArchiveSession>> g_sessions;
std::atomic<jlong> g_nextId(1);

std::shared_ptr<ArchiveSession> findSession(jlong id) {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    auto it = g_sessions.find(id);
    return it == g_sessions.end() ? nullptr : it->second;
}

}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_starrift_starzip_NativeBridge_nativeOpenArchive(JNIEnv *env, jobject, jint fd, jstring name) {
    try {
        if (!name) return -static_cast<jlong>(kResultError);
        const jchar *chars = env->GetStringChars(name, nullptr);
        if (!chars) return -static_cast<jlong>(kResultError);
        std::u16string text(reinterpret_cast<const char16_t *>(chars),
                            static_cast<size_t>(env->GetStringLength(name)));
        env->ReleaseStringChars(name, chars);

        auto session = std::make_shared<ArchiveSession>();
        jint code;
        {
            std::lock_guard<std::mutex> lock(session->mutex);
            code = session->open(fd, text);
        }
        if (code != kResultOk) return -static_cast<jlong>(code);

        jlong id = g_nextId.fetch_add(1);
        std::lock_guard<std::mutex> lock(g_tableMutex);
        g_sessions[id] = session;
        return id;
    } catch (...) {
        return -static_cast<jlong>(kResultError);
    }
}

JNIEXPORT jlong JNICALL
Java_com_starrift_starzip_NativeBridge_nativeArchiveItemCount(JNIEnv *, jobject, jlong id) {
    try {
        auto session = findSession(id);
        if (!session) return -static_cast<jlong>(kResultError);
        std::lock_guard<std::mutex> lock(session->mutex);
        uint32_t count = 0;
        if (session->itemCount(count) != S_OK) return -static_cast<jlong>(kResultError);
        return static_cast<jlong>(count);
    } catch (...) {
        return -static_cast<jlong>(kResultError);
    }
}

JNIEXPORT void JNICALL
Java_com_starrift_starzip_NativeBridge_nativeCancelArchive(JNIEnv *, jobject, jlong id) {
    try {
        auto session = findSession(id);
        if (session) session->cancel();
    } catch (...) {
    }
}

JNIEXPORT void JNICALL
Java_com_starrift_starzip_NativeBridge_nativeCloseArchive(JNIEnv *, jobject, jlong id) {
    try {
        std::shared_ptr<ArchiveSession> session;
        {
            std::lock_guard<std::mutex> lock(g_tableMutex);
            auto it = g_sessions.find(id);
            if (it == g_sessions.end()) return;
            session = it->second;
            g_sessions.erase(it);
        }
        session->cancel();
    } catch (...) {
    }
}

}
