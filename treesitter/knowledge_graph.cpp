// knowledge_graph.cpp — G094.5 Persistent Knowledge Graph implementation
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "knowledge_graph.h"

namespace agentc {
namespace knowledge {

// Helper: create a string value
static CPtr<ListreeValue> str(const std::string& s) {
    return createStringValue(s);
}

// Helper: get a named child from a tree node
static CPtr<ListreeValue> getField(CPtr<ListreeValue> tree, const std::string& name) {
    if (!tree) return nullptr;
    auto item = tree->find(name);
    if (!item || !item->getValue()) return nullptr;
    return item->getValue(false, false);
}

// Helper: count list items
static size_t listCount(CPtr<ListreeValue> list) {
    if (!list || !list->isListMode()) return 0;
    size_t count = 0;
    list->forEachList([&](CPtr<ListreeValueRef>&) { ++count; }, false);
    return count;
}

CPtr<ListreeValue> KnowledgeGraph::create() {
    auto graph = createNullValue();
    addNamedItem(graph, "nodes", createNullValue());
    addNamedItem(graph, "edges", createListValue());
    return graph;
}

void KnowledgeGraph::addNode(CPtr<ListreeValue> graph,
                              const std::string& name,
                              CPtr<ListreeValue> properties) {
    if (!graph) return;
    auto nodesItem = graph->find("nodes", true);
    if (!nodesItem || !nodesItem->getValue()) return;
    auto nodes = nodesItem->getValue(false, false);
    if (!nodes) {
        nodes = createNullValue();
        graph->addItemValue(nodesItem, nodes, false);
    }

    // Check if node already exists
    auto existingItem = nodes->find(name, true);
    if (existingItem && existingItem->getValue()) {
        // Merge properties into existing node
        if (properties && !properties->isListMode()) {
            auto existing = existingItem->getValue(false, false);
            if (existing) {
                properties->forEachTree([&](const std::string& key, CPtr<ListreeItem>& item) {
                    if (item && item->getValue()) {
                        addNamedItem(existing, key, item->getValue(false, false));
                    }
                });
            }
        }
    } else {
        // Create new node
        auto nodeData = properties ? properties : createNullValue();
        if (!existingItem) existingItem = nodes->find(name, true);
        if (existingItem) nodes->addItemValue(existingItem, nodeData, false);
    }
}

void KnowledgeGraph::addEdge(CPtr<ListreeValue> graph,
                              const std::string& from,
                              const std::string& relation,
                              const std::string& to,
                              CPtr<ListreeValue> properties) {
    if (!graph) return;
    auto edgesItem = graph->find("edges", true);
    if (!edgesItem || !edgesItem->getValue()) return;
    auto edges = edgesItem->getValue(false, false);
    if (!edges || !edges->isListMode()) return;

    auto edge = createNullValue();
    addNamedItem(edge, "from", str(from));
    addNamedItem(edge, "relation", str(relation));
    addNamedItem(edge, "to", str(to));
    if (properties) {
        addNamedItem(edge, "properties", properties);
    }
    addListItem(edges, edge);
}

CPtr<ListreeValue> KnowledgeGraph::getNode(CPtr<ListreeValue> graph,
                                             const std::string& name) {
    if (!graph) return nullptr;
    auto nodes = getField(graph, "nodes");
    if (!nodes) return nullptr;
    auto item = nodes->find(name);
    if (!item || !item->getValue()) return nullptr;
    return item->getValue(false, false);
}

CPtr<ListreeValue> KnowledgeGraph::queryEdges(CPtr<ListreeValue> graph,
                                                const std::string& from,
                                                const std::string& relation,
                                                const std::string& to) {
    auto result = createListValue();
    auto edges = getField(graph, "edges");
    if (!edges || !edges->isListMode()) return result;

    edges->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (!ref || !ref->getValue()) return;
        auto edge = ref->getValue();

        std::string eFrom = getField(edge, "from") ?
            std::string(static_cast<char*>(getField(edge, "from")->getData()),
                        getField(edge, "from")->getLength()) : "";
        std::string eRel = getField(edge, "relation") ?
            std::string(static_cast<char*>(getField(edge, "relation")->getData()),
                        getField(edge, "relation")->getLength()) : "";
        std::string eTo = getField(edge, "to") ?
            std::string(static_cast<char*>(getField(edge, "to")->getData()),
                        getField(edge, "to")->getLength()) : "";

        bool match = true;
        if (!from.empty() && from != eFrom) match = false;
        if (!relation.empty() && relation != eRel) match = false;
        if (!to.empty() && to != eTo) match = false;

        if (match) addListItem(result, edge);
    }, false);

