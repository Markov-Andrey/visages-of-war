#pragma once
#include <filesystem>

namespace rts {
class Paths {
public:
    // Explicit roots also make tests independent of the user's profile.
    Paths(std::filesystem::path assets, std::filesystem::path userData);
    static Paths discover();
    static std::filesystem::path executable();
    std::filesystem::path asset(const std::filesystem::path& relative) const;
    // Missing is allowed; invalid paths, unreadable files and directories are errors.
    std::filesystem::path optionalAsset(const std::filesystem::path& relative) const;
    std::filesystem::path writable(const std::filesystem::path& relative) const;
    const std::filesystem::path& assetRoot() const { return assets_; }
private:
    static std::filesystem::path resolve(const std::filesystem::path& root, const std::filesystem::path& relative);
    std::filesystem::path assets_;
    std::filesystem::path userData_;
};
}
