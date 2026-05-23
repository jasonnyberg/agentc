// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#pragma once

#include "../listree/listree.h"

#include <string>
#include <vector>

namespace agentc::edict::static_image {

struct ValidationResult {
    bool ok = false;
    std::string code;
    std::string message;
};

struct MountedDeclarationImage {
    CPtr<agentc::ListreeValue> root;
    ValidationResult validation;
    std::vector<SlabId> staticValueSlots;
    SlabId rootId;
    uint64_t mountId = 0;
};

// G103 first slice: a deterministic declarative import image for the small
// worker primitive surface.  This is intentionally metadata-only: it declares
// namespace/symbol/schema facts that a future static slab image can mount, but
// it does not store dlopen handles, function pointers, eventfds, pidfds, VM
// frames, or any other process-local authority.
CPtr<agentc::ListreeValue> buildWorkerPrimitiveDeclarationImage();

bool writeDeclarationImage(CPtr<agentc::ListreeValue> image,
                           const std::string& path,
                           std::string* error = nullptr);
bool writeDeclarationImageContainer(CPtr<agentc::ListreeValue> image,
                                    const std::string& path,
                                    std::string* error = nullptr);
CPtr<agentc::ListreeValue> readDeclarationImage(const std::string& path,
                                                std::string* error = nullptr);
CPtr<agentc::ListreeValue> readDeclarationImageMmapReadOnly(const std::string& path,
                                                            std::string* error = nullptr);
CPtr<agentc::ListreeValue> readDeclarationImageContainerMmapReadOnly(const std::string& path,
                                                                     std::string* error = nullptr);
ValidationResult validateDeclarationImage(CPtr<agentc::ListreeValue> image);
MountedDeclarationImage mountDeclarationImageReadOnly(CPtr<agentc::ListreeValue> image);
MountedDeclarationImage mountDeclarationImageReadOnly(CPtr<agentc::ListreeValue> image,
                                                     agentc::ListreeStaticMountRegistry& registry);

std::string declarationPayloadHash(CPtr<agentc::ListreeValue> declarations);

} // namespace agentc::edict::static_image