    return result;
}

CPtr<ListreeValue> KnowledgeGraph::listNodes(CPtr<ListreeValue> graph) {
    auto result = createListValue();
    auto nodes = getField(graph, "nodes");
    if (!nodes || nodes->isListMode()) return result;

    nodes->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
        if (item && item->getValue()) {
            addListItem(result, str(name));
        }
    });

    return result;
}

CPtr<ListreeValue> KnowledgeGraph::listEdges(CPtr<ListreeValue> graph) {
    auto edges = getField(graph, "edges");
    if (!edges || !edges->isListMode()) return createListValue();
    // Return a copy
    auto result = createListValue();
    edges->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (ref && ref->getValue()) addListItem(result, ref->getValue());
    }, false);
    return result;
}

bool KnowledgeGraph::removeNode(CPtr<ListreeValue> graph, const std::string& name) {
    if (!graph) return false;
    auto nodes = getField(graph, "nodes");
    if (!nodes) return false;
    auto removed = nodes->remove(name);
    if (!removed) return false;

    // Also remove all edges connected to this node
    removeEdges(graph, name, "", "");
    removeEdges(graph, "", "", name);
    return true;
}

size_t KnowledgeGraph::removeEdges(CPtr<ListreeValue> graph,
                                     const std::string& from,
                                     const std::string& relation,
                                     const std::string& to) {
    auto edges = getField(graph, "edges");
    if (!edges || !edges->isListMode()) return 0;

    // Collect matching edges, then rebuild the list without them
    size_t removed = 0;
    auto remaining = createListValue();

    edges->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (!ref || !ref->getValue()) return;
        auto edge = ref->getValue();

        std::string eFrom = getField(edge, "from") ?
            std::string(static_cast<char*>(getField(edge, "from")->getData()),
                        getField(edge, "from")->getLength()) : "";
        std::string eRel = getField(edge, "relation") ?
            std::string(static_cast<char*>(getField(edge, "relation")->getData()),
                        getField(edge, "relation")->getLength()) : "";
        std::string eTo = getField(edge, "to") ?
            std::string(static_cast<char*>(getField(edge, "to")->getData()),
                        getField(edge, "to")->getLength()) : "";

        bool match = true;
        if (!from.empty() && from != eFrom) match = false;
        if (!relation.empty() && relation != eRel) match = false;
        if (!to.empty() && to != eTo) match = false;

        if (match) {
            ++removed;
        } else {
            addListItem(remaining, edge);
        }
    }, false);

    // Replace edges list
    auto edgesItem = graph->find("edges", true);
    if (edgesItem) {
        // Clear existing list and repopulate
        while (edges->get(true, true)) {}
        remaining->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) addListItem(edges, ref->getValue());
        }, false);
    }

    return removed;
}

bool KnowledgeGraph::hasNode(CPtr<ListreeValue> graph, const std::string& name) {
    auto node = getNode(graph, name);
    return static_cast<bool>(node);
}

size_t KnowledgeGraph::nodeCount(CPtr<ListreeValue> graph) {
    auto nodes = getField(graph, "nodes");
    if (!nodes || nodes->isListMode()) return 0;
    size_t count = 0;
    nodes->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) {
        if (item && item->getValue()) ++count;
    });
    return count;
}

size_t KnowledgeGraph::edgeCount(CPtr<ListreeValue> graph) {
    auto edges = getField(graph, "edges");
    return listCount(edges);
}

} // namespace knowledge
} // namespace agentc
