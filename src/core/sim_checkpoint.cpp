#include <bitloop/core/sim_checkpoint.h>

#ifdef BITLOOP_DEV_MODE

#include <algorithm>
#include <cctype>

BL_BEGIN_NS

namespace
{
    std::string sanitizeChunkName(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || c == '_' || c == '-' || c == '.')
                out.push_back(c);
            else
                out.push_back('_');
        }

        if (out.empty())
            out = "chunk";
        return out;
    }

    void writeJsonFile(const std::filesystem::path& path, const JSON::json& value)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            throw std::runtime_error("Could not write checkpoint JSON file: " + path.string());
        out << value.dump(2) << '\n';
    }
}

SimCheckpointWriter::SimCheckpointWriter(std::filesystem::path root)
    : root_dir(std::move(root))
{
    std::filesystem::create_directories(root_dir);
}

std::filesystem::path SimCheckpointWriter::chunkPath(std::string_view name) const
{
    return root_dir / (sanitizeChunkName(name) + ".bin");
}

void SimCheckpointWriter::writeBytes(
    std::string_view name,
    const void* data,
    std::size_t bytes,
    JSON::json metadata)
{
    const std::string safe_name = sanitizeChunkName(name);
    const std::filesystem::path path = chunkPath(safe_name);

    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("Could not write checkpoint chunk: " + path.string());

    if (bytes > 0)
    {
        if (!data)
            throw std::runtime_error("Checkpoint chunk has bytes but null data: " + std::string(name));
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
        if (!out)
            throw std::runtime_error("Failed while writing checkpoint chunk: " + path.string());
    }

    metadata["file"] = path.filename().string();
    metadata["bytes"] = bytes;
    chunks[safe_name] = std::move(metadata);
}

void SimCheckpointWriter::writeJson(std::string_view name, const JSON::json& value)
{
    const std::string text = value.dump();
    JSON::json metadata = {
        {"type", "json"}
    };
    writeBytes(name, text.data(), text.size(), std::move(metadata));
}

void SimCheckpointWriter::finalize(JSON::json metadata) const
{
    metadata["schema_version"] = 1;
    metadata["chunks"] = chunks;
    writeJsonFile(root_dir / "manifest.json", metadata);
}

SimCheckpointReader::SimCheckpointReader(std::filesystem::path root)
    : root_dir(std::move(root))
{
    std::ifstream in(root_dir / "manifest.json", std::ios::binary);
    if (!in)
        throw std::runtime_error("Could not read checkpoint manifest: " + (root_dir / "manifest.json").string());
    manifest = JSON::json::parse(in);
}

std::filesystem::path SimCheckpointReader::chunkPath(std::string_view name) const
{
    const std::string safe_name = sanitizeChunkName(name);
    if (manifest.contains("chunks") && manifest["chunks"].contains(safe_name))
    {
        const JSON::json& chunk = manifest["chunks"][safe_name];
        if (chunk.contains("file"))
            return root_dir / chunk["file"].get<std::string>();
    }
    return root_dir / (safe_name + ".bin");
}

bool SimCheckpointReader::hasChunk(std::string_view name) const
{
    return manifest.contains("chunks") && manifest["chunks"].contains(sanitizeChunkName(name));
}

JSON::json SimCheckpointReader::chunkMetadata(std::string_view name) const
{
    if (!hasChunk(name))
        return JSON::json::object();
    return manifest["chunks"][sanitizeChunkName(name)];
}

bytebuf SimCheckpointReader::readBytes(std::string_view name) const
{
    const std::filesystem::path path = chunkPath(name);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("Could not read checkpoint chunk: " + path.string());

    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    bytebuf out(static_cast<std::size_t>(std::max<std::streamsize>(0, size)));
    if (!out.empty())
    {
        in.read(reinterpret_cast<char*>(out.data()), size);
        if (!in)
            throw std::runtime_error("Failed while reading checkpoint chunk: " + path.string());
    }
    return out;
}

void SimCheckpointReader::readBytes(std::string_view name, void* data, std::size_t bytes) const
{
    const bytebuf stored = readBytes(name);
    if (stored.size() != bytes)
        throw std::runtime_error("Checkpoint chunk size mismatch: " + std::string(name));
    if (bytes > 0)
        std::memcpy(data, stored.data(), bytes);
}

JSON::json SimCheckpointReader::readJson(std::string_view name) const
{
    const bytebuf bytes = readBytes(name);
    return JSON::json::parse(bytes.begin(), bytes.end());
}

BL_END_NS

#endif
