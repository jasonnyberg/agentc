// This file is part of AgentC.
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <iostream>

namespace agentc::edict {

class BlockAccumulator {
public:
    // Initialize with a set of bracket pairs, e.g. "[](){}"
    BlockAccumulator(const std::string& pairs = "[](){}");

    // Add a line of text. Returns true if a complete block is now available.
    bool addLine(const std::string& line, std::ostream* warn_stream = nullptr, int line_num = 0);

    // Check if the current accumulated buffer is a complete block (depth == 0)
    bool isComplete() const;

    // Check if there are no characters accumulated at all
    bool isEmpty() const;

    // Get the accumulated block
    std::string getBlock() const;

    // Clear the accumulated block and state
    void clear();

    // Get the current bracket depth
    size_t getDepth() const { return stack.size(); }

    // Get the starting line number of the current block
    int getStartLine() const { return start_line; }

private:
    std::vector<std::pair<char, char>> bracket_pairs;
    std::string accumulated;
    std::vector<char> stack;
    bool in_string = false;
    bool escape_next = false;
    int start_line = 0;

    char getExpectedCloser(char opener) const;
    bool isOpener(char c) const;
    bool isCloser(char c) const;
};

} // namespace agentc::edict
