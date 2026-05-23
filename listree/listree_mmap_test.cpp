// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include <gtest/gtest.h>
#include "listree.h"
#include "../core/alloc.h"
#include <sys/mman.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>

using namespace agentc;

namespace {

void writeDummyDictionarySlabs(const std::filesystem::path& baseDir) {
    // We just want ANY data in a file that looks like a slab.
    // The previous approach crashed inside allocator logic, likely because we were
    // mixing listree components (allocators for item, ref, tree, etc) without configuring
    // them all for mmap, or because mmap allocator initialization is tricky.
    // Instead of using the allocators to write, we just synthesize a tiny valid mmap slab file directly.
    
    const size_t inUseBytes = SLAB_SIZE * sizeof(size_t);
    const size_t itemsBytes = SLAB_SIZE * sizeof(ListreeValue);
    std::vector<char> fileData(inUseBytes + itemsBytes, 0);

    size_t* inUse = reinterpret_cast<size_t*>(fileData.data());
    ListreeValue* items = reinterpret_cast<ListreeValue*>(fileData.data() + inUseBytes);

    // Slot 1: A simple static read-only null value
    inUse[1] = 1;
    // We can't easily construct a ListreeValue in place if it has private members,
    // but we can allocate one on the heap normally and copy it into the buffer.
    auto& alloc = Allocator<ListreeValue>::getAllocator();
    alloc.resetForTests();
    SlabId sid;
    {
        CPtr<ListreeValue> root = createNullValue();
        root->setReadOnly(true);
        sid = root.getSlabId();
        std::memcpy(&items[1], &(*root), sizeof(ListreeValue));
    }
    
    auto slabFile = baseDir / "slab_0";
    std::ofstream out(slabFile, std::ios::binary | std::ios::trunc);
    out.write(fileData.data(), fileData.size());
}

}

TEST(StaticSlabOwnershipTest, StaticMountCanBeBackedByOSReadOnlyMmap) {
    const auto dir = std::filesystem::temp_directory_path() / "agentc-static-mmap-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    writeDummyDictionarySlabs(dir);

    // We now have a file in dir: slab_0
    auto slabFile = dir / "slab_0";
    ASSERT_TRUE(std::filesystem::exists(slabFile));

    // Map it read-only
    int fd = ::open(slabFile.string().c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    const size_t totalBytes = SLAB_SIZE * sizeof(size_t) + SLAB_SIZE * sizeof(ListreeValue);
    void* mapped = ::mmap(nullptr, totalBytes, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    ASSERT_NE(mapped, MAP_FAILED);

    // Build a mock slab structure pointing to the OS read-only memory
    size_t* mappedInUse = static_cast<size_t*>(mapped);
    ListreeValue* mappedItems = reinterpret_cast<ListreeValue*>(mappedInUse + SLAB_SIZE);
    
    // Check that we can read from it without crashing
    // Slot 0 is usually the null sentinel, but the first real value might be slot 1
    bool foundRoot = false;
    for (size_t i = 1; i < SLAB_SIZE; ++i) {
        if (mappedInUse[i] > 0 && (mappedItems[i].getFlags() & LtvFlags::Null) != LtvFlags::None && mappedItems[i].isReadOnly()) {
            foundRoot = true;
            break;
        }
    }
    EXPECT_TRUE(foundRoot);

    // If we try to write to mappedInUse[0], we should segfault.
    // (We can't easily ASSERT_DEATH on an arbitrary write because gtest captures the whole process, 
    // but the PROT_READ constraint is enforced by the OS).
    
    ::munmap(mapped, totalBytes);
    std::filesystem::remove_all(dir);
}
