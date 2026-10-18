#include <celerpay/statement.hpp>

#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace std;

namespace celerpay::statement {

using celerpay::detail::fail;

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_{exchange(other.data_, nullptr)}, size_{exchange(other.size_, 0)}, file_{exchange(other.file_, nullptr)},
      mapping_{exchange(other.mapping_, nullptr)} {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        MappedFile old(std::move(*this));
        data_ = exchange(other.data_, nullptr);
        size_ = exchange(other.size_, 0);
        file_ = exchange(other.file_, nullptr);
        mapping_ = exchange(other.mapping_, nullptr);
    }
    return *this;
}

#if defined(_WIN32)

MappedFile::~MappedFile() {
    if (data_ != nullptr) {
        UnmapViewOfFile(data_);
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
    }
    if (file_ != nullptr) {
        CloseHandle(file_);
    }
}

Result<MappedFile> MappedFile::open(const filesystem::path& path) {
    MappedFile out;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return fail(ErrorCode::IoError, 0, "não foi possível abrir o arquivo");
    }
    out.file_ = file;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return fail(ErrorCode::IoError, 0, "não foi possível ler o tamanho do arquivo");
    }
    if (size.QuadPart == 0) {
        return out;
    }
    out.mapping_ = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (out.mapping_ == nullptr) {
        return fail(ErrorCode::IoError, 0, "não foi possível mapear o arquivo");
    }
    out.data_ = static_cast<const char*>(MapViewOfFile(out.mapping_, FILE_MAP_READ, 0, 0, 0));
    if (out.data_ == nullptr) {
        return fail(ErrorCode::IoError, 0, "não foi possível mapear o arquivo");
    }
    out.size_ = static_cast<size_t>(size.QuadPart);
    return out;
}

#else

MappedFile::~MappedFile() {
    if (data_ != nullptr) {
        munmap(const_cast<char*>(data_), size_);
    }
    if (file_ != nullptr) {
        ::close(static_cast<int>(reinterpret_cast<intptr_t>(file_)) - 1);
    }
}

Result<MappedFile> MappedFile::open(const filesystem::path& path) {
    MappedFile out;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return fail(ErrorCode::IoError, 0, "não foi possível abrir o arquivo");
    }
    out.file_ = reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1);
    struct stat info {};
    if (fstat(fd, &info) != 0) {
        return fail(ErrorCode::IoError, 0, "não foi possível ler o tamanho do arquivo");
    }
    if (info.st_size == 0) {
        return out;
    }
    void* data = mmap(nullptr, static_cast<size_t>(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        return fail(ErrorCode::IoError, 0, "não foi possível mapear o arquivo");
    }
    madvise(data, static_cast<size_t>(info.st_size), MADV_SEQUENTIAL);
    out.data_ = static_cast<const char*>(data);
    out.size_ = static_cast<size_t>(info.st_size);
    return out;
}

#endif

}
