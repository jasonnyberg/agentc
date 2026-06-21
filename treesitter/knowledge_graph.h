// knowledge_graph.h — G094.5 Persistent Knowledge Graph
//
// A lightweight knowledge graph backed by Listree trees, queryable
// from Edict and composable with miniKanren relational queries.
//
// The graph uses a tree-mode ListreeValue as the backing store:
//   - "nodes" : tree of node_name -> {properties...}
//   - "edges" : list of {from, to, relation, properties}
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <string>
#include "../listree/listree.h"

namespace agentc {
namespace knowledge {

/// A persistent knowledge graph backed by Listree.
/// All operations return new Listree values; the graph itself is
/// mutated in-place on the backing Listree tree.
class KnowledgeGraph {
public:
    /// Create a new empty knowledge graph.
    static CPtr<ListreeValue> create();

    /// Add a node to the graph with optional properties (tree-mode ListreeValue).
    /// If the node already exists, properties are merged.
    static void addNode(CPtr<ListreeValue> graph,
                        const std::string& name,
                        CPtr<ListreeValue> properties = nullptr);

    /// Add an edge: from --[relation]--> to, with optional properties.
    static void addEdge(CPtr<ListreeValue> graph,
                        const std::string& from,
                        const std::string& relation,
                        const std::string& to,
                        CPtr<ListreeValue> properties = nullptr);

    /// Look up a node by name. Returns the node's property tree or nullptr.
    static CPtr<ListreeValue> getNode(CPtr<ListreeValue> graph,
                                       const std::string& name);

    /// Query edges matching a pattern. Any empty string matches all.
    /// Returns a list of edge trees: {from, to, relation, properties}.
    static CPtr<ListreeValue> queryEdges(CPtr<ListreeValue> graph,
                                          const std::string& from = "",
                                          const std::string& relation = "",
                                          const std::string& to = "");

    /// Get all node names.
    static CPtr<ListreeValue> listNodes(CPtr<ListreeValue> graph);

    /// Get all edges as a list.
    static CPtr<ListreeValue> listEdges(CPtr<ListreeValue> graph);

    /// Remove a node and all edges connected to it.
    static bool removeNode(CPtr<ListreeValue> graph, const std::string& name);

    /// Remove edges matching the pattern (empty = wildcard).
    static size_t removeEdges(CPtr<ListreeValue> graph,
                               const std::string& from = "",
                               const std::string& relation = "",
                               const std::string& to = "");

    /// Check if a node exists.
    static bool hasNode(CPtr<ListreeValue> graph, const std::string& name);

    /// Count nodes.
    static size_t nodeCount(CPtr<ListreeValue> graph);

    /// Count edges.
    static size_t edgeCount(CPtr<ListreeValue> graph);
};

} // namespace knowledge
} // namespace agentc
