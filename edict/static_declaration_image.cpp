// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "static_declaration_image.h"

#include "../core/root1_resource_broker.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace agentc::edict::static_image {
namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value,
                                      const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string stringValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return {};
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

uint64_t listValueCount(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->isListMode()) {
        return 0;
    }
    uint64_t count = 0;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

CPtr<agentc::ListreeValue> stringList(std::initializer_list<std::string> values) {
    auto list = agentc::createListValue();
    for (const auto& value : values) {
        agentc::addListItem(list, agentc::createStringValue(value));
    }
    return list;
}

CPtr<agentc::ListreeValue> symbolDeclaration(const std::string& word,
                                             const std::string& nativeSymbol,
                                             const std::string& stackSignature,
                                             const std::string& category) {
    auto symbol = agentc::createNullValue();
    agentc::addNamedItem(symbol, "word", agentc::createStringValue(word));
    agentc::addNamedItem(symbol, "native_symbol", agentc::createStringValue(nativeSymbol));
    agentc::addNamedItem(symbol, "stack_signature", agentc::createStringValue(stackSignature));
    agentc::addNamedItem(symbol, "category", agentc::createStringValue(category));
    agentc::addNamedItem(symbol, "binding", agentc::createStringValue("lazy_process_local"));
    agentc::addNamedItem(symbol, "stores_native_handle", agentc::createStringValue("false"));
    agentc::addNamedItem(symbol, "worker_allowed", agentc::createStringValue("true"));
    agentc::addNamedItem(symbol, "notes", agentc::createStringValue(
        "Declarative metadata only; actual Cartographer/FFI binding is process-local."));
    return symbol;
}

std::string fnv1a64(const std::string& text) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

ValidationResult fail(const std::string& code, const std::string& message) {
    return ValidationResult{false, code, message};
}

constexpr char kContainerMagic[] = {'A', 'C', 'S', 'D', 'I', '0', '0', '1'};
constexpr uint32_t kContainerVersion = 1;

void appendU32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
    }
}

void appendU64(std::string& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
    }
}

bool readU32(const char* data, size_t size, size_t& cursor, uint32_t& value) {
    if (cursor + 4 > size) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(static_cast<unsigned char>(data[cursor++])) << (i * 8);
    }
    return true;
}

bool readU64(const char* data, size_t size, size_t& cursor, uint64_t& value) {
    if (cursor + 8 > size) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(static_cast<unsigned char>(data[cursor++])) << (i * 8);
    }
    return true;
}

bool mappedFileReadOnly(const std::string& path,
                        const char*& bytes,
                        size_t& size,
                        void*& mapped,
                        std::string* error) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (error) {
            *error = "failed to open static declaration image for read-only mmap: " + path;
        }
        return false;
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        if (error) {
            *error = "failed to stat static declaration image for read-only mmap: " + path;
        }
        ::close(fd);
        return false;
    }

    size = static_cast<size_t>(st.st_size);
    mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        if (error) {
            *error = std::string("failed to mmap static declaration image read-only: ") + std::strerror(errno);
        }
        mapped = nullptr;
        return false;
    }
    bytes = static_cast<const char*>(mapped);
    return true;
}

} // namespace

std::string declarationPayloadHash(CPtr<agentc::ListreeValue> declarations) {
    return fnv1a64(agentc::toJson(declarations));
}

