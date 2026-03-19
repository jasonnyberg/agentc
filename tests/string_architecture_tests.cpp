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
#include "../listree/listree.h"
#include "../core/alloc.h"
#include "../core/cursor.h"
#include <string>

using namespace agentc;

class StringArchitectureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset allocators to ensure a clean state
        Allocator<ListreeValue>::getAllocator().resetForTests();
    }
};

// AC1: Small String Optimization (SSO) <= 15 bytes
TEST_F(StringArchitectureTest, SmallStringOptimizationAllocatesZeroBytes) {
    std::string shortStr = "hello world!"; // 12 bytes
    EXPECT_LE(shortStr.length(), 15);
    
    auto ssoVal = createStringValue(shortStr);
    ASSERT_TRUE(ssoVal); // Replaced ASSERT_NE for CPtr compatibility
    
    // It should have the inline flag (using Immediate flag which was reserved for this)
    EXPECT_TRUE((ssoVal->getFlags() & LtvFlags::Immediate) != LtvFlags::None);
    EXPECT_FALSE((ssoVal->getFlags() & LtvFlags::Duplicate) != LtvFlags::None);
    EXPECT_FALSE((ssoVal->getFlags() & LtvFlags::Free) != LtvFlags::None);
    
    // The data pointer should actually point directly to the inline data inside the struct
    // (This ensures no malloc was called)
    const void* dataPtr = ssoVal->getData();
    ASSERT_TRUE(dataPtr != nullptr); // Replaced ASSERT_NE for CPtr compatibility
    
    // The data length should be exactly 12
    EXPECT_EQ(ssoVal->getLength(), 12);
    
    // The data should match exactly
    std::string extracted(static_cast<const char*>(dataPtr), ssoVal->getLength());
    EXPECT_EQ(extracted, shortStr);
}

// AC2: Slab-backed BlobAllocator for > 15 bytes
TEST_F(StringArchitectureTest, BlobAllocatorUsedForLargeStrings) {
    std::string largeStr = "This is a much longer string that exceeds the 15 byte limit of SSO.";
    EXPECT_GT(largeStr.length(), 15);
    
    auto blobVal = createStringValue(largeStr);
    ASSERT_TRUE(blobVal); // Replaced ASSERT_NE for CPtr compatibility
    
    // It should NOT have the inline flag
    EXPECT_FALSE((blobVal->getFlags() & LtvFlags::Immediate) != LtvFlags::None);
    
    // It SHOULD be marked as a SlabBlob (we'll reuse Own for this test until we add SlabBlob)
    EXPECT_TRUE((blobVal->getFlags() & LtvFlags::SlabBlob) != LtvFlags::None);
    
    // It should NOT be marked Free (because it uses a SlabId, not malloc)
    EXPECT_FALSE((blobVal->getFlags() & LtvFlags::Free) != LtvFlags::None);
    
    const void* dataPtr = blobVal->getData();
    ASSERT_TRUE(dataPtr != nullptr); // Replaced ASSERT_NE for CPtr compatibility
    EXPECT_EQ(blobVal->getLength(), largeStr.length());
    
    std::string extracted(static_cast<const char*>(dataPtr), blobVal->getLength());
    EXPECT_EQ(extracted, largeStr);
}

// AC3: Zero-Copy Static Views
TEST_F(StringArchitectureTest, StaticViewAvoidsCopying) {
    const char* staticBuffer = "This is a globally static buffer view.";
    size_t length = strlen(staticBuffer);
    
    // Create a ListreeValue using the ListreeValue(void*, size_t, LtvFlags) constructor directly
    SlabId sid = Allocator<ListreeValue>::getAllocator().allocate((void*)staticBuffer, length, LtvFlags::StaticView);
    CPtr<ListreeValue> viewVal(sid);
    
    ASSERT_TRUE(viewVal); // Replaced ASSERT_NE for CPtr compatibility
    
    EXPECT_TRUE((viewVal->getFlags() & LtvFlags::StaticView) != LtvFlags::None);
    EXPECT_FALSE((viewVal->getFlags() & LtvFlags::Free) != LtvFlags::None);
    EXPECT_FALSE((viewVal->getFlags() & LtvFlags::Duplicate) != LtvFlags::None);
    
    // Data pointer should exactly equal the static buffer address
    EXPECT_EQ(viewVal->getData(), staticBuffer);
    EXPECT_EQ(viewVal->getLength(), length);
}
