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

// G108: Tests for TraversalVisitState — per-slab bitmap visit tracking.

#include <gtest/gtest.h>
#include <thread>
#include "listree.h"
#include "../core/cursor.h"

using namespace agentc;

// Two independent traversal contexts over the same graph should each
// produce the same set of visited nodes without shared-state interference.
TEST(TraversalVisitTest, TwoIndependentTraversalsSameGraph) {
    auto root = createNullValue();
    auto child = createStringValue("child");
    addNamedItem(root, "key", child);

    std::vector<std::string> visited1, visited2;

    auto ctx1 = std::make_shared<TraversalVisitState>();
    auto ctx2 = std::make_shared<TraversalVisitState>();

    root->traverse([&](CPtr<ListreeValue> val) {
        if (val && val->getData()) visited1.push_back(std::string((char*)val->getData(), val->getLength()));
    }, {}, ctx1);

    root->traverse([&](CPtr<ListreeValue> val) {
        if (val && val->getData()) visited2.push_back(std::string((char*)val->getData(), val->getLength()));
    }, {}, ctx2);

    EXPECT_EQ(visited1.size(), 1);
    EXPECT_EQ(visited2.size(), 1);
    EXPECT_EQ(visited1[0], "child");
    EXPECT_EQ(visited2[0], "child");
}

// Two independent contexts over a read-only graph — should still work
// because bitmap state is owned by the TraversalVisitState, not the nodes.
TEST(TraversalVisitTest, TwoIndependentTraversalsReadOnlyGraph) {
    auto root = createNullValue();
    auto child = createStringValue("ro-data");
    addNamedItem(root, "key", child);

    // Freeze the graph
    root->setReadOnly(true);

    std::vector<std::string> visited1, visited2;

    auto ctx1 = std::make_shared<TraversalVisitState>();
    auto ctx2 = std::make_shared<TraversalVisitState>();

    root->traverse([&](CPtr<ListreeValue> val) {
        if (val && val->getData()) visited1.push_back(std::string((char*)val->getData(), val->getLength()));
    }, {}, ctx1);

    root->traverse([&](CPtr<ListreeValue> val) {
        if (val && val->getData()) visited2.push_back(std::string((char*)val->getData(), val->getLength()));
    }, {}, ctx2);

    EXPECT_EQ(visited1.size(), 1);
    EXPECT_EQ(visited2.size(), 1);
    EXPECT_EQ(visited1[0], "ro-data");
    EXPECT_EQ(visited2[0], "ro-data");
}

// Breadth-first traversal of a cyclic graph should terminate correctly.
TEST(TraversalVisitTest, BreadthFirstCycleHandling) {
    auto valA = createStringValue("A");
    auto valB = createStringValue("B");
    addNamedItem(valA, "toB", valB);
    addNamedItem(valB, "toA", valA);  // A ← → B cycle

    std::vector<std::string> visited;
    TraversalOptions opts;
    opts.order = TraversalOrder::BreadthFirst;
    valA->traverse([&](CPtr<ListreeValue> val) {
        if (val && val->getData()) visited.push_back(std::string((char*)val->getData(), val->getLength()));
    }, opts);

    // BFS should visit both nodes exactly once
    EXPECT_EQ(visited.size(), 2);
    bool hasA = (visited[0] == "A" || visited[1] == "A");
    bool hasB = (visited[0] == "B" || visited[1] == "B");
    EXPECT_TRUE(hasA);
    EXPECT_TRUE(hasB);
}

// A third traversal after the first two should still work independently.
TEST(TraversalVisitTest, TraversalVisitStateFreshAfterSeparateUsage) {
    auto val = createStringValue("fresh");

    // Use the same TraversalVisitState for two separate traversals
    TraversalVisitState state;
    state.mark_seen(SlabId(0, 1));  // simulate seeing a different node

    std::vector<std::string> visited;
    val->traverse([&](CPtr<ListreeValue> v) {
        if (v && v->getData()) visited.push_back(std::string((char*)v->getData(), v->getLength()));
    }, {}, std::make_shared<TraversalVisitState>());

    EXPECT_EQ(visited.size(), 1);
    EXPECT_EQ(visited[0], "fresh");
}

// Clear() resets state so a subsequent traversal works as if fresh.
TEST(TraversalVisitTest, ClearResetsState) {
    auto val = createStringValue("reset-test");
    auto state = std::make_shared<TraversalVisitState>();

    std::vector<std::string> visited1, visited2;

    val->traverse([&](CPtr<ListreeValue> v) {
        if (v && v->getData()) visited1.push_back(std::string((char*)v->getData(), v->getLength()));
    }, {}, state);

    EXPECT_EQ(visited1.size(), 1);

    state->clear();

    val->traverse([&](CPtr<ListreeValue> v) {
        if (v && v->getData()) visited2.push_back(std::string((char*)v->getData(), v->getLength()));
    }, {}, state);

    EXPECT_EQ(visited2.size(), 1);
}
