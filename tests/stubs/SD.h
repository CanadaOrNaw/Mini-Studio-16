#pragma once

#include "Arduino.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"

class SDStub;

class File {
public:
    File() : owner_(nullptr), position_(0), writable_(false), valid_(false) {}
    explicit operator bool() const;
    size_t write(const uint8_t* data, size_t length);
    int read(uint8_t* data, size_t length);
    bool seek(uint32_t position);
    int available() const;
    uint32_t position() const { return static_cast<uint32_t>(position_); }
    size_t size() const;
    const char* name() const { return path_.c_str(); }
    bool isDirectory() const { return false; }
    File openNextFile() { return File(); }
    void flush() {}
    void close() { valid_ = false; }

private:
    friend class SDStub;
    File(SDStub* owner, const char* path, bool writable)
        : owner_(owner), path_(path ? path : ""), position_(0),
          writable_(writable), valid_(true) {}

    SDStub* owner_;
    std::string path_;
    size_t position_;
    bool writable_;
    bool valid_;
};

class SDStub {
public:
    template <typename Spi>
    bool begin(uint8_t, Spi&, uint32_t) { return true; }

    bool exists(const char* path) const {
        return path && files_.find(path) != files_.end();
    }
    bool mkdir(const char*) { return true; }
    bool rmdir(const char*) { return true; }
    bool remove(const char* path) {
        if (!path) return false;
        return files_.erase(path) != 0;
    }
    bool rename(const char* from, const char* to) {
        if (!from || !to) return false;
        if (failRename_ && failRenameFrom_ == from && failRenameTo_ == to) {
            failRename_ = false;
            return false;
        }
        auto source = files_.find(from);
        if (source == files_.end() || files_.find(to) != files_.end()) return false;
        files_[to] = source->second;
        files_.erase(source);
        return true;
    }
    File open(const char* path, const char* mode) {
        if (!path || !mode) return File();
        const bool writable = mode[0] == 'w' || (mode[0] == 'r' && mode[1] == '+');
        if (mode[0] == 'w') files_[path].clear();
        else if (!exists(path)) return File();
        return File(this, path, writable);
    }
    File open(const char* path) { return open(path, FILE_READ); }

    void reset() {
        files_.clear();
        failRename_ = false;
        failRenameFrom_.clear();
        failRenameTo_.clear();
    }
    void put(const char* path, const uint8_t* data, size_t length) {
        std::vector<uint8_t>& file = files_[path ? path : ""];
        file.assign(data, data + length);
    }
    void put(const char* path, const std::vector<uint8_t>& data) {
        files_[path ? path : ""] = data;
    }
    const std::vector<uint8_t>* bytes(const char* path) const {
        auto it = files_.find(path ? path : "");
        return it == files_.end() ? nullptr : &it->second;
    }
    std::vector<uint8_t>* mutableBytes(const char* path) {
        auto it = files_.find(path ? path : "");
        return it == files_.end() ? nullptr : &it->second;
    }
    void failNextRename(const char* from, const char* to) {
        failRename_ = true;
        failRenameFrom_ = from ? from : "";
        failRenameTo_ = to ? to : "";
    }

private:
    friend class File;
    std::map<std::string, std::vector<uint8_t>> files_;
    bool failRename_ = false;
    std::string failRenameFrom_;
    std::string failRenameTo_;
};

inline File::operator bool() const {
    return valid_ && owner_ && owner_->exists(path_.c_str());
}
inline size_t File::write(const uint8_t* data, size_t length) {
    if (!valid_ || !owner_ || !writable_ || !data) return 0;
    std::vector<uint8_t>& file = owner_->files_[path_];
    if (position_ + length > file.size()) file.resize(position_ + length);
    std::copy(data, data + length, file.begin() + static_cast<std::ptrdiff_t>(position_));
    position_ += length;
    return length;
}
inline int File::read(uint8_t* data, size_t length) {
    if (!valid_ || !owner_ || !data) return 0;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end() || position_ >= it->second.size()) return 0;
    const size_t available = it->second.size() - position_;
    const size_t count = length < available ? length : available;
    std::copy(it->second.begin() + static_cast<std::ptrdiff_t>(position_),
              it->second.begin() + static_cast<std::ptrdiff_t>(position_ + count), data);
    position_ += count;
    return static_cast<int>(count);
}
inline bool File::seek(uint32_t position) {
    if (!valid_ || !owner_) return false;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end()) return false;
    if (position > it->second.size() && !writable_) return false;
    if (position > it->second.size()) it->second.resize(position);
    position_ = position;
    return true;
}
inline int File::available() const {
    if (!valid_ || !owner_) return 0;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end() || position_ >= it->second.size()) return 0;
    return static_cast<int>(it->second.size() - position_);
}
inline size_t File::size() const {
    if (!valid_ || !owner_) return 0;
    auto it = owner_->files_.find(path_);
    return it == owner_->files_.end() ? 0 : it->second.size();
}

extern SDStub SD;
