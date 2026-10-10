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


JNIEXPORT jlong JNICALL
Java_com_starrift_starzip_NativeBridge_nativeDirCount(JNIEnv *, jobject, jlong id, jlong node) {
    try {
        auto session = findSession(id);
        if (!session) return -static_cast<jlong>(kResultError);
        std::lock_guard<std::mutex> lock(session->mutex);
        const ArchiveTree &tree = session->tree();
        if (node < 0 || static_cast<uint64_t>(node) >= tree.nodeCount())
            return -static_cast<jlong>(kResultError);
        if (!tree.at(static_cast<uint32_t>(node)).isDir) return -static_cast<jlong>(kResultError);
        return static_cast<jlong>(tree.childCount(static_cast<uint32_t>(node)));
    } catch (...) {
        return -static_cast<jlong>(kResultError);
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_starrift_starzip_NativeBridge_nativeGetItems(JNIEnv *env, jobject, jlong id, jlong node,
                                                      jlong offset, jint count) {
    try {
        auto session = findSession(id);
        if (!session) return nullptr;
        std::lock_guard<std::mutex> lock(session->mutex);
        const ArchiveTree &tree = session->tree();
        if (node < 0 || static_cast<uint64_t>(node) >= tree.nodeCount()) return nullptr;
        if (offset < 0 || count <= 0) return nullptr;
        const uint32_t dir = static_cast<uint32_t>(node);
        if (!tree.at(dir).isDir) return nullptr;
        const uint64_t total = tree.childCount(dir);
        if (static_cast<uint64_t>(offset) > total) return nullptr;
        uint64_t take = static_cast<uint64_t>(count > 1000 ? 1000 : count);
        if (take > total - static_cast<uint64_t>(offset)) take = total - static_cast<uint64_t>(offset);

        jclass cls = env->FindClass("com/starrift/starzip/ArchiveEntry");
        if (!cls) {
            env->ExceptionClear();
            return nullptr;
        }
        jmethodID ctor = env->GetMethodID(cls, "<init>", "(JJZLjava/lang/String;)V");
        if (!ctor) {
            env->ExceptionClear();
            env->DeleteLocalRef(cls);
            return nullptr;
        }
        jobjectArray result = env->NewObjectArray(static_cast<jsize>(take), cls, nullptr);
        if (!result) {
            env->ExceptionClear();
            env->DeleteLocalRef(cls);
            return nullptr;
        }
        for (uint64_t k = 0; k < take; ++k) {
            const uint32_t child = tree.childId(dir, static_cast<uint32_t>(static_cast<uint64_t>(offset) + k));
            const TreeNode &entry = tree.at(child);
            const std::u16string name = tree.name(entry);
            jstring text = env->NewString(reinterpret_cast<const jchar *>(name.data()),
                                          static_cast<jsize>(name.size()));
            if (!text) {
                env->ExceptionClear();
                env->DeleteLocalRef(cls);
                return nullptr;
            }
            jobject item = env->NewObject(cls, ctor, static_cast<jlong>(child),
                                          static_cast<jlong>(entry.size),
                                          static_cast<jboolean>(entry.isDir ? JNI_TRUE : JNI_FALSE), text);
            env->DeleteLocalRef(text);
            if (!item) {
                env->ExceptionClear();
                env->DeleteLocalRef(cls);
                return nullptr;
            }
            env->SetObjectArrayElement(result, static_cast<jsize>(k), item);
            env->DeleteLocalRef(item);
        }
        env->DeleteLocalRef(cls);
        return result;
    } catch (...) {
        return nullptr;
    }
}

}
