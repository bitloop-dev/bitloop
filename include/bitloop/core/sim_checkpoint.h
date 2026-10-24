#pragma once

#ifdef BITLOOP_DEV_MODE

#include <bitloop/core/types.h>
#include <bitloop/util/json.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

BL_BEGIN_NS

class SimCheckpointWriter
{
    std::filesystem::path root_dir;
    JSON::json chunks = JSON::json::object();

    [[nodiscard]] std::filesystem::path chunkPath(std::string_view name) const;

public:
    explicit SimCheckpointWriter(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const { return root_dir; }

    void writeBytes(std::string_view name, const void* data, std::size_t bytes, JSON::json metadata = {});
    void writeJson(std::string_view name, const JSON::json& value);
    void finalize(JSON::json metadata = {}) const;

    template<typename T>
    void writePod(std::string_view name, const T& value, JSON::json metadata = {})
    {
        static_assert(std::is_trivially_copyable_v<T>, "writePod requires a trivially copyable type");
        metadata["type"] = "pod";
        metadata["sizeof"] = sizeof(T);
        writeBytes(name, &value, sizeof(T), std::move(metadata));
    }

    template<typename T>
    void writeTrivialVector(std::string_view name, const std::vector<T>& values, JSON::json metadata = {})
    {
        static_assert(std::is_trivially_copyable_v<T>, "writeTrivialVector requires trivially copyable elements");
        metadata["type"] = "trivial_vector";
        metadata["sizeof"] = sizeof(T);
        metadata["count"] = values.size();
        const void* data = values.empty() ? nullptr : values.data();
        writeBytes(name, data, values.size() * sizeof(T), std::move(metadata));
    }

    template<typename T>
    void writeTrivialSpan(std::string_view name, std::span<const T> values, JSON::json metadata = {})
    {
        static_assert(std::is_trivially_copyable_v<T>, "writeTrivialSpan requires trivially copyable elements");
        metadata["type"] = "trivial_span";
        metadata["sizeof"] = sizeof(T);
        metadata["count"] = values.size();
        const void* data = values.empty() ? nullptr : values.data();
        writeBytes(name, data, values.size_bytes(), std::move(metadata));
    }
};

class SimCheckpointReader
{
    std::filesystem::path root_dir;
    JSON::json manifest = JSON::json::object();

    [[nodiscard]] std::filesystem::path chunkPath(std::string_view name) const;

public:
    explicit SimCheckpointReader(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const { return root_dir; }
    [[nodiscard]] const JSON::json& metadata() const { return manifest; }
    [[nodiscard]] bool hasChunk(std::string_view name) const;
    [[nodiscard]] JSON::json chunkMetadata(std::string_view name) const;

    [[nodiscard]] bytebuf readBytes(std::string_view name) const;
    void readBytes(std::string_view name, void* data, std::size_t bytes) const;
    [[nodiscard]] JSON::json readJson(std::string_view name) const;

    template<typename T>
    void readPod(std::string_view name, T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "readPod requires a trivially copyable type");
        readBytes(name, &value, sizeof(T));
    }

    template<typename T>
    [[nodiscard]] std::vector<T> readTrivialVector(std::string_view name) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "readTrivialVector requires trivially copyable elements");
        const bytebuf bytes = readBytes(name);
        if (bytes.size() % sizeof(T) != 0)
            throw std::runtime_error("Checkpoint chunk size is not a multiple of element size: " + std::string(name));

        std::vector<T> out(bytes.size() / sizeof(T));
        if (!bytes.empty())
            std::memcpy(out.data(), bytes.data(), bytes.size());
        return out;
    }
};

BL_END_NS

#endif
