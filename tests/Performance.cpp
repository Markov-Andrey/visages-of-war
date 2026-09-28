#include "rts/FogOfWar.hpp"
#include "rts/Navigation.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
template<class Work> void measure(const char* name, Work work) {
    using Clock = std::chrono::steady_clock;
    std::vector<double> samples;
    std::uint64_t checksum = 0;
    work(); // Warm caches; report the median, not process startup or the first run.
    for (int repeat = 0; repeat < 7; ++repeat) {
        const auto start = Clock::now();
        checksum += work();
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << ": " << std::fixed << std::setprecision(3) << samples[3]
              << " ms; checksum=" << checksum << '\n';
}
}

int main() {
    rts::Map map(128, 128);
    std::vector<rts::EnvironmentObject> forest;
    for (int y = 20; y < 110; y += 3)
        forest.push_back(rts::makeEnvironment("TREE", static_cast<rts::EntityId>(1 + forest.size()), {67, y}));
    map.rebuildVisionBlockers(forest);
    for (bool clustered : {true, false}) {
        std::vector<rts::VisionSource> sources;
        for (int i = 0; i < 48; ++i)
            sources.push_back({clustered ? rts::Cell{54 + i % 8, 50 + i / 8} : rts::Cell{8 + i % 8 * 16, 8 + i / 8 * 19}, 10, i % 8 == 0});
        rts::FogOfWar fog(map.width(), map.height());
        measure(clustered ? "fog: clustered 48 observers / 80 updates" : "fog: spread 48 observers / 80 updates", [&] {
            for (int i = 0; i < 80; ++i) fog.update(map, sources);
            std::uint64_t checksum = 0;
            for (int y = 0; y < map.height(); ++y) for (int x = 0; x < map.width(); ++x)
                checksum += (1 + x + y * map.width()) * static_cast<unsigned>(fog.at({x, y}));
            return checksum;
        });
    }
    for (int x = 12; x < 118; x += 12)
        for (int y = 4; y < 124; ++y) if (y / 8 % 3 != x / 12 % 3) map.at({x, y}).blocked = true;
    measure("navigation: 64 routes on 128x128", [&] {
        std::uint64_t checksum = 0;
        for (int i = 0; i < 64; ++i) {
            const auto path = rts::findPath(map, {2, 2 + i}, {125, 125 - i});
            if (!path) continue;
            checksum += path->cost;
            for (auto cell : path->cells) checksum = checksum * 31 + cell.x + cell.y * 128;
        }
        return checksum;
    });
}