CPtr<agentc::ListreeValue> buildWorkerPrimitiveDeclarationImage() {
    auto declarations = agentc::createListValue();
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_active_count", "agentc_worker_edict_active_count_ltv", "() -> ltv", "lifecycle"));
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_lifecycle_status", "agentc_worker_edict_lifecycle_status_ltv", "() -> ltv", "lifecycle"));
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_prepare_task", "agentc_worker_edict_prepare_task_ltv", "(ltv task) -> ltv", "task"));
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_start_status", "agentc_worker_edict_start_status_ltv", "(ltv task) -> ltv", "async"));
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_collect_status", "agentc_worker_edict_collect_status_ltv", "(ltv job_or_request, ltv events) -> ltv", "async"));
    agentc::addListItem(declarations, symbolDeclaration(
        "worker.edict_validate_result_contract", "agentc_worker_edict_validate_result_contract_ltv", "(ltv check) -> ltv", "contract"));

    auto manifest = agentc::createNullValue();
    agentc::addNamedItem(manifest, "format", agentc::createStringValue("agentc.static_declaration_image"));
    agentc::addNamedItem(manifest, "format_version", agentc::createStringValue("1"));
    agentc::addNamedItem(manifest, "image_kind", agentc::createStringValue("declarative_import_module"));
    agentc::addNamedItem(manifest, "module", agentc::createStringValue("worker.edict"));
    agentc::addNamedItem(manifest, "root_id", agentc::createStringValue("worker.edict/declarations"));
    agentc::addNamedItem(manifest, "hash_algorithm", agentc::createStringValue("fnv1a64"));
    agentc::addNamedItem(manifest, "payload_hash", agentc::createStringValue(declarationPayloadHash(declarations)));
    agentc::addNamedItem(manifest, "contains_native_handles", agentc::createStringValue("false"));
    agentc::addNamedItem(manifest, "native_binding_policy", agentc::createStringValue("lazy_process_local_sidecar"));
    agentc::addNamedItem(manifest, "forbidden_payloads", stringList({
        "dlopen_handle", "dlsym_pointer", "function_pointer", "eventfd", "epoll_fd",
        "pidfd", "edict_vm_pointer", "activation_frame", "credential", "provider_handle"
    }));

    auto image = agentc::createNullValue();
    agentc::addNamedItem(image, "manifest", manifest);
    agentc::addNamedItem(image, "declarations", declarations);
    return image;
}

bool writeDeclarationImage(CPtr<agentc::ListreeValue> image,
                           const std::string& path,
                           std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "failed to open static declaration image for writing: " + path;
        }
        return false;
    }
    out << agentc::toJson(image);
    return static_cast<bool>(out);
}

bool writeDeclarationImageContainer(CPtr<agentc::ListreeValue> image,
                                    const std::string& path,
                                    std::string* error) {
    const auto validation = validateDeclarationImage(image);
    if (!validation.ok) {
        if (error) {
            *error = validation.code + ": " + validation.message;
        }
        return false;
    }
    const std::string manifestJson = agentc::toJson(namedValue(image, "manifest"));
    const std::string payloadJson = agentc::toJson(namedValue(image, "declarations"));

    std::string bytes;
    bytes.append(kContainerMagic, sizeof(kContainerMagic));
    appendU32(bytes, kContainerVersion);
    appendU64(bytes, static_cast<uint64_t>(manifestJson.size()));
    appendU64(bytes, static_cast<uint64_t>(payloadJson.size()));
    bytes.append(manifestJson);
    bytes.append(payloadJson);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "failed to open static declaration image container for writing: " + path;
        }
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

CPtr<agentc::ListreeValue> readDeclarationImage(const std::string& path,
                                                std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "failed to open static declaration image for reading: " + path;
        }
        return nullptr;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto value = agentc::fromJson(buffer.str());
    if (!value && error) {
        *error = "failed to parse static declaration image: " + path;
    }
    return value;
}

CPtr<agentc::ListreeValue> readDeclarationImageMmapReadOnly(const std::string& path,
                                                            std::string* error) {
    const char* bytes = nullptr;
    size_t size = 0;
    void* mapped = nullptr;
    if (!mappedFileReadOnly(path, bytes, size, mapped, error)) {
        return nullptr;
    }

    std::string json(bytes, size);
    ::munmap(mapped, size);
    auto value = agentc::fromJson(json);
    if (!value && error) {
        *error = "failed to parse read-only mmapped static declaration image: " + path;
    }
    return value;
}

