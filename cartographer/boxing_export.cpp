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

// boxing_export.cpp — Thin C++ wrapper exporting agentc_box / agentc_unbox /
// agentc_box_free as extern "C" symbols by delegating directly to the C++
// Boxing class.  This replaces the 225-line pure-C libboxing/boxing_ffi.c
// implementation; by living in libcartographer.so it shares the same slab
// allocator instance without requiring a separate shared library.

#include "boxing_ffi.h"
#include "boxing.h"

using namespace agentc;
using namespace agentc::cartographer;

namespace {
    const Boxing g_boxing;
} // namespace

extern "C" {

LTV agentc_box(LTV source, LTV typeDef) {
    CPtr<ListreeValue> result = g_boxing.box(ltv_borrow(source), ltv_borrow(typeDef));
    return result.release();
}

LTV agentc_unbox(LTV boxed) {
    CPtr<ListreeValue> result = g_boxing.unbox(ltv_borrow(boxed));
    return result.release();
}

void agentc_box_free(LTV boxed) {
    Boxing::freeBox(ltv_borrow(boxed));
}

} // extern "C"
