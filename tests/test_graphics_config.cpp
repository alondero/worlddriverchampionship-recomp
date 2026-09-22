#include "graphics_config.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>

using namespace wdc::graphics;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("wdc-graphics-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        const auto path = dir / "graphics.json";
        load(path);
        require(snapshot() == Settings{}, "missing file must use defaults");
        Settings s;
        s.resolution = 4; s.aspect = 2; s.rate = 144; s.samples = 4;
        s.filtering = 2; s.precision = 2; s.fullscreen = true;
        require(apply(s), "save failed");
        load(path);
        require(snapshot() == s, "settings did not survive restart");
        require(apply(Settings{}), "replacing an existing file failed");
        load(path);
        require(snapshot() == Settings{}, "defaults were not persisted");
        for (const char* invalid : {"{", "[]", "{\"version\":2}", "{\"resolution\":\"4\"}",
                                   "{\"rate\":18446744073709551615}", "{\"samples\":2.5}"}) {
            { std::ofstream out(path); out << invalid; }
            load(path);
            require(snapshot() == Settings{}, "invalid settings were accepted");
            std::ifstream in(path);
            const std::string preserved{std::istreambuf_iterator<char>(in), {}};
            require(preserved == invalid, "load destroyed malformed settings");
        }
        { std::ofstream out(path); out << R"({"resolution":999,"aspect":-3,"rate":999,"samples":3,"filtering":-1,"precision":99})"; }
        load(path);
        const auto bounded = snapshot();
        require(bounded.resolution == 8 && bounded.aspect == 0 && bounded.rate == 0 &&
            bounded.samples == 1 && bounded.filtering == 1 && bounded.precision == 0, "values not bounded");
        // A failed save still publishes a whole snapshot and leaves old data intact.
        load(dir / "missing-directory" / "graphics.json");
        require(!apply(s), "save unexpectedly succeeded");
        require(snapshot() == s, "failed save lost live settings");
        load(path);
        std::atomic<bool> stop{}, invalid{};
        std::thread reader([&] {
            while (!stop) {
                auto value = snapshot();
                if (value != bounded && value != s && value != Settings{}) invalid = true;
            }
        });
        bool saved = true;
        for (int i = 0; i < 12; ++i) saved &= apply(i % 2 ? s : Settings{});
        stop = true;
        reader.join();
        require(saved && !invalid, "concurrent snapshots were incomplete");
        std::filesystem::remove_all(dir);
        std::cout << "graphics configuration tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(dir);
        return 1;
    }
}
