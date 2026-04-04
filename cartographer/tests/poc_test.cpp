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
#include "../ltv_api.h"
#include "../mapper.h"
#include "libagentthreads_poc.h"
#include "../../core/cursor.h"
#include "../../listree/listree.h"
#include <iostream>
#include <filesystem>

using namespace agentc::cartographer;
using namespace agentc;

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

TEST(PoCTest, CallAddFromLib) {
    FFI ffi;
    Mapper mapper;

    std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    std::filesystem::path buildDir(TEST_BUILD_DIR);
    
    std::filesystem::path headerPath = sourceDir / "libagentmath_poc.h";
    std::filesystem::path libPath = buildDir / "libagentmath_poc.so";

    // 1. Parse the header
    std::cout << "Parsing header: " << headerPath << std::endl;
    CPtr<ListreeValue> root = mapper.parse(headerPath.string());
    ASSERT_TRUE(bool(root)) << "Failed to parse header";

    // Debug: print keys in root
    // root->forEachTree([](const std::string& key, CPtr<ListreeItem>& item) {
    //     std::cout << "Found symbol: " << key << std::endl;
    // });

    // 2. Find 'add' function definition
    auto addItem = root->find("add");
    ASSERT_TRUE(bool(addItem)) << "Could not find 'add' function in header";
    CPtr<ListreeValue> addDef = addItem->getValue();
    ASSERT_TRUE(bool(addDef));

    // 3. Load the library
    std::cout << "Loading library: " << libPath << std::endl;
    bool loaded = ffi.loadLibrary(libPath.string());
    ASSERT_TRUE(loaded) << "Failed to load library: " << libPath;

    // 4. Prepare arguments
    int a = 10;
    int b = 32;
    auto args = createListValue();
    addListItem(args, createBinaryValue(&a, sizeof(int)));
    addListItem(args, createBinaryValue(&b, sizeof(int)));

    // 5. Invoke
    CPtr<ListreeValue> result = ffi.invoke("add", addDef, args);
    ASSERT_TRUE(bool(result)) << "FFI invoke failed";

    // 6. Verify result
    ASSERT_TRUE(bool(result)) << "FFI invoke returned null";
    std::string resultStr(static_cast<const char*>(result->getData()), result->getLength());
    std::cout << "Result of add(" << a << ", " << b << ") = " << resultStr << std::endl;
    EXPECT_EQ(resultStr, "42");
}

namespace {

ltv identity_ltv(ltv value) {
    return value;
}

} // namespace

TEST(PoCTest, ThreadHelperRejectsIteratorValTransfers) {
    Cursor* liveCursor = new Cursor(createListValue());
    CPtr<ListreeValue> iteratorValue = createCursorValue(liveCursor);
    ASSERT_TRUE((bool)iteratorValue);

    ltv owned = iteratorValue.release();

    agentc_shared_value* cell = agentc_shared_create_ltv(owned);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(agentc_shared_read_ltv(cell), 0u);
    agentc_shared_destroy(cell);

    agentc_thread_handle* handle = agentc_thread_spawn_ltv(identity_ltv, owned);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(agentc_thread_join_ltv(handle), 0u);
    agentc_thread_destroy(handle);

    ltv_unref(owned);
}
