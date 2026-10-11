#include <jni.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct DirEntry {
    std::string name;
    int64_t size;
    int64_t modified;
    bool isDir;
};

struct DirListing {
    std::vector<DirEntry> entries;
};

std::mutex g_mutex;
std::map<jlong, std::shared_ptr<DirListing>> g_listings;
std::atomic<jlong> g_nextId(1);

std::string Utf16ToUtf8(const jchar *text, size_t length) {
    std::string out;
    out.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        uint32_t cp = text[i];
        if (cp >= 0xD800 && cp < 0xDC00) {
            if (i + 1 < length && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(text[i + 1]) - 0xDC00);
                ++i;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::u16string Utf8ToUtf16(const std::string &s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (c < 0x80) {
            cp = c;
            len = 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1F;
            len = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0F;
            len = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07;
            len = 4;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + len > n) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (ok) {
            if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                (cp >= 0xD800 && cp <= 0xDFFF)) {
                ok = false;
            }
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += len;
    }
    return out;
}

unsigned char Fold(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}

bool EntryLess(const DirEntry &a, const DirEntry &b) {
    if (a.isDir != b.isDir) return a.isDir;
    const size_t common = a.name.size() < b.name.size() ? a.name.size() : b.name.size();
    for (size_t i = 0; i < common; ++i) {
        const unsigned char x = Fold(static_cast<unsigned char>(a.name[i]));
        const unsigned char y = Fold(static_cast<unsigned char>(b.name[i]));
        if (x != y) return x < y;
    }
    if (a.name.size() != b.name.size()) return a.name.size() < b.name.size();
    return a.name < b.name;
}

std::shared_ptr<DirListing> Find(jlong id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_listings.find(id);
    return it == g_listings.end() ? nullptr : it->second;
}

}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_starrift_starzip_NativeBridge_nativeListDir(JNIEnv *env, jobject, jstring path) {
    try {
        if (!path) return -static_cast<jlong>(EINVAL);
        const jchar *chars = env->GetStringChars(path, nullptr);
        if (!chars) return -static_cast<jlong>(ENOMEM);
        const std::string utf8 = Utf16ToUtf8(chars, static_cast<size_t>(env->GetStringLength(path)));
        env->ReleaseStringChars(path, chars);

        DIR *dir = opendir(utf8.c_str());
        if (!dir) return -static_cast<jlong>(errno != 0 ? errno : EIO);
        auto listing = std::make_shared<DirListing>();
        const int fd = dirfd(dir);
        struct dirent *item;
        while ((item = readdir(dir)) != nullptr) {
            const char *name = item->d_name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
            DirEntry entry;
            entry.name = name;
            entry.size = 0;
            entry.modified = 0;
            entry.isDir = (item->d_type == DT_DIR);
            struct stat st;
            if (fstatat(fd, name, &st, 0) == 0 || fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                entry.isDir = S_ISDIR(st.st_mode);
                entry.size = entry.isDir ? 0 : static_cast<int64_t>(st.st_size);
                entry.modified = static_cast<int64_t>(st.st_mtime);
            }
            listing->entries.push_back(std::move(entry));
        }
        closedir(dir);
        std::sort(listing->entries.begin(), listing->entries.end(), EntryLess);

        const jlong id = g_nextId.fetch_add(1);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_listings[id] = listing;
        return id;
    } catch (...) {
        return -static_cast<jlong>(ENOMEM);
    }
}

JNIEXPORT jlong JNICALL
Java_com_starrift_starzip_NativeBridge_nativeDirTotal(JNIEnv *, jobject, jlong id) {
    try {
        auto listing = Find(id);
        if (!listing) return -1;
        return static_cast<jlong>(listing->entries.size());
    } catch (...) {
        return -1;
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_starrift_starzip_NativeBridge_nativeDirPage(JNIEnv *env, jobject, jlong id, jlong offset,
                                                     jint count) {
    try {
        auto listing = Find(id);
        if (!listing) return nullptr;
        const uint64_t total = listing->entries.size();
        if (offset < 0 || count <= 0 || static_cast<uint64_t>(offset) > total) return nullptr;
        uint64_t take = static_cast<uint64_t>(count > 1000 ? 1000 : count);
        if (take > total - static_cast<uint64_t>(offset)) take = total - static_cast<uint64_t>(offset);

        jclass cls = env->FindClass("com/starrift/starzip/FileEntry");
        if (!cls) {
            env->ExceptionClear();
            return nullptr;
        }
        jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;JZJ)V");
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
            const DirEntry &entry = listing->entries[static_cast<size_t>(static_cast<uint64_t>(offset) + k)];
            const std::u16string name = Utf8ToUtf16(entry.name);
            jstring text = env->NewString(reinterpret_cast<const jchar *>(name.data()),
                                          static_cast<jsize>(name.size()));
            if (!text) {
                env->ExceptionClear();
                env->DeleteLocalRef(cls);
                return nullptr;
            }
            jobject object = env->NewObject(cls, ctor, text, static_cast<jlong>(entry.size),
                                            static_cast<jboolean>(entry.isDir ? JNI_TRUE : JNI_FALSE),
                                            static_cast<jlong>(entry.modified));
            env->DeleteLocalRef(text);
            if (!object) {
                env->ExceptionClear();
                env->DeleteLocalRef(cls);
                return nullptr;
            }
            env->SetObjectArrayElement(result, static_cast<jsize>(k), object);
            env->DeleteLocalRef(object);
        }
        env->DeleteLocalRef(cls);
        return result;
    } catch (...) {
        return nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_starrift_starzip_NativeBridge_nativeCloseDir(JNIEnv *, jobject, jlong id) {
    try {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_listings.erase(id);
    } catch (...) {
    }
}

}
