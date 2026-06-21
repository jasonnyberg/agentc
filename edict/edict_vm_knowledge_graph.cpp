// edict_vm_knowledge_graph.cpp — G094.5 Knowledge Graph VM operations
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "edict_vm.h"
#include "../treesitter/knowledge_graph.h"
#include "../listree/listree.h"

namespace agentc::edict {

static bool kgValueToString(CPtr<agentc::ListreeValue> v, std::string& out) {
    if (!v || !v->getData() || v->getLength() == 0) return false;
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return false;
    out.assign(static_cast<char*>(v->getData()), v->getLength());
    return true;
}

void EdictVM::op_KG_CREATE() {
    pushData(agentc::knowledge::KnowledgeGraph::create());
}

void EdictVM::op_KG_ADD_NODE() {
    // Stack: graph name [properties?] -- graph
    // The properties argument is optional. We check if there's a third
    // value on the stack that's a tree (not a string).
    auto nameVal = popData();
    auto graphVal = peekData();

    std::string name;
    if (!kgValueToString(nameVal, name)) {
        setError("KG_ADD_NODE expects name string");
        return;
    }
    if (!graphVal) {
        setError("KG_ADD_NODE expects graph");
        return;
    }

    // Pop the graph, then check if the next value is properties
    popData(); // remove graph from stack
    auto maybeProps = peekData();
    CPtr<agentc::ListreeValue> props = nullptr;

    // If the next value is a tree-mode (not string, not list), treat as properties
    if (maybeProps && maybeProps->getData() &&
        (maybeProps->getFlags() & agentc::LtvFlags::Binary) == agentc::LtvFlags::None &&
        !maybeProps->isListMode() &&
        maybeProps->getLength() == 0) {
        // It's a tree/null — could be properties or just a null
        // Check if it has any named children
        bool hasChildren = false;
        maybeProps->forEachTree([&](const std::string&, CPtr<ListreeItem>&) {
            hasChildren = true;
        });
        if (hasChildren) {
            props = maybeProps;
            popData(); // consume properties
        }
    }

    agentc::knowledge::KnowledgeGraph::addNode(graphVal, name, props);
    pushData(graphVal);
}

void EdictVM::op_KG_ADD_EDGE() {
    // Stack: graph from relation to -- graph
    auto toVal = popData();
    auto relVal = popData();
    auto fromVal = popData();
    auto graphVal = popData();

    std::string from, relation, to;
    if (!kgValueToString(fromVal, from) ||
        !kgValueToString(relVal, relation) ||
        !kgValueToString(toVal, to)) {
        setError("KG_ADD_EDGE expects from, relation, to strings");
        return;
    }
    if (!graphVal) {
        setError("KG_ADD_EDGE expects graph");
        return;
    }

    agentc::knowledge::KnowledgeGraph::addEdge(graphVal, from, relation, to);
    pushData(graphVal);
}

void EdictVM::op_KG_GET_NODE() {
    auto nameVal = popData();
    auto graphVal = popData();

    std::string name;
    if (!kgValueToString(nameVal, name)) {
        setError("KG_GET_NODE expects name string");
        return;
    }
    if (!graphVal) {
        setError("KG_GET_NODE expects graph");
        return;
    }

    auto node = agentc::knowledge::KnowledgeGraph::getNode(graphVal, name);
    if (node) {
        // Clear the Null flag so empty nodes serialize as {} not null
        node->clearFlags(agentc::LtvFlags::Null);
        pushData(node);
    } else {
        pushData(agentc::createNullValue());
    }
}

void EdictVM::op_KG_QUERY() {
    auto toVal = popData();
    auto relVal = popData();
    auto fromVal = popData();
    auto graphVal = popData();

    std::string from, relation, to;
    if (!kgValueToString(fromVal, from) ||
        !kgValueToString(relVal, relation) ||
        !kgValueToString(toVal, to)) {
        setError("KG_QUERY expects from, relation, to strings");
        return;
    }
    if (!graphVal) {
        setError("KG_QUERY expects graph");
        return;
    }

    pushData(agentc::knowledge::KnowledgeGraph::queryEdges(graphVal, from, relation, to));
}

void EdictVM::op_KG_LIST_NODES() {
    auto graphVal = popData();
    if (!graphVal) {
        setError("KG_LIST_NODES expects graph");
        return;
    }
    pushData(agentc::knowledge::KnowledgeGraph::listNodes(graphVal));
}

void EdictVM::op_KG_LIST_EDGES() {
    auto graphVal = popData();
    if (!graphVal) {
        setError("KG_LIST_EDGES expects graph");
        return;
    }
    pushData(agentc::knowledge::KnowledgeGraph::listEdges(graphVal));
}

} // namespace agentc::edict
