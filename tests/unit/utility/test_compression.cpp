#include <catch2/catch_test_macros.hpp>
#include <bitloop/util/compression.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace {
    std::mt19937& rng() {
        static std::mt19937 gen(0xC0FFEEu); // fixed seed for repeatability
        return gen;
    }

    std::string random_printable_string(std::size_t len) {
        static const char charset[] =
            " \t\n\r"
            "!\"#$%&'()*+,-./0123456789:;<=>?@"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
            "abcdefghijklmnopqrstuvwxyz{|}~";
        std::uniform_int_distribution<int> dist(0, int(sizeof(charset) - 2));
        std::string s;
        s.resize(len);
        for (std::size_t i = 0; i < len; ++i) s[i] = charset[dist(rng())];
        return s;
    }

    std::string random_binary_string(std::size_t len) {
        std::uniform_int_distribution<int> dist(0, 255);
        std::string s;
        s.resize(len);
        for (std::size_t i = 0; i < len; ++i) {
            char c;
            do {
                c = static_cast<char>(dist(rng()));
            } while (c == '\033'); // Avoid 0x1B
            s[i] = c;
        }
        return s;
    }

} // namespace

// Put this near the top of the test file (or make it a lambda in place)
static inline bool is_base64_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '+' || c == '/' || c == '=';
}

static std::string to_hex(const std::string& s, size_t max_bytes = 256) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    size_t n = std::min(max_bytes, s.size());
    for (size_t i = 0; i < n; ++i) {
        os << std::setw(2) << (static_cast<unsigned>(static_cast<unsigned char>(s[i])));
        if (i + 1 != n) os << ' ';
    }
    if (s.size() > n) os << " ... (" << s.size() << " bytes total)";
    return os.str();
}

TEST_CASE("base64_compress / base64_decompress", "[compression]")
{
    // Hand-picked edge cases (including UTF-8 and embedded nulls)
    std::vector<std::string> edge_cases = {
        "",
        "a",
        "ab",
        "abc",
        "The quick brown fox jumps over the lazy dog.",
        std::string(1, '\0'),
        std::string("\0\0\0", 3),
        std::string("null\0byte", 9),
        std::string(1024, 'A'),
        "héllo 🌍 — UTF-8 test"
    };


    for (const auto& test_data : edge_cases) {
        std::string compressed = compression::brotli_ascii_compress(test_data);
        std::string decompressed = compression::brotli_ascii_decompress(compressed);

        // Round-trip must hold
        REQUIRE(decompressed == test_data);
    }


    // Fuzz: random printable strings of varying lengths
    {
        std::uniform_int_distribution<int> len_dist(0, 1024);
        for (int i = 0; i < 250; ++i) {
            const auto len = static_cast<std::size_t>(len_dist(rng()));
            const auto test_data = random_printable_string(len);
            const auto compressed = compression::brotli_ascii_compress(test_data);
            const auto decompressed = compression::brotli_ascii_decompress(compressed);
            REQUIRE(decompressed == test_data);
        }
    }

    // Fuzz: random binary blobs (full 0–255, includes '\0')
    {
        std::uniform_int_distribution<int> len_dist(0, 1024);
        for (int i = 0; i < 250; ++i) {
            const auto len = static_cast<std::size_t>(len_dist(rng()));
            const auto test_data = random_binary_string(len);
            const auto compressed = compression::brotli_ascii_compress(test_data);
            const auto decompressed = compression::brotli_ascii_decompress(compressed);
            bool matches = decompressed == test_data;
            REQUIRE(matches);
        }
    }
}
