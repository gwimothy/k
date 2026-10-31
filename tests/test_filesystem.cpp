#include "core/cmdbuffer.h"
#include "core/cvar.h"
#include "core/filesystem.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace k;
namespace stdfs = std::filesystem;

namespace {
stdfs::path makeTempDir(const char* tag) {
    std::random_device rd;
    auto p = stdfs::temp_directory_path() / ("k_test_" + std::string(tag) + "_" + std::to_string(rd()));
    stdfs::create_directories(p);
    return p;
}
} // namespace

TEST_CASE("Search paths are checked in order and writes go to the first match") {
    auto a = makeTempDir("a"), b = makeTempDir("b");
    fs::clearSearchPaths();
    fs::addSearchPath(a, "MOD");
    fs::addSearchPath(b, "GAME");

    CHECK(fs::writeText("cfg/x.cfg", "from b", "GAME"));
    CHECK(stdfs::exists(b / "cfg/x.cfg"));
    CHECK(fs::readText("cfg/x.cfg").value() == "from b");

    CHECK(fs::writeText("cfg/x.cfg", "from a", "MOD"));
    CHECK(fs::readText("cfg/x.cfg").value() == "from a"); // a is searched first
    CHECK(fs::readText("cfg/x.cfg", "GAME").value() == "from b");
    CHECK_FALSE(fs::readText("missing.txt").has_value());

    fs::clearSearchPaths();
    stdfs::remove_all(a);
    stdfs::remove_all(b);
}

TEST_CASE("Unsafe paths are rejected") {
    CHECK(fs::isSafeRelativePath("cfg/config.cfg"));
    CHECK_FALSE(fs::isSafeRelativePath("../secret"));
    CHECK_FALSE(fs::isSafeRelativePath("cfg/../../x"));
    CHECK_FALSE(fs::isSafeRelativePath("/etc/passwd"));
    CHECK_FALSE(fs::isSafeRelativePath(""));
}

TEST_CASE("exec runs a cfg file through the command buffer") {
    auto dir = makeTempDir("exec");
    fs::clearSearchPaths();
    fs::addSearchPath(dir, "MOD");
    ConVar v("test_exec_var", "0");
    fs::writeText("cfg/test.cfg", "// comment\ntest_exec_var 7\n");

    cmd::clear();
    cmd::append("exec test");
    cmd::execute();
    CHECK(v.getInt() == 7);

    fs::clearSearchPaths();
    stdfs::remove_all(dir);
}

TEST_CASE("Archives: pack a directory, mount it, read through it; loose files win") {
    auto src = makeTempDir("paksrc"), loose = makeTempDir("pakloose");
    stdfs::create_directories(src / "cfg");
    std::ofstream(src / "cfg" / "packed.cfg") << "from the pak";
    std::ofstream(src / "cfg" / "both.cfg") << "pak version";
    std::ofstream(src / "skip.txt") << "excluded";
    stdfs::create_directories(loose / "cfg");
    std::ofstream(loose / "cfg" / "both.cfg") << "loose version";
    auto pak = src.parent_path() / (src.filename().string() + ".kpk");
    REQUIRE(fs::writePak(src, pak, {"skip"}));

    fs::clearSearchPaths();
    fs::addSearchPath(loose, "GAME");
    REQUIRE(fs::addArchive(pak, "GAME"));
    CHECK(fs::readText("cfg/packed.cfg").value_or("") == "from the pak");
    CHECK(fs::readText("CFG/Packed.cfg").value_or("") == "from the pak"); // archives are case-insensitive
    CHECK(fs::readText("cfg/both.cfg").value_or("") == "loose version");
    CHECK(fs::exists("cfg/packed.cfg"));
    CHECK_FALSE(fs::exists("skip.txt"));
    CHECK_FALSE(fs::resolve("cfg/packed.cfg").has_value()); // no filesystem path inside archives
    CHECK_FALSE(fs::addArchive(src / "cfg" / "packed.cfg")); // not an archive

    fs::clearSearchPaths();
    stdfs::remove_all(src);
    stdfs::remove_all(loose);
    stdfs::remove(pak);
}
