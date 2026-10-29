#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace bl::detail {

// Dawn owns binary compatibility keys; this store treats each key/value as opaque bytes.
class WebGpuCache
{
    static constexpr uint64_t magic = 0x3145484341434c42ull;
    std::filesystem::path directory;
    std::mutex mutex;
    std::vector<std::byte> loaded, loaded_key;
    uint64_t serial = 0;

    static uint64_t hash(const void* data, size_t size)
    {
        uint64_t result = 14695981039346656037ull;
        for (auto byte : std::span(static_cast<const unsigned char*>(data), size)) result = (result ^ byte) * 1099511628211ull;
        return result;
    }
    std::filesystem::path path(const void* key, size_t size) const { return directory / (std::to_string(hash(key, size)) + ".bin"); }

public:
    explicit WebGpuCache(const std::string& directory) : directory(std::filesystem::u8path(directory))
    {
        std::error_code error;
        std::filesystem::create_directories(this->directory, error);
        if (error) this->directory.clear();
    }

    size_t load(const void* key, size_t key_size, void* value, size_t value_size) noexcept
    {
        try
        {
            std::lock_guard lock(mutex);
            if (directory.empty()) return 0;
            if (!value)
            {
                loaded.clear(); loaded_key.clear();
                std::ifstream file(path(key, key_size), std::ios::binary | std::ios::ate);
                const auto length = file.tellg();
                std::array<uint64_t, 4> header;
                // Reject damaged lengths before allocating memory for a cache entry.
                if (length < std::streamoff(sizeof(header)) || length > 64 * 1024 * 1024) return 0;
                file.seekg(0);
                if (!file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) || header[0] != magic || header[1] != key_size ||
                    header[1] > uint64_t(length) - sizeof(header) || header[2] != uint64_t(length) - sizeof(header) - header[1]) return 0;
                loaded_key.resize(key_size);
                if (!file.read(reinterpret_cast<char*>(loaded_key.data()), key_size) || std::memcmp(loaded_key.data(), key, key_size) != 0) return 0;
                loaded.resize(size_t(header[2]));
                if (!file.read(reinterpret_cast<char*>(loaded.data()), loaded.size()) || hash(loaded.data(), loaded.size()) != header[3])
                { loaded.clear(); return 0; }
                return loaded.size();
            }
            // Dawn requires the size query and copy to agree, even if another process removes the file in between.
            if (loaded_key.size() != key_size || std::memcmp(loaded_key.data(), key, key_size) != 0 || value_size < loaded.size()) return 0;
            std::memcpy(value, loaded.data(), loaded.size());
            return loaded.size();
        }
        catch (...) { return 0; }
    }

    void store(const void* key, size_t key_size, const void* value, size_t value_size) noexcept
    {
        try
        {
            std::lock_guard lock(mutex);
            if (directory.empty()) return;
            const auto target = path(key, key_size);
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial);
            auto temporary = target; temporary += "." + suffix + ".tmp";
            std::array<uint64_t, 4> header = {magic, key_size, value_size, hash(value, value_size)};
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
            file.write(static_cast<const char*>(key), key_size); file.write(static_cast<const char*>(value), value_size);
            file.close();
            std::error_code error;
            if (file)
            {
                // Publish complete entries; Windows rename needs an existing target removed first.
                std::filesystem::rename(temporary, target, error);
                if (error) { std::filesystem::remove(target, error); std::filesystem::rename(temporary, target, error); }
            }
            std::filesystem::remove(temporary, error);
        }
        catch (...) {} // Cache I/O must never prevent device creation or shader compilation.
    }
};

} // namespace bl::detail
