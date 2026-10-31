// kpak - pack a directory into a .kpk archive, or list one.
//
//   kpak <directory> <out.kpk> [-x prefix ...]   pack (skip paths starting with prefix)
//   kpak -l <file.kpk>                            list contents
//
// The engine mounts every .kpk in the game directory after the loose files.

#include "core/filesystem.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "-l") {
        if (!k::fs::addArchive(args[1], "PAK")) {
            std::fprintf(stderr, "can't read %s\n", args[1].c_str());
            return 1;
        }
        uint64_t total = 0;
        for (auto& sp : k::fs::searchPaths())
            for (auto& [path, e] : sp.entries) {
                std::printf("%10llu  %s\n", static_cast<unsigned long long>(e.size), path.c_str());
                total += e.size;
            }
        std::printf("%10llu  total\n", static_cast<unsigned long long>(total));
        return 0;
    }
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: kpak <directory> <out.kpk> [-x prefix ...]\n       kpak -l <file.kpk>\n");
        return 2;
    }
    std::vector<std::string> exclude;
    for (size_t i = 2; i + 1 < args.size(); i += 2)
        if (args[i] == "-x") exclude.push_back(args[i + 1]);
    std::string error;
    if (!k::fs::writePak(args[0], args[1], exclude, &error)) {
        std::fprintf(stderr, "kpak: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote %s\n", args[1].c_str());
    return 0;
}
