#include "../../src/gpu/webgpu_cache.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition) { if (!condition) throw std::runtime_error("WebGPU cache regression"); }

int main()
{
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("bitloop-webgpu-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        const std::string key = "shader-layout-driver-key", data = "compiled shader bytes";
        bl::detail::WebGpuCache cache(directory.string());
        require(cache.load(key.data(), key.size(), nullptr, 0) == 0);
        cache.store(key.data(), key.size(), data.data(), data.size());
        bl::detail::WebGpuCache reopened(directory.string());
        require(reopened.load(key.data(), key.size(), nullptr, 0) == data.size());
        auto file = fs::directory_iterator(directory)->path();
        fs::remove(file);
        std::string actual(data.size(), '\0');
        require(reopened.load(key.data(), key.size(), actual.data(), actual.size()) == data.size() && actual == data);
        cache.store(key.data(), key.size(), data.data(), data.size());
        file = fs::directory_iterator(directory)->path();
        { std::fstream damaged(file, std::ios::binary | std::ios::in | std::ios::out); damaged.seekp(-1, std::ios::end); damaged.put('!'); }
        require(cache.load(key.data(), key.size(), nullptr, 0) == 0);
        cache.store(key.data(), key.size(), data.data(), data.size());
        require(cache.load(key.data(), key.size(), nullptr, 0) == data.size());
        { std::ofstream damaged(file, std::ios::trunc); damaged << "truncated"; }
        require(cache.load(key.data(), key.size(), nullptr, 0) == 0);
        fs::remove(file);
        { std::ofstream blocker(directory / "blocked"); blocker << "not a directory"; }
        bl::detail::WebGpuCache blocked((directory / "blocked" / "cache").string());
        blocked.store(key.data(), key.size(), data.data(), data.size());
        require(blocked.load(key.data(), key.size(), nullptr, 0) == 0);
        for (const auto& entry : fs::directory_iterator(directory)) fs::remove(entry.path());
        fs::remove(directory);
        std::cout << "PASS WebGPU cache persistence, corruption, load snapshot and unavailable storage\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
