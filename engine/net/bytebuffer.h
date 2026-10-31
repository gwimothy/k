#pragma once
// Simple little-endian serialization for network messages.
// (Bit-packing and delta compression come in Phase 7; the interface stays the same.)

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace k::net {

class ByteWriter {
public:
    template <class T>
        requires std::is_trivially_copyable_v<T>
    void write(const T& value) {
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        data_.insert(data_.end(), p, p + sizeof(T));
    }

    void writeString(std::string_view s) {
        write<uint16_t>(static_cast<uint16_t>(s.size()));
        data_.insert(data_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(s.size(), 0xFFFF)));
    }

    void writeBytes(std::span<const uint8_t> bytes) { data_.insert(data_.end(), bytes.begin(), bytes.end()); }

    const std::vector<uint8_t>& data() const { return data_; }
    std::vector<uint8_t>&& take() { return std::move(data_); }
    size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }

private:
    std::vector<uint8_t> data_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    // On underflow the reader enters an overflowed state and returns zeroed values.
    template <class T>
        requires std::is_trivially_copyable_v<T>
    T read() {
        T value{};
        if (!canRead(sizeof(T))) {
            overflowed_ = true;
            pos_ = data_.size();
            return value;
        }
        std::memcpy(&value, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    std::string readString() {
        auto len = read<uint16_t>();
        if (!canRead(len)) {
            overflowed_ = true;
            pos_ = data_.size();
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return s;
    }

    bool overflowed() const { return overflowed_; }
    size_t remaining() const { return data_.size() - pos_; }
    bool atEnd() const { return pos_ >= data_.size(); }

private:
    bool canRead(size_t n) const { return !overflowed_ && pos_ + n <= data_.size(); }

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    bool overflowed_ = false;
};

} // namespace k::net
