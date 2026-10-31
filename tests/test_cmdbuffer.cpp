#include "core/cmdbuffer.h"
#include "core/cvar.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("splitStatements handles ; newlines quotes and comments") {
    auto s = cmd::splitStatements("echo a; echo \"b;c\"\n// comment; ignored\nbind x \"+jump\" // trailing\n\n");
    REQUIRE(s.size() == 3);
    CHECK(s[0] == "echo a");
    CHECK(s[1] == "echo \"b;c\"");
    CHECK(s[2] == "bind x \"+jump\"");
}

TEST_CASE("tokenize strips quotes") {
    auto t = cmd::tokenize("  bind  \"mouse1\" \"+attack; say hi\"  x");
    REQUIRE(t.size() == 4);
    CHECK(t[0] == "bind");
    CHECK(t[1] == "mouse1");
    CHECK(t[2] == "+attack; say hi");
    CHECK(t[3] == "x");
    CHECK(cmd::tokenize("   ").empty());
}

TEST_CASE("Commands receive arguments and cvars can be set") {
    std::vector<std::string> got;
    std::string argString;
    ConCommand c("test_cmd_args", [&](const CmdArgs& a) {
        got = a.tokens();
        argString = a.argString();
    });
    ConVar v("test_cmd_var", "1");

    cmd::executeNow("test_cmd_args one \"two three\"; test_cmd_var 42");
    REQUIRE(got.size() == 3);
    CHECK(got[1] == "one");
    CHECK(got[2] == "two three");
    CHECK(argString == "one \"two three\"");
    CHECK(v.getInt() == 42);
}

TEST_CASE("Buffered execution, wait, and aliases") {
    cmd::clear();
    int count = 0;
    ConCommand c("test_cmd_count", [&](const CmdArgs&) { ++count; });

    cmd::append("test_cmd_count; wait; test_cmd_count");
    cmd::execute();
    CHECK(count == 1);
    cmd::execute(); // consumes the wait frame
    cmd::execute();
    CHECK(count == 2);

    cmd::executeNow("alias test_alias_twice \"test_cmd_count; test_cmd_count\"");
    cmd::append("test_alias_twice");
    cmd::execute();
    CHECK(count == 4);
}

TEST_CASE("insert runs before already-queued text") {
    cmd::clear();
    std::string order;
    ConCommand a("test_order_a", [&](const CmdArgs&) { order += 'a'; });
    ConCommand b("test_order_b", [&](const CmdArgs&) { order += 'b'; });
    cmd::append("test_order_a");
    cmd::insert("test_order_b");
    cmd::execute();
    CHECK(order == "ba");
}

TEST_CASE("Recursive alias does not hang") {
    cmd::clear();
    cmd::executeNow("alias test_loop test_loop");
    cmd::append("test_loop");
    cmd::execute();
    CHECK(cmd::empty());
}

TEST_CASE("toggle and incrementvar") {
    ConVar v("test_toggle_var", "0");
    cmd::executeNow("toggle test_toggle_var");
    CHECK(v.getInt() == 1);
    cmd::executeNow("toggle test_toggle_var 1 2 3");
    CHECK(v.getInt() == 2);
    cmd::executeNow("toggle test_toggle_var 1 2 3");
    CHECK(v.getInt() == 3);
    cmd::executeNow("toggle test_toggle_var 1 2 3");
    CHECK(v.getInt() == 1);
    cmd::executeNow("incrementvar test_toggle_var 0 2 1");
    CHECK(v.getInt() == 2);
    cmd::executeNow("incrementvar test_toggle_var 0 2 1");
    CHECK(v.getInt() == 0);
}
