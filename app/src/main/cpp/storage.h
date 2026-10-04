#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

class NativeFile {
public:
    NativeFile() = default;
    ~NativeFile();
    NativeFile(const NativeFile &) = delete;
    NativeFile &operator=(const NativeFile &) = delete;

    bool openPath(const char *path, bool write);
    bool openFd(int fd, bool write);
    size_t read(void *buf, size_t size);
    size_t write(const void *buf, size_t size);
    bool seek(int64_t offset, int whence);
    int64_t tell() const;
    int64_t size() const;
    bool flush();
    bool hasError() const;
    int fd() const;
    bool isOpen() const { return file_ != nullptr; }
    void close();

private:
    FILE *file_ = nullptr;
};