CPtr<agentc::ListreeValue> readDeclarationImageContainerMmapReadOnly(const std::string& path,
                                                                     std::string* error) {
    const char* bytes = nullptr;
    size_t size = 0;
    void* mapped = nullptr;
    if (!mappedFileReadOnly(path, bytes, size, mapped, error)) {
        return nullptr;
    }

    auto cleanup = [&]() {
        if (mapped) {
            ::munmap(mapped, size);
            mapped = nullptr;
        }
    };

    size_t cursor = 0;
    if (size < sizeof(kContainerMagic) || std::memcmp(bytes, kContainerMagic, sizeof(kContainerMagic)) != 0) {
        if (error) {
            *error = "invalid static declaration image container magic";
        }
        cleanup();
        return nullptr;
    }
    cursor += sizeof(kContainerMagic);

    uint32_t version = 0;
    uint64_t manifestLen = 0;
    uint64_t payloadLen = 0;
    if (!readU32(bytes, size, cursor, version) ||
        !readU64(bytes, size, cursor, manifestLen) ||
        !readU64(bytes, size, cursor, payloadLen)) {
        if (error) {
            *error = "truncated static declaration image container header";
        }
        cleanup();
        return nullptr;
    }
    if (version != kContainerVersion) {
        if (error) {
            *error = "unsupported static declaration image container version";
        }
        cleanup();
        return nullptr;
    }
    if (manifestLen > size || payloadLen > size || cursor + manifestLen + payloadLen != size) {
        if (error) {
            *error = "invalid static declaration image container lengths";
        }
        cleanup();
        return nullptr;
    }

    std::string manifestJson(bytes + cursor, static_cast<size_t>(manifestLen));
    cursor += static_cast<size_t>(manifestLen);
    std::string payloadJson(bytes + cursor, static_cast<size_t>(payloadLen));
    cleanup();

    auto manifest = agentc::fromJson(manifestJson);
    auto declarations = agentc::fromJson(payloadJson);
    if (!manifest || !declarations) {
        if (error) {
            *error = "failed to parse static declaration image container manifest or payload";
        }
        return nullptr;
    }

    auto image = agentc::createNullValue();
    agentc::addNamedItem(image, "manifest", manifest);
    agentc::addNamedItem(image, "declarations", declarations);
    const auto validation = validateDeclarationImage(image);
    if (!validation.ok) {
        if (error) {
            *error = validation.code + ": " + validation.message;
        }
        return nullptr;
    }
    return image;
}

ValidationResult validateDeclarationImage(CPtr<agentc::ListreeValue> image) {
    auto manifest = namedValue(image, "manifest");
    auto declarations = namedValue(image, "declarations");
    if (!manifest || !declarations || !declarations->isListMode()) {
        return fail("invalid_shape", "static declaration image requires manifest and list declarations");
    }
    if (stringValue(namedValue(manifest, "format")) != "agentc.static_declaration_image") {
        return fail("invalid_format", "static declaration image format mismatch");
    }
    if (stringValue(namedValue(manifest, "format_version")) != "1") {
        return fail("unsupported_version", "static declaration image format_version is unsupported");
    }
    if (stringValue(namedValue(manifest, "contains_native_handles")) != "false") {
        return fail("native_handles_forbidden", "static declaration image must not contain native handles");
    }
    if (stringValue(namedValue(manifest, "native_binding_policy")) != "lazy_process_local_sidecar") {
        return fail("invalid_binding_policy", "native bindings must remain process-local sidecars");
    }
    if (stringValue(namedValue(manifest, "payload_hash")) != declarationPayloadHash(declarations)) {
        return fail("payload_hash_mismatch", "static declaration image payload hash mismatch");
    }

    bool symbolError = false;
    std::string missingField;
    declarations->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        auto symbol = ref ? ref->getValue() : nullptr;
        if (symbolError) {
            return;
        }
        if (stringValue(namedValue(symbol, "word")).empty()) {
            symbolError = true;
            missingField = "word";
        } else if (stringValue(namedValue(symbol, "native_symbol")).empty()) {
            symbolError = true;
            missingField = "native_symbol";
        } else if (stringValue(namedValue(symbol, "stores_native_handle")) != "false") {
            symbolError = true;
            missingField = "stores_native_handle=false";
        }
    });
    if (symbolError) {
        return fail("invalid_symbol_declaration", "static declaration symbol missing required field: " + missingField);
    }

    return ValidationResult{true, "ok", "static declaration image is valid"};
}

MountedDeclarationImage mountDeclarationImageReadOnly(CPtr<agentc::ListreeValue> image) {
    MountedDeclarationImage mounted;
    mounted.root = image;
    mounted.rootId = image ? image.getSlabId() : SlabId();
    mounted.validation = validateDeclarationImage(image);
    if (!mounted.validation.ok || !image) {
        return mounted;
    }

    // Freeze logically before marking slots immortal; setReadOnly mutates flags
    // and therefore must not run after a real OS read-only mapping is active.
    image->setReadOnly(true);

    auto& allocator = Allocator<agentc::ListreeValue>::getAllocator();
    std::vector<SlabId> slots;
    image->traverse([&](CPtr<agentc::ListreeValue> value) {
        if (!value) {
            return;
        }
        const SlabId sid = value.getSlabId();
        if (std::find(slots.begin(), slots.end(), sid) == slots.end()) {
            slots.push_back(sid);
        }
    });
    const SlabId rootSid = mounted.rootId;
    if (std::find(slots.begin(), slots.end(), rootSid) == slots.end()) {
        slots.push_back(rootSid);
    }

    for (const auto& sid : slots) {
        if (allocator.markSlotStaticImmortal(sid)) {
            mounted.staticValueSlots.push_back(sid);
        }
    }
    return mounted;
}

