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

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <ffi.h>
#include <dlfcn.h>
#include "../core/container.h"
#include "../listree/listree.h"
#include "../core/cursor.h"

namespace agentc {
namespace cartographer {

class FFI {
public:
    using UserDataCleanup = void (*)(void*);

    FFI();
    ~FFI();
    bool loadLibrary(const std::string& path);
    // Load symbols from the process itself (i.e. all shared libraries already
    // linked at startup, such as libcartographer.so).  Used by the bootstrap
    // cartographer to call agentc_box/unbox/box_free via the FFI path without
    // needing a runtime path to libcartographer.so.
    bool loadProcessSymbols();
    CPtr<ListreeValue> invoke(const std::string& funcName, CPtr<ListreeValue> definition, CPtr<ListreeValue> args);
    void* createClosure(CPtr<ListreeValue> definition, void (*thunk)(ffi_cif*,void*,void**,void*), void* userData, UserDataCleanup userDataCleanup = nullptr);

    // M5: Exact-string blocklist check — returns true if funcName is blocked.
    // Uses std::unordered_set (collision-resistant hash) rather than FNV1a.
    static bool isBlocked(const std::string& funcName);

    // Helpers for marshalling
    void convertValue(CPtr<ListreeValue> val, ffi_type* type, void* storage);
    CPtr<ListreeValue> convertReturn(void* storage, ffi_type* type);
    static bool isLtvType(ffi_type* type);

private:
    struct ClosureInfo {
        ffi_closure* closure;
        ffi_cif cif;
        ffi_type** argTypes;
        void* code;
        void* userData;
        UserDataCleanup userDataCleanup;
    };
    struct ClosureInfoNode {
        ClosureInfo* value;
    };
    CPtr<CLL<ClosureInfoNode>> closures;

    void appendClosure(ClosureInfo* closureInfo);
    void forEachClosure(const std::function<void(ClosureInfo*)>& callback) const;

    // H2: Map of canonical path → dlopen handle; supports multiple simultaneously-loaded libraries.
    std::unordered_map<std::string, void*> handles_;
    ffi_type* getFFIType(const std::string& typeName);
};

} // namespace cartographer
} // namespace agentc
