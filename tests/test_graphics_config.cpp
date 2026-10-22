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
    const auto original_directory = std::filesystem::current_path();
    const auto dir = std::filesystem::temp_directory_path() /
        ("wdc-graphics-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        // An uninitialized save must publish live settings without staging a
        // write against the empty path (which otherwise overwrites CWD/.tmp).
        std::filesystem::current_path(dir);
        const auto temporary = dir / ".tmp";
        { std::ofstream out(temporary); out << "preserve unconfigured save sentinel"; }
        auto unconfigured = Settings{};
        unconfigured.resolution = 2;
        require(!apply(unconfigured), "unconfigured save unexpectedly succeeded");
        require(snapshot() == unconfigured, "unconfigured save lost live settings");
        std::ifstream before_load(temporary);
        const std::string preserved_temporary{std::istreambuf_iterator<char>(before_load), {}};
        before_load.close();
        require(preserved_temporary == "preserve unconfigured save sentinel",
            "unconfigured save wrote a temporary file");
        std::filesystem::current_path(original_directory);
        const auto path = dir / "graphics.json";
        load(path);
        require(snapshot() == Settings{}, "missing file must use defaults");
        Settings s;
        s.resolution = 4; s.rate = 144; s.samples = 4;
        s.filtering = 2; s.precision = 2; s.fullscreen = true; s.hud = 2;
        require(apply(s), "save failed");
        load(path);
        require(snapshot() == s, "settings did not survive restart");
        require(apply(Settings{}), "replacing an existing file failed");
        load(path);
        require(snapshot() == Settings{}, "defaults were not persisted");
        for (int hud : {-1, 0, 1, 2, 3}) {
            { std::ofstream out(path); out << "{\"version\":2,\"hud\":" << hud << "}"; }
            load(path);
            require(snapshot().hud == (hud >= 0 && hud <= 2 ? hud : 1), "HUD placement not normalized");
        }
        for (int aspect : {0, 1, 2}) {
            { std::ofstream out(path); out << "{\"aspect\":" << aspect << ",\"resolution\":4}"; }
            load(path);
            require(snapshot().aspect == 1 && snapshot().resolution == 4,
                "legacy forced-4:3 setting did not upgrade to the wider camera");
            auto requested = snapshot();
            requested.aspect = 0;
            require(apply(requested), "saving original aspect failed");
            load(path);
            require(snapshot().aspect == 0, "explicit original aspect did not survive restart");
        }
        for (const char* invalid : {"{", "[]", "{\"version\":3}", "{\"resolution\":\"4\"}",
                                   "{\"rate\":18446744073709551615}", "{\"samples\":2.5}", "{\"hud\":\"Full\"}"}) {
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
        require(bounded.resolution == 8 && bounded.aspect == 1 && bounded.rate == 0 &&
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
        std::filesystem::current_path(original_directory);
        std::filesystem::remove_all(dir);
        return 1;
    }
}