MountedDeclarationImage mountDeclarationImageReadOnly(CPtr<agentc::ListreeValue> image,
                                                     agentc::ListreeStaticMountRegistry& registry) {
    MountedDeclarationImage mounted;
    mounted.root = image;
    mounted.rootId = image ? image.getSlabId() : SlabId();
    mounted.validation = validateDeclarationImage(image);
    if (!mounted.validation.ok || !image) {
        return mounted;
    }

    auto manifest = namedValue(image, "manifest");
    agentc::ListreeStaticMountMetadata metadata;
    metadata.manifestHash = stringValue(namedValue(manifest, "payload_hash"));
    metadata.rootDescriptor = stringValue(namedValue(manifest, "root_id"));
    metadata.sectionDescriptor = stringValue(namedValue(manifest, "image_kind")) + ":" + stringValue(namedValue(manifest, "module"));
    metadata.provenance = "static_declaration_image";
    metadata.imageId = stringValue(namedValue(manifest, "module")) + ":" + metadata.manifestHash;
    metadata.root1ResourceDescriptor = "root1.static_mount/" + metadata.imageId;
    metadata.sections.push_back(agentc::ListreeStaticMountSectionDescriptor{
        "manifest", "object", 0, 1});
    metadata.sections.push_back(agentc::ListreeStaticMountSectionDescriptor{
        "declarations", "list", 0, listValueCount(namedValue(image, "declarations"))});

    // The registry-backed mount keeps logical mount identity separate from the
    // native/process-local static ownership marks.  Freeze before the registry
    // lease is created because real static images become OS read-only after this
    // mutable preparation phase.
    image->setReadOnly(true);
    mounted.mountId = registry.mountActiveRoot(image, std::move(metadata));
    if (mounted.mountId == 0) {
        mounted.validation = ValidationResult{false, "mount_failed", "failed to register static declaration image mount"};
    }
    return mounted;
}

struct AdvertisedMountEntry {
    agentc::root1::ResourceKey key;
    std::unique_ptr<agentc::root1::ResourceState> state;
};

std::unordered_map<uint64_t, AdvertisedMountEntry>& getAdvertisedMountsTable() {
    static auto* table = new std::unordered_map<uint64_t, AdvertisedMountEntry>();
    return *table;
}

static std::mutex g_advertisedMountsMutex;

bool advertiseStaticMount(uint64_t mountId, 
                          const agentc::ListreeStaticMountRegistry& registry,
                          agentc::root1::Root1ResourceBroker& broker) {
    if (!registry.active(mountId)) {
        return false;
    }

    const auto metadata = registry.metadata(mountId);
    const auto rootId = registry.rootId(mountId);

    agentc::root1::ResourceKey key;
    key.layerId = static_cast<uint32_t>(mountId);
    key.slabId = rootId.first;
    key.offset = rootId.second;
    key.allocatorKind = 1; // ListreeValue static
    key.fieldId = 0;

    // Compute a deterministic generation hash from metadata.imageId
    uint64_t gen = 0;
    for (char c : metadata.imageId) {
        gen = gen * 31 + static_cast<uint64_t>(c);
    }
    key.generation = gen;

    std::lock_guard<std::mutex> lock(g_advertisedMountsMutex);
    auto& table = getAdvertisedMountsTable();
    auto& entry = table[mountId];
    entry.key = key;
    if (!entry.state) {
        entry.state = std::make_unique<agentc::root1::ResourceState>();
    }

    agentc::root1::ParticipantId systemParticipant = 1;
    if (!broker.hasParticipant(systemParticipant)) {
        systemParticipant = broker.registerParticipant();
    }

    broker.tryAcquire(*entry.state, systemParticipant);
    return broker.registerLease(key, *entry.state, systemParticipant, -1ull);
}

} // namespace agentc::edict::static_image
