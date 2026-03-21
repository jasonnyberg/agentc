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

#include "ffi.h"
#include <iostream>
#include <cstring>
#include <climits>    // PATH_MAX
#include <stdlib.h>   // realpath

namespace agentc {
namespace cartographer {

namespace {

// M5: Blocklist of dangerous C functions that must never be callable via the
// Edict FFI.  Uses exact string comparison through std::unordered_set, which
// provides collision-resistant lookups (libstdc++ uses SipHash or similar),
// unlike a custom FNV1a hash.
//
// Threat model: Edict source code may be loaded from untrusted sources (agent
// plugins, network). This list is a hard guardrail against the most dangerous
// syscall wrappers.  It is not a substitute for OS-level sandboxing (seccomp,
// pledge), but it prevents the most obvious escape paths.
//
// Categories:
//   PROC  — process creation / exec
//   SHELL — shell / word-expansion
//   FILE  — destructive filesystem ops
//   DL    — dynamic loading (recursive FFI bypass)
//   MEM   — memory protection changes
//   SIG   — signal handler installation
static const std::unordered_set<std::string>& blocklist() {
    static const std::unordered_set<std::string> kBlocklist = {
        // PROC — process creation and execution
        "execv", "execvp", "execvpe", "execve", "execle", "execl", "execlp",
        "fexecve",
        "posix_spawn", "posix_spawnp",
        "fork", "vfork", "clone",
        "_exit", "_Exit",

        // SHELL — shell invocation and word expansion
        "system", "popen", "pclose",
        "wordexp", "wordfree",

        // FILE — destructive filesystem operations
        "unlink", "unlinkat",
        "remove",
        "rename", "renameat", "renameat2",
        "rmdir",
        "chmod", "fchmod", "fchmodat",
        "chown", "fchown", "lchown", "fchownat",
        "truncate", "ftruncate",
        "mkfifo", "mkfifoat",
        "mknod", "mknodat",
        "mount", "umount", "umount2",

        // DL — dynamic loading (would bypass this blocklist for any newly loaded lib)
        "dlopen", "dlmopen", "dlclose",

        // MEM — memory protection changes (can make heap/stack executable)
        "mprotect",

        // SIG — signal handler installation (could subvert error handling)
        "signal", "sigaction", "sigprocmask", "sigwait",
    };
    return kBlocklist;
}

static bool isParameterNode(CPtr<ListreeValue> node) {
    if (!node) {
        return false;
    }
    auto kind = node->find("kind");
    auto kindValue = kind ? kind->getValue() : nullptr;
    if (!kindValue || !kindValue->getData() || kindValue->getLength() == 0) {
        return false;
    }
    return std::string((char*)kindValue->getData(), kindValue->getLength()) == "Parameter";
}

static size_t countParameterTypes(CPtr<ListreeValue> definition) {
    size_t count = 0;
    auto children = definition ? definition->find("children") : nullptr;
    auto childValue = children ? children->getValue() : nullptr;
    if (childValue) {
        childValue->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) {
            auto node = item ? item->getValue() : nullptr;
            if (isParameterNode(node)) {
                ++count;
            }
        });
    }
    return count;
}

static size_t countInvokeArgs(CPtr<ListreeValue> args) {
    if (!args) {
        return 0;
    }
    if (!args->isListMode()) {
        return 1;
    }
    size_t count = 0;
    args->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

} // namespace

// M5: Public static isBlocked() — exact string lookup, no custom hashing.
bool FFI::isBlocked(const std::string& funcName) {
    return blocklist().count(funcName) != 0;
}

FFI::FFI() {}
FFI::~FFI() { 
    // H2: close all loaded library handles
    for (auto& [path, h] : handles_) {
        if (h) dlclose(h);
    }
    forEachClosure([&](ClosureInfo* c) {
        ffi_closure_free(c->closure);
        if (c->userDataCleanup && c->userData) c->userDataCleanup(c->userData);
        delete[] c->argTypes;
        delete c;
    });
}

void FFI::appendClosure(ClosureInfo* closureInfo) {
    if (!closures) {
        SlabId sid = Allocator<CLL<FFI::ClosureInfoNode>>::getAllocator().allocate();
        closures = CPtr<CLL<FFI::ClosureInfoNode>>(sid);
    }
    FFI::ClosureInfoNode nodeValue{};
    nodeValue.value = closureInfo;
    CPtr<CLL<FFI::ClosureInfoNode>> node(nodeValue);
    closures->store(node, false);
}

void FFI::forEachClosure(const std::function<void(ClosureInfo*)>& callback) const {
    if (!closures) {
        return;
    }
    closures->forEach([&](CPtr<FFI::ClosureInfoNode>& entry) {
        if (entry && entry->value) {
            callback(entry->value);
        }
    }, false);
}

bool FFI::loadLibrary(const std::string& path) {
    // H2: Canonicalize path to detect duplicate loads (e.g., relative vs absolute).
    char resolved[PATH_MAX];
    const std::string key = (realpath(path.c_str(), resolved) != nullptr) ? resolved : path;

    // If already loaded, reuse the existing handle.
    if (handles_.count(key)) return true;

    void* h = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!h) return false;
    handles_[key] = h;
    return true;
}
ffi_type* FFI::getFFIType(const std::string& name) {
    if (name == "int" || name == "signed int") return &ffi_type_sint;
    if (name == "unsigned int")               return &ffi_type_uint;
    if (name == "void")                        return &ffi_type_void;
    if (name == "double")                      return &ffi_type_double;
    if (name == "float")                       return &ffi_type_float;
    if (name == "char" || name == "signed char")   return &ffi_type_schar;
    if (name == "unsigned char")               return &ffi_type_uchar;
    if (name == "short" || name == "short int" || name == "signed short") return &ffi_type_sshort;
    if (name == "unsigned short" || name == "unsigned short int")         return &ffi_type_ushort;
    if (name == "long" || name == "long int" || name == "signed long")    return &ffi_type_slong;
    if (name == "unsigned long" || name == "unsigned long int")           return &ffi_type_ulong;
    if (name == "long long" || name == "long long int" || name == "signed long long")  return &ffi_type_sint64;
    if (name == "unsigned long long" || name == "unsigned long long int") return &ffi_type_uint64;
    if (name == "char*") return &ffi_type_pointer;
    if (name == "pointer") return &ffi_type_pointer;
    // Handle clang-style pointer spellings: "char *", "const char *", "void *", etc.
    if (name.find('*') != std::string::npos) return &ffi_type_pointer;
    return &ffi_type_sint; // Default for unknown types
}
void FFI::convertValue(CPtr<ListreeValue> val, ffi_type* type, void* storage) {
    // Helper: get string representation from an Edict value (non-binary).
    auto getText = [&]() -> std::string {
        if (!val || !val->getData() || val->getLength() == 0) return {};
        if ((val->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return {};
        return std::string((char*)val->getData(), val->getLength());
    };
    // Helper: get raw binary bytes (for pointer pass-through and binary integer values).
    auto isBinary = [&]() -> bool {
        return val && (val->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None;
    };

    if (type == &ffi_type_sint) {
        if (isBinary() && val->getLength() == sizeof(int32_t)) {
            std::memcpy(storage, val->getData(), sizeof(int32_t));
        } else {
            std::string s = getText();
            try { *(int32_t*)storage = s.empty() ? 0 : std::stoi(s); } catch(...) { *(int32_t*)storage = 0; }
        }
    } else if (type == &ffi_type_uint) {
        if (isBinary() && val->getLength() == sizeof(uint32_t)) {
            std::memcpy(storage, val->getData(), sizeof(uint32_t));
        } else {
            std::string s = getText();
            try { *(uint32_t*)storage = s.empty() ? 0 : (uint32_t)std::stoul(s); } catch(...) { *(uint32_t*)storage = 0; }
        }
    } else if (type == &ffi_type_slong) {
        if (isBinary() && val->getLength() == sizeof(long)) {
            std::memcpy(storage, val->getData(), sizeof(long));
        } else {
            std::string s = getText();
            try { *(long*)storage = s.empty() ? 0L : std::stol(s); } catch(...) { *(long*)storage = 0L; }
        }
    } else if (type == &ffi_type_ulong) {
        if (isBinary() && val->getLength() == sizeof(unsigned long)) {
            std::memcpy(storage, val->getData(), sizeof(unsigned long));
        } else {
            std::string s = getText();
            try { *(unsigned long*)storage = s.empty() ? 0UL : std::stoul(s); } catch(...) { *(unsigned long*)storage = 0UL; }
        }
    } else if (type == &ffi_type_sint64) {
        if (isBinary() && val->getLength() == sizeof(int64_t)) {
            std::memcpy(storage, val->getData(), sizeof(int64_t));
        } else {
            std::string s = getText();
            try { *(int64_t*)storage = s.empty() ? 0LL : (int64_t)std::stoll(s); } catch(...) { *(int64_t*)storage = 0LL; }
        }
    } else if (type == &ffi_type_uint64) {
        if (isBinary() && val->getLength() == sizeof(uint64_t)) {
            std::memcpy(storage, val->getData(), sizeof(uint64_t));
        } else {
            std::string s = getText();
            try { *(uint64_t*)storage = s.empty() ? 0ULL : (uint64_t)std::stoull(s); } catch(...) { *(uint64_t*)storage = 0ULL; }
        }
    } else if (type == &ffi_type_sshort) {
        if (isBinary() && val->getLength() == sizeof(int16_t)) {
            std::memcpy(storage, val->getData(), sizeof(int16_t));
        } else {
            std::string s = getText();
            try { *(int16_t*)storage = s.empty() ? 0 : (int16_t)std::stoi(s); } catch(...) { *(int16_t*)storage = 0; }
        }
    } else if (type == &ffi_type_ushort) {
        if (isBinary() && val->getLength() == sizeof(uint16_t)) {
            std::memcpy(storage, val->getData(), sizeof(uint16_t));
        } else {
            std::string s = getText();
            try { *(uint16_t*)storage = s.empty() ? 0 : (uint16_t)std::stoul(s); } catch(...) { *(uint16_t*)storage = 0; }
        }
    } else if (type == &ffi_type_schar) {
        if (isBinary() && val->getLength() == sizeof(int8_t)) {
            std::memcpy(storage, val->getData(), sizeof(int8_t));
        } else {
            std::string s = getText();
            try { *(int8_t*)storage = s.empty() ? 0 : (int8_t)std::stoi(s); } catch(...) { *(int8_t*)storage = 0; }
        }
    } else if (type == &ffi_type_uchar) {
        if (isBinary() && val->getLength() == sizeof(uint8_t)) {
            std::memcpy(storage, val->getData(), sizeof(uint8_t));
        } else {
            std::string s = getText();
            try { *(uint8_t*)storage = s.empty() ? 0 : (uint8_t)std::stoul(s); } catch(...) { *(uint8_t*)storage = 0; }
        }
    } else if (type == &ffi_type_float) {
        if (isBinary() && val->getLength() == sizeof(float)) {
            std::memcpy(storage, val->getData(), sizeof(float));
        } else {
            std::string s = getText();
            try { *(float*)storage = s.empty() ? 0.0f : std::stof(s); } catch(...) { *(float*)storage = 0.0f; }
        }
    } else if (type == &ffi_type_double) {
        if (isBinary() && val->getLength() == sizeof(double)) {
            *(double*)storage = *(double*)val->getData();
        } else {
            std::string s = getText();
            try { *(double*)storage = s.empty() ? 0.0 : std::stod(s); } catch(...) { *(double*)storage = 0.0; }
        }
    } else if (type == &ffi_type_pointer) {
        if (isBinary() && val->getLength() == sizeof(void*)) {
            *(void**)storage = *(void**)val->getData();
        } else {
            *(void**)storage = nullptr;
        }
    }
}
CPtr<ListreeValue> FFI::convertReturn(void* storage, ffi_type* type) {
    char buf[32];
    if (type == &ffi_type_sint) {
        int32_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%d", v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_uint) {
        uint32_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%u", v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_slong) {
        long v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%ld", v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_ulong) {
        unsigned long v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%lu", v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_sint64) {
        int64_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_uint64) {
        uint64_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_sshort) {
        int16_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%d", (int)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_ushort) {
        uint16_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%u", (unsigned)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_schar) {
        int8_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%d", (int)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_uchar) {
        uint8_t v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%u", (unsigned)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_float) {
        float v; std::memcpy(&v, storage, sizeof(v));
        std::snprintf(buf, sizeof(buf), "%.9g", (double)v);
        return createStringValue(std::string(buf));
    }
    if (type == &ffi_type_double) {
        double d;
        std::memcpy(&d, storage, sizeof(double));
        std::snprintf(buf, sizeof(buf), "%.15g", d);
        return createStringValue(std::string(buf));
    }
    // H3: Pointer-returning functions (char*, void*, etc.) — wrap raw pointer as opaque binary.
    if (type == &ffi_type_pointer) return createBinaryValue(storage, sizeof(void*));
    return createNullValue();
}
void* FFI::createClosure(CPtr<ListreeValue> definition, void (*thunk)(ffi_cif*,void*,void**,void*), void* userData, UserDataCleanup userDataCleanup) {
    if (!definition) return nullptr;
    std::string retTypeStr = "void";
    auto retItem = definition->find("return_type");
    if (retItem && retItem->getValue()) retTypeStr = std::string((char*)retItem->getValue()->getData(), retItem->getValue()->getLength());
    ffi_type* rtype = getFFIType(retTypeStr);

    size_t argCount = countParameterTypes(definition);

    ClosureInfo* info = new ClosureInfo();
    info->argTypes = argCount > 0 ? new ffi_type*[argCount] : nullptr;
    info->userData = userData;
    info->userDataCleanup = userDataCleanup;
    if (argCount > 0) {
        size_t index = 0;
        auto children = definition->find("children");
        auto childValue = children ? children->getValue() : nullptr;
        if (childValue) {
            childValue->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) {
                auto node = item ? item->getValue() : nullptr;
                if (!isParameterNode(node)) {
                    return;
                }
                auto type = node->find("type");
                auto typeValue = type ? type->getValue() : nullptr;
                if (!typeValue || !typeValue->getData() || typeValue->getLength() == 0) {
                    info->argTypes[index++] = getFFIType("int");
                    return;
                }
                info->argTypes[index++] = getFFIType(std::string((char*)typeValue->getData(), typeValue->getLength()));
            });
        }
    }

    info->closure = (ffi_closure*)ffi_closure_alloc(sizeof(ffi_closure), &info->code);
    
    if (ffi_prep_cif(&info->cif, FFI_DEFAULT_ABI, static_cast<unsigned int>(argCount), rtype, info->argTypes) == FFI_OK) {
        if (ffi_prep_closure_loc(info->closure, &info->cif, thunk, userData, info->code) == FFI_OK) {
            void* code = info->code;
            appendClosure(info);
            return code;
        }
    }

    delete[] info->argTypes;
    ffi_closure_free(info->closure);
    if (info->userDataCleanup && info->userData) info->userDataCleanup(info->userData);
    delete info;
    return nullptr;
}
CPtr<ListreeValue> FFI::invoke(const std::string& funcName, CPtr<ListreeValue> definition, CPtr<ListreeValue> args) {
    // M5: Reject calls to blocked dangerous functions before touching dlsym.
    if (isBlocked(funcName)) return nullptr;

    if (handles_.empty() || !definition) return nullptr;

    // H2: Search all loaded libraries for the symbol.
    void* ptr = nullptr;
    for (auto& [path, h] : handles_) {
        ptr = dlsym(h, funcName.c_str());
        if (ptr) break;
    }
    if (!ptr) return nullptr;
    std::string retTypeStr = "void";
    auto retItem = definition->find("return_type");
    if (retItem && retItem->getValue()) retTypeStr = std::string((char*)retItem->getValue()->getData(), retItem->getValue()->getLength());
    ffi_type* rtype = getFFIType(retTypeStr);
    unsigned int nargs = static_cast<unsigned int>(countInvokeArgs(args));

    // G007.3: Use stack storage for arg types/pointers to avoid heap allocation for
    // the common case of small argument lists. Fall back to heap for large lists.
    constexpr unsigned int kStackArgLimit = 16;
    // Each arg needs at most sizeof(void*) bytes of value storage (largest FFI scalar).
    constexpr size_t kArgStorageSize = sizeof(void*);

    ffi_type* stackTypes[kStackArgLimit];
    // Aligned storage for up to kStackArgLimit argument values.
    alignas(alignof(void*)) char stackStorage[kStackArgLimit * kArgStorageSize] = {};
    void* stackPtrs[kStackArgLimit];

    ffi_type** types = (nargs > 0 && nargs <= kStackArgLimit) ? stackTypes : (nargs > 0 ? new ffi_type*[nargs] : nullptr);
    void** values   = (nargs > 0 && nargs <= kStackArgLimit) ? stackPtrs  : (nargs > 0 ? new void*[nargs]   : nullptr);

    // Point each stackPtrs[i] into the pre-allocated stack storage block.
    if (nargs > 0 && nargs <= kStackArgLimit) {
        for (unsigned int i = 0; i < nargs; ++i) {
            stackPtrs[i] = &stackStorage[i * kArgStorageSize];
        }
    } else if (nargs > kStackArgLimit) {
        // Heap fallback for oversized argument lists.
        for (unsigned int i = 0; i < nargs; ++i) {
            values[i] = malloc(kArgStorageSize);
        }
    }

    if (nargs > 0) {
        size_t index = 0;
        auto children = definition->find("children");
        auto childValue = children ? children->getValue() : nullptr;
        if (childValue) {
            childValue->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) {
                auto node = item ? item->getValue() : nullptr;
                if (!isParameterNode(node)) {
                    return;
                }
                auto type = node->find("type");
                auto typeValue = type ? type->getValue() : nullptr;
                if (!typeValue || !typeValue->getData() || typeValue->getLength() == 0) {
                    types[index++] = getFFIType("int");
                    return;
                }
                types[index++] = getFFIType(std::string((char*)typeValue->getData(), typeValue->getLength()));
            });
        }
    }
    unsigned int argIndex = 0;
    auto convertArgument = [&](CPtr<ListreeValue> argValue) {
        convertValue(argValue, types[argIndex], values[argIndex]);
        ++argIndex;
    };
    if (args && !args->isListMode()) {
        convertArgument(args);
    } else if (args) {
        args->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) {
                convertArgument(ref->getValue());
            }
        }, false);
    }

    // Return value: stack buffer sized to hold the largest scalar (sizeof(void*)).
    alignas(alignof(void*)) char rvalBuf[sizeof(void*)] = {};

    ffi_cif cif;
    CPtr<ListreeValue> res = nullptr;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, rtype, types) == FFI_OK) {
        ffi_call(&cif, FFI_FN(ptr), rvalBuf, values);
        res = convertReturn(rvalBuf, rtype);
    }

    // Free heap-allocated arg buffers only for the oversized case.
    if (nargs > kStackArgLimit) {
        for (unsigned int i = 0; i < nargs; ++i) free(values[i]);
        delete[] types;
        delete[] values;
    }
    return res;
}

} // namespace cartographer
} // namespace agentc
