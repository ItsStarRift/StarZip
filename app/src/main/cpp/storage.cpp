#define _FILE_OFFSET_BITS 64
#include "storage.h"
#include <sys/stat.h>

NativeFile::~NativeFile() { close(); }

bool NativeFile::openPath(const char *path, bool write) {
    close();
    file_ = fopen(path, write ? "wb" : "rb");
    return file_ != nullptr;
}

bool NativeFile::openFd(int fd, bool write) {
    close();
    int copy = dup(fd);
    if (copy < 0) return false;
    file_ = fdopen(copy, write ? "wb" : "rb");
    if (!file_) {
        ::close(copy);
        return false;
    }
    return true;
}

size_t NativeFile::read(void *buf, size_t size) {
    return file_ ? fread(buf, 1, size, file_) : 0;
}

size_t NativeFile::write(const void *buf, size_t size) {
    return file_ ? fwrite(buf, 1, size, file_) : 0;
}

bool NativeFile::seek(int64_t offset, int whence) {
    return file_ && fseeko(file_, (off_t) offset, whence) == 0;
}

int64_t NativeFile::tell() const {
    return file_ ? (int64_t) ftello(file_) : -1;
}

int64_t NativeFile::size() const {
    struct stat st;
    if (!file_ || fstat(fileno(file_), &st) != 0) return -1;
    return (int64_t) st.st_size;
}

bool NativeFile::flush() { return file_ && fflush(file_) == 0; }

bool NativeFile::hasError() const { return file_ && ferror(file_) != 0; }

int NativeFile::fd() const { return file_ ? fileno(file_) : -1; }

void NativeFile::close() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}
