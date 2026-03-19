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

#include <iostream>
#include <fstream>
#include "mapper.h"

using namespace agentc;
using namespace agentc::cartographer;

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    Mapper mapper; CPtr<ListreeValue> root = mapper.parse(argv[1]);
    if (!root) return 1;
    if (argc > 2) { std::ofstream ofs(argv[2]); root->toDot(ofs, argv[1]); }
    else root->toDot(std::cout, argv[1]);
    return 0;
}
