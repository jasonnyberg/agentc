#include "block_accumulator.h"

namespace agentc::edict {

BlockAccumulator::BlockAccumulator(const std::string& pairs) {
    for (size_t i = 0; i + 1 < pairs.length(); i += 2) {
        bracket_pairs.push_back({pairs[i], pairs[i+1]});
    }
}

char BlockAccumulator::getExpectedCloser(char opener) const {
    for (const auto& pair : bracket_pairs) {
        if (pair.first == opener) return pair.second;
    }
    return '\0';
}

bool BlockAccumulator::isOpener(char c) const {
    for (const auto& pair : bracket_pairs) {
        if (pair.first == c) return true;
    }
    return false;
}

bool BlockAccumulator::isCloser(char c) const {
    for (const auto& pair : bracket_pairs) {
        if (pair.second == c) return true;
    }
    return false;
}

bool BlockAccumulator::addLine(const std::string& line, std::ostream* warn_stream, int line_num) {
    const auto first = line.find_first_not_of(" \t");
    
    // Parse characters
    for (char c : line) {
        if (escape_next) {
            escape_next = false;
            continue;
        }
        if (c == '\\' && in_string) {
            escape_next = true;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
        }
        if (in_string) continue;
        
        if (c == '#') break; // stop at comment

        if (isOpener(c)) {
            stack.push_back(c);
        } else if (isCloser(c)) {
            if (!stack.empty()) {
                char expected = getExpectedCloser(stack.back());
                if (c != expected && warn_stream) {
                    *warn_stream << "Warning (line " << line_num << "): mismatched bracket closing " 
                                 << c << " (expected matching for " << stack.back() << ")" << std::endl;
                }
                stack.pop_back();
            } else if (warn_stream) {
                *warn_stream << "Warning (line " << line_num << "): unexpected closing bracket " << c << std::endl;
            }
        }
    }

    if (accumulated.empty() && (first == std::string::npos || line[first] == '#')) {
        return false; // pure comment or empty line, and nothing accumulated yet
    }

    if (accumulated.empty()) {
        start_line = line_num;
    }
    
    accumulated += line + " ";

    return stack.empty() && !in_string;
}

bool BlockAccumulator::isComplete() const {
    return stack.empty() && !in_string && !accumulated.empty();
}

bool BlockAccumulator::isEmpty() const {
    return accumulated.empty();
}

std::string BlockAccumulator::getBlock() const {
    return accumulated;
}

void BlockAccumulator::clear() {
    accumulated.clear();
    stack.clear();
    in_string = false;
    escape_next = false;
    start_line = 0;
}

} // namespace agentc::edict
