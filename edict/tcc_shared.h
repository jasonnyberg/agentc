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

/// @file tcc_shared.h
/// Shared types and constants used by both the TCC coordinator
/// (tcc_runtime.cpp) and the TCC worker exec (tcc_worker_native.cpp).
///
/// Rules:
///   - BoundSymbol is the canonical struct for a pre-approved host symbol.
///     The `address` field is populated by the worker (resolution side) and is
///     unused on the coordinator (validation-only) side — this is intentional
///     and does not trigger unused-member warnings.
///   - Wire serialisation (writeBoundSymbols / readBoundSymbols) encodes only
///     name + declaration + origin. The `address` field is never transmitted.

#include <string>
#include <unordered_map>
#include <dlfcn.h>

namespace agentc::edict::tcc {

// ── Constants ────────────────────────────────────────────────────────────────

constexpr const char* kProcessOrigin = "<process>";
constexpr const char* kModeCompile   = "compile";
constexpr const char* kModeRun       = "run";

// ── BoundSymbol ──────────────────────────────────────────────────────────────

/// A host symbol that has been approved for use in TCC-compiled code.
///
/// Coordinator side: name/declaration/origin are set; address is nullptr
///   (validateSymbolOrigin checks the symbol is resolvable without storing
///    the address — the worker resolves it independently in its own process).
///
/// Worker side: resolveSymbolOrigin populates address from dlsym and passes
///   it to tcc_add_symbol.
struct BoundSymbol {
    std::string name;
    std::string declaration;
    std::string origin;
    const void* address = nullptr;  ///< Populated by worker; unused by coordinator.
};

// ── Symbol resolution ────────────────────────────────────────────────────────

/// Open the dynamic library for `symbol.origin`, dlsym the symbol, and
/// optionally store the resolved address into `symbol.address`.
///
/// @param symbol          Symbol to resolve (origin and name must be set).
/// @param handles         Per-origin dlopen handle cache; updated in-place.
/// @param processHandle   Cached handle from dlopen(nullptr); updated in-place.
/// @param outAddress      If non-null, store the resolved address here.
///                        If null, validation-only mode (coordinator path).
/// @param error           Human-readable error on failure.
/// @returns true on success, false with error set on failure.
inline bool resolveSymbolOrigin(const BoundSymbol& symbol,
                                std::unordered_map<std::string, void*>& handles,
                                void*& processHandle,
                                const void** outAddress,
                                std::string& error) {
    if (symbol.origin.empty()) {
        error = "symbol '" + symbol.name + "' has no origin";
        return false;
    }

    void* handle = nullptr;
    if (symbol.origin == kProcessOrigin) {
        if (!processHandle) {
            processHandle = ::dlopen(nullptr, RTLD_NOW);
            if (processHandle) {
                handles[kProcessOrigin] = processHandle;
            }
        }
        handle = processHandle;
        if (!handle) {
            const char* msg = ::dlerror();
            error = msg ? msg : "dlopen(nullptr) failed";
            return false;
        }
    } else {
        auto existing = handles.find(symbol.origin);
        if (existing != handles.end()) {
            handle = existing->second;
        } else {
            handle = ::dlopen(symbol.origin.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                const char* msg = ::dlerror();
                error = msg ? msg : ("Failed to open library: " + symbol.origin);
                return false;
            }
            handles[symbol.origin] = handle;
        }
    }

    ::dlerror(); // clear any previous error
    void* resolved = ::dlsym(handle, symbol.name.c_str());
    const char* symError = ::dlerror();
    if (!resolved || symError) {
        error = symError ? symError
                         : ("Failed to resolve symbol '" + symbol.name + "'");
        return false;
    }

    if (outAddress) {
        *outAddress = resolved;
    }
    return true;
}

/// Close all library handles in the cache, skipping the process handle.
inline void closeLibraryHandles(const std::unordered_map<std::string, void*>& handles) {
    for (const auto& [origin, handle] : handles) {
        if (handle && origin != kProcessOrigin) {
            ::dlclose(handle);
        }
    }
}

} // namespace agentc::edict::tcc
