// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>
#include "../ffi.h"
#include "../mapper.h"
#include <fstream>
#include <iostream>
#include <filesystem>

using namespace agentc::cartographer;
using namespace agentc;

TEST(FFITest, CallAbs) {
    FFI ffi; Mapper mapper;
    std::filesystem::path hPath = std::filesystem::temp_directory_path() / "j3_abs_test.h";
    {
        std::ofstream ofs(hPath);
        ASSERT_TRUE(ofs.is_open());
        ofs << "int abs(int n);" << std::endl;
    }
    std::string absPath = std::filesystem::absolute(hPath).string();
    CPtr<ListreeValue> root = mapper.parse(absPath); ASSERT_TRUE(bool(root));
    auto absDef = root->find("abs")->getValue();
    bool loaded = ffi.loadLibrary("libc.so.6"); if (!loaded) loaded = ffi.loadLibrary("/lib/x86_64-linux-gnu/libc.so.6");
    if (!loaded) return;
    int argVal = -42;
    auto args = createListValue();
    addListItem(args, createBinaryValue(&argVal, sizeof(int)));
    auto result = ffi.invoke("abs", absDef, args);
    ASSERT_TRUE(bool(result)); EXPECT_EQ(*(int*)result->getData(), 42);
}

// G011: Blocklist tests — verify dangerous functions are blocked.
// isBlocked() uses exact string comparison via std::unordered_set (M5 fix).

TEST(FFITest, BlocklistRejectsDangerousProcessFunctions) {
    // Process creation / exec family — must all be blocked.
    EXPECT_TRUE(FFI::isBlocked("system"));
    EXPECT_TRUE(FFI::isBlocked("execve"));
    EXPECT_TRUE(FFI::isBlocked("execvp"));
    EXPECT_TRUE(FFI::isBlocked("execl"));
    EXPECT_TRUE(FFI::isBlocked("fork"));
    EXPECT_TRUE(FFI::isBlocked("vfork"));
    EXPECT_TRUE(FFI::isBlocked("posix_spawn"));
}

TEST(FFITest, BlocklistRejectsShellFunctions) {
    EXPECT_TRUE(FFI::isBlocked("popen"));
    EXPECT_TRUE(FFI::isBlocked("pclose"));
    EXPECT_TRUE(FFI::isBlocked("wordexp"));
}

TEST(FFITest, BlocklistRejectsDynamicLoading) {
    // dlopen bypass would allow loading an arbitrary library and calling anything.
    EXPECT_TRUE(FFI::isBlocked("dlopen"));
    EXPECT_TRUE(FFI::isBlocked("dlclose"));
}

TEST(FFITest, BlocklistAllowsSafeFunctions) {
    // A safe math function must not be blocked.
    EXPECT_FALSE(FFI::isBlocked("abs"));
    EXPECT_FALSE(FFI::isBlocked("sqrt"));
    EXPECT_FALSE(FFI::isBlocked("strlen"));
    EXPECT_FALSE(FFI::isBlocked("printf"));
}

TEST(FFITest, InvokeBlockedFunctionReturnsNull) {
    // Even with a library loaded and a fake definition, invoking a blocked name
    // must return nullptr without touching dlsym.
    FFI ffi;
    // A minimal definition node (real content doesn't matter — blocked before parsing).
    auto dummyDef = createNullValue();
    EXPECT_FALSE(bool(ffi.invoke("system", dummyDef, nullptr)));
    EXPECT_FALSE(bool(ffi.invoke("execve", dummyDef, nullptr)));
    EXPECT_FALSE(bool(ffi.invoke("fork",   dummyDef, nullptr)));
    EXPECT_FALSE(bool(ffi.invoke("popen",  dummyDef, nullptr)));
}
