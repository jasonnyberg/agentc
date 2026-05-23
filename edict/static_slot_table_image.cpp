// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "static_slot_table_image.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace agentc::edict::static_image {
namespace {

constexpr char kMagic[] = {'A', 'C', 'S', 'T', 'B', 'L', '0', '1'};
constexpr uint32_t kVersion = 2;

struct Header {
    uint32_t version = 0;
    uint32_t moduleName = 0;
    uint32_t rootValue = 0;
    uint32_t stringCount = 0;
    uint32_t declarationCount = 0;
    uint32_t valueCount = 0;
    uint32_t treeCount = 0;
    uint32_t itemCount = 0;
    uint32_t listEntryCount = 0;
    uint64_t stringRecordBytes = 0;
    uint64_t declarationRecordBytes = 0;
    uint64_t valueRecordBytes = 0;
    uint64_t treeRecordBytes = 0;
    uint64_t itemRecordBytes = 0;
    uint64_t listEntryBytes = 0;
    uint64_t stringBytes = 0;
    char payloadHash[17] = {};
};

struct StringRecord {
    uint64_t offset = 0;
    uint64_t length = 0;
};

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

std::string fnv1a64(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : bytes) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

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

void appendValueRecord(std::string& out, const StaticSlotTableValueRecord& value) {
    appendU32(out, static_cast<uint32_t>(value.kind));
    appendU32(out, value.stringId);
    appendU32(out, value.first);
    appendU32(out, value.count);
}

bool readValueRecord(const char* data,
                     size_t size,
                     size_t& cursor,
                     StaticSlotTableValueRecord& value) {
    uint32_t kind = 0;
    if (!readU32(data, size, cursor, kind) ||
        !readU32(data, size, cursor, value.stringId) ||
        !readU32(data, size, cursor, value.first) ||
        !readU32(data, size, cursor, value.count)) {
        return false;
    }
    value.kind = static_cast<StaticSlotValueKind>(kind);
    return true;
}

void appendTreeRecord(std::string& out, const StaticSlotTableTreeRecord& tree) {
    appendU32(out, tree.firstItem);
    appendU32(out, tree.itemCount);
}

bool readTreeRecord(const char* data,
                    size_t size,
                    size_t& cursor,
                    StaticSlotTableTreeRecord& tree) {
    return readU32(data, size, cursor, tree.firstItem) &&
           readU32(data, size, cursor, tree.itemCount);
}

void appendItemRecord(std::string& out, const StaticSlotTableItemRecord& item) {
    appendU32(out, item.name);
    appendU32(out, item.value);
}

bool readItemRecord(const char* data,
                    size_t size,
                    size_t& cursor,
                    StaticSlotTableItemRecord& item) {
    return readU32(data, size, cursor, item.name) &&
           readU32(data, size, cursor, item.value);
}

void appendDeclarationRecord(std::string& out, const StaticSlotTableDeclaration& declaration) {
    appendU32(out, declaration.word);
    appendU32(out, declaration.nativeSymbol);
    appendU32(out, declaration.stackSignature);
    appendU32(out, declaration.category);
    appendU32(out, declaration.binding);
    appendU32(out, declaration.storesNativeHandle);
    appendU32(out, declaration.workerAllowed);
    appendU32(out, declaration.notes);
}

bool readDeclarationRecord(const char* data,
                           size_t size,
                           size_t& cursor,
                           StaticSlotTableDeclaration& declaration) {
    return readU32(data, size, cursor, declaration.word) &&
           readU32(data, size, cursor, declaration.nativeSymbol) &&
           readU32(data, size, cursor, declaration.stackSignature) &&
           readU32(data, size, cursor, declaration.category) &&
           readU32(data, size, cursor, declaration.binding) &&
           readU32(data, size, cursor, declaration.storesNativeHandle) &&
           readU32(data, size, cursor, declaration.workerAllowed) &&
           readU32(data, size, cursor, declaration.notes);
}

ValidationResult fail(const std::string& code, const std::string& message) {
    return ValidationResult{false, code, message};
}

} // namespace

std::string StaticSlotTableView::stringAt(uint32_t id) const {
    if (id >= stringOffsets_.size() || !stringBytes_) {
        return {};
    }
    const uint64_t offset = stringOffsets_[id];
    const uint64_t length = stringLengths_[id];
    if (offset > stringBytesSize_ || length > stringBytesSize_ || offset + length > stringBytesSize_) {
        return {};
    }
    return std::string(stringBytes_ + offset, static_cast<size_t>(length));
}

StaticSlotValueKind StaticSlotTableView::valueKind(uint32_t valueId) const {
    return valueId < values_.size() ? values_[valueId].kind : StaticSlotValueKind::Invalid;
}

size_t StaticSlotTableView::listValueCount(uint32_t valueId) const {
    if (valueId >= values_.size() || values_[valueId].kind != StaticSlotValueKind::List) {
        return 0;
    }
    return values_[valueId].count;
}

uint32_t StaticSlotTableView::listValueAt(uint32_t valueId, size_t index) const {
    if (valueId >= values_.size() || values_[valueId].kind != StaticSlotValueKind::List) {
        return 0;
    }
    const auto& value = values_[valueId];
    if (index >= value.count || value.first + index >= listEntries_.size()) {
        return 0;
    }
    return listEntries_[value.first + index];
}

std::string StaticSlotTableView::objectStringField(uint32_t valueId, const std::string& fieldName) const {
    if (valueId >= values_.size() || values_[valueId].kind != StaticSlotValueKind::Object || values_[valueId].first >= trees_.size()) {
        return {};
    }
    const auto& tree = trees_[values_[valueId].first];
    size_t low = 0;
    size_t high = tree.itemCount;
    while (low < high) {
        const size_t mid = low + ((high - low) / 2);
        if (tree.firstItem + mid >= items_.size()) {
            return {};
        }
        const auto& item = items_[tree.firstItem + mid];
        const std::string name = stringAt(item.name);
        if (name == fieldName) {
            if (item.value < values_.size() && values_[item.value].kind == StaticSlotValueKind::String) {
                return stringAt(values_[item.value].stringId);
            }
            return {};
        }
        if (name < fieldName) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return {};
}

uint32_t StaticSlotTableView::declarationValueId(size_t index) const {
    return index < declarationValueIds_.size() ? declarationValueIds_[index] : 0;
}

std::string StaticSlotTableView::declarationWord(size_t index) const {
    return index < declarations_.size() ? stringAt(declarations_[index].word) : std::string();
}

std::string StaticSlotTableView::declarationNativeSymbol(size_t index) const {
    return index < declarations_.size() ? stringAt(declarations_[index].nativeSymbol) : std::string();
}

std::string StaticSlotTableView::declarationStackSignature(size_t index) const {
    return index < declarations_.size() ? stringAt(declarations_[index].stackSignature) : std::string();
}

std::string StaticSlotTableView::declarationCategory(size_t index) const {
    return index < declarations_.size() ? stringAt(declarations_[index].category) : std::string();
}

bool writeStaticSlotTableImage(CPtr<agentc::ListreeValue> declarationImage,
                               const std::string& path,
                               std::string* error) {
    const auto validation = validateDeclarationImage(declarationImage);
    if (!validation.ok) {
        if (error) {
            *error = validation.code + ": " + validation.message;
        }
        return false;
    }

    std::vector<std::string> strings;
    std::unordered_map<std::string, uint32_t> stringIds;
    auto intern = [&](const std::string& text) -> uint32_t {
        auto it = stringIds.find(text);
        if (it != stringIds.end()) {
            return it->second;
        }
        const uint32_t id = static_cast<uint32_t>(strings.size());
        strings.push_back(text);
        stringIds[text] = id;
        return id;
    };

    auto manifest = namedValue(declarationImage, "manifest");
    const uint32_t moduleName = intern(stringValue(namedValue(manifest, "module")));

    std::vector<StaticSlotTableDeclaration> declarations;
    std::vector<uint32_t> declarationValueIds;
    std::vector<StaticSlotTableValueRecord> values;
    std::vector<StaticSlotTableTreeRecord> trees;
    std::vector<StaticSlotTableItemRecord> items;
    std::vector<uint32_t> listEntries;

    auto makeStringValue = [&](const std::string& text) -> uint32_t {
        const uint32_t id = static_cast<uint32_t>(values.size());
        values.push_back({StaticSlotValueKind::String, intern(text), 0, 0});
        return id;
    };
    auto makeStringField = [&](uint32_t nameId, const std::string& text) {
        return StaticSlotTableItemRecord{nameId, makeStringValue(text)};
    };

    const uint32_t wordName = intern("word");
    const uint32_t nativeSymbolName = intern("native_symbol");
    const uint32_t stackSignatureName = intern("stack_signature");
    const uint32_t categoryName = intern("category");
    const uint32_t bindingName = intern("binding");
    const uint32_t storesNativeHandleName = intern("stores_native_handle");
    const uint32_t workerAllowedName = intern("worker_allowed");
    const uint32_t notesName = intern("notes");

    auto declarationList = namedValue(declarationImage, "declarations");
    declarationList->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        auto symbol = ref ? ref->getValue() : nullptr;
        if (!symbol) {
            return;
        }
        StaticSlotTableDeclaration declaration;
        declaration.word = intern(stringValue(namedValue(symbol, "word")));
        declaration.nativeSymbol = intern(stringValue(namedValue(symbol, "native_symbol")));
        declaration.stackSignature = intern(stringValue(namedValue(symbol, "stack_signature")));
        declaration.category = intern(stringValue(namedValue(symbol, "category")));
        declaration.binding = intern(stringValue(namedValue(symbol, "binding")));
        declaration.storesNativeHandle = intern(stringValue(namedValue(symbol, "stores_native_handle")));
        declaration.workerAllowed = intern(stringValue(namedValue(symbol, "worker_allowed")));
        declaration.notes = intern(stringValue(namedValue(symbol, "notes")));
        declarations.push_back(declaration);

        std::vector<StaticSlotTableItemRecord> objectItems;
        objectItems.push_back(makeStringField(wordName, stringValue(namedValue(symbol, "word"))));
        objectItems.push_back(makeStringField(nativeSymbolName, stringValue(namedValue(symbol, "native_symbol"))));
        objectItems.push_back(makeStringField(stackSignatureName, stringValue(namedValue(symbol, "stack_signature"))));
        objectItems.push_back(makeStringField(categoryName, stringValue(namedValue(symbol, "category"))));
        objectItems.push_back(makeStringField(bindingName, stringValue(namedValue(symbol, "binding"))));
        objectItems.push_back(makeStringField(storesNativeHandleName, stringValue(namedValue(symbol, "stores_native_handle"))));
        objectItems.push_back(makeStringField(workerAllowedName, stringValue(namedValue(symbol, "worker_allowed"))));
        objectItems.push_back(makeStringField(notesName, stringValue(namedValue(symbol, "notes"))));
        std::sort(objectItems.begin(), objectItems.end(), [&](const auto& lhs, const auto& rhs) {
            return strings[lhs.name] < strings[rhs.name];
        });
        const uint32_t firstItem = static_cast<uint32_t>(items.size());
        items.insert(items.end(), objectItems.begin(), objectItems.end());
        const uint32_t treeId = static_cast<uint32_t>(trees.size());
        trees.push_back({firstItem, static_cast<uint32_t>(objectItems.size())});
        const uint32_t objectId = static_cast<uint32_t>(values.size());
        values.push_back({StaticSlotValueKind::Object, 0, treeId, 1});
        declarationValueIds.push_back(objectId);
        listEntries.push_back(objectId);
    });
    const uint32_t rootValue = static_cast<uint32_t>(values.size());
    values.push_back({StaticSlotValueKind::List, 0, 0, static_cast<uint32_t>(listEntries.size())});

    std::string stringRecords;
    std::string stringBytes;
    for (const auto& text : strings) {
        appendU64(stringRecords, static_cast<uint64_t>(stringBytes.size()));
        appendU64(stringRecords, static_cast<uint64_t>(text.size()));
        stringBytes.append(text);
    }

    std::string declarationRecords;
    for (const auto& declaration : declarations) {
        appendDeclarationRecord(declarationRecords, declaration);
    }

    std::string valueRecords;
    for (const auto& value : values) {
        appendValueRecord(valueRecords, value);
    }

    std::string treeRecords;
    for (const auto& tree : trees) {
        appendTreeRecord(treeRecords, tree);
    }

    std::string itemRecords;
    for (const auto& item : items) {
        appendItemRecord(itemRecords, item);
    }

    std::string listEntryRecords;
    for (const auto& entry : listEntries) {
        appendU32(listEntryRecords, entry);
    }

    std::string body = stringRecords + declarationRecords + valueRecords + treeRecords + itemRecords + listEntryRecords + stringBytes;
    const std::string hash = fnv1a64(body);

    std::string out;
    out.append(kMagic, sizeof(kMagic));
    appendU32(out, kVersion);
    appendU32(out, moduleName);
    appendU32(out, rootValue);
    appendU32(out, static_cast<uint32_t>(strings.size()));
    appendU32(out, static_cast<uint32_t>(declarations.size()));
    appendU32(out, static_cast<uint32_t>(values.size()));
    appendU32(out, static_cast<uint32_t>(trees.size()));
    appendU32(out, static_cast<uint32_t>(items.size()));
    appendU32(out, static_cast<uint32_t>(listEntries.size()));
    appendU64(out, static_cast<uint64_t>(stringRecords.size()));
    appendU64(out, static_cast<uint64_t>(declarationRecords.size()));
    appendU64(out, static_cast<uint64_t>(valueRecords.size()));
    appendU64(out, static_cast<uint64_t>(treeRecords.size()));
    appendU64(out, static_cast<uint64_t>(itemRecords.size()));
    appendU64(out, static_cast<uint64_t>(listEntryRecords.size()));
    appendU64(out, static_cast<uint64_t>(stringBytes.size()));
    out.append(hash);
    out.append(body);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error) {
            *error = "failed to open static slot table image for writing: " + path;
        }
        return false;
    }
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

StaticSlotTableView readStaticSlotTableImageMmapReadOnly(const std::string& path,
                                                         std::string* error) {
    StaticSlotTableView view;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        view.validation_ = fail("open_failed", "failed to open static slot table image: " + path);
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        view.validation_ = fail("stat_failed", "failed to stat static slot table image: " + path);
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }

    const size_t size = static_cast<size_t>(st.st_size);
    void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        view.validation_ = fail("mmap_failed", std::string("failed to mmap static slot table image read-only: ") + std::strerror(errno));
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }

    std::shared_ptr<void> region(mapped, [size](void* ptr) {
        if (ptr && ptr != MAP_FAILED) {
            ::munmap(ptr, size);
        }
    });
    const char* bytes = static_cast<const char*>(mapped);
    size_t cursor = 0;
    if (size < sizeof(kMagic) || std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
        view.validation_ = fail("invalid_magic", "static slot table image magic mismatch");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    cursor += sizeof(kMagic);

    Header header;
    if (!readU32(bytes, size, cursor, header.version) ||
        !readU32(bytes, size, cursor, header.moduleName) ||
        !readU32(bytes, size, cursor, header.rootValue) ||
        !readU32(bytes, size, cursor, header.stringCount) ||
        !readU32(bytes, size, cursor, header.declarationCount) ||
        !readU32(bytes, size, cursor, header.valueCount) ||
        !readU32(bytes, size, cursor, header.treeCount) ||
        !readU32(bytes, size, cursor, header.itemCount) ||
        !readU32(bytes, size, cursor, header.listEntryCount) ||
        !readU64(bytes, size, cursor, header.stringRecordBytes) ||
        !readU64(bytes, size, cursor, header.declarationRecordBytes) ||
        !readU64(bytes, size, cursor, header.valueRecordBytes) ||
        !readU64(bytes, size, cursor, header.treeRecordBytes) ||
        !readU64(bytes, size, cursor, header.itemRecordBytes) ||
        !readU64(bytes, size, cursor, header.listEntryBytes) ||
        !readU64(bytes, size, cursor, header.stringBytes)) {
        view.validation_ = fail("truncated_header", "static slot table image header is truncated");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    if (cursor + 16 > size) {
        view.validation_ = fail("truncated_hash", "static slot table image hash is truncated");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    std::memcpy(header.payloadHash, bytes + cursor, 16);
    header.payloadHash[16] = '\0';
    cursor += 16;

    if (header.version != kVersion) {
        view.validation_ = fail("unsupported_version", "static slot table image version is unsupported");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    if (header.stringRecordBytes != static_cast<uint64_t>(header.stringCount) * 16ull ||
        header.declarationRecordBytes != static_cast<uint64_t>(header.declarationCount) * 32ull ||
        header.valueRecordBytes != static_cast<uint64_t>(header.valueCount) * 16ull ||
        header.treeRecordBytes != static_cast<uint64_t>(header.treeCount) * 8ull ||
        header.itemRecordBytes != static_cast<uint64_t>(header.itemCount) * 8ull ||
        header.listEntryBytes != static_cast<uint64_t>(header.listEntryCount) * 4ull ||
        cursor + header.stringRecordBytes + header.declarationRecordBytes + header.valueRecordBytes + header.treeRecordBytes + header.itemRecordBytes + header.listEntryBytes + header.stringBytes != size) {
        view.validation_ = fail("invalid_lengths", "static slot table image section lengths are invalid");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }

    const char* body = bytes + cursor;
    const size_t bodySize = static_cast<size_t>(header.stringRecordBytes + header.declarationRecordBytes + header.valueRecordBytes + header.treeRecordBytes + header.itemRecordBytes + header.listEntryBytes + header.stringBytes);
    if (fnv1a64(std::string(body, bodySize)) != std::string(header.payloadHash)) {
        view.validation_ = fail("payload_hash_mismatch", "static slot table image payload hash mismatch");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }

    view.stringOffsets_.reserve(header.stringCount);
    view.stringLengths_.reserve(header.stringCount);
    for (uint32_t i = 0; i < header.stringCount; ++i) {
        uint64_t offset = 0;
        uint64_t length = 0;
        if (!readU64(bytes, size, cursor, offset) || !readU64(bytes, size, cursor, length) ||
            offset > header.stringBytes || length > header.stringBytes || offset + length > header.stringBytes) {
            view.validation_ = fail("invalid_string_record", "static slot table image string record is invalid");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.stringOffsets_.push_back(offset);
        view.stringLengths_.push_back(length);
    }

    auto validStringId = [&](uint32_t id) { return id < header.stringCount; };

    view.declarations_.reserve(header.declarationCount);
    for (uint32_t i = 0; i < header.declarationCount; ++i) {
        StaticSlotTableDeclaration declaration;
        if (!readDeclarationRecord(bytes, size, cursor, declaration)) {
            view.validation_ = fail("invalid_declaration_record", "static slot table image declaration record is truncated");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        if (!validStringId(declaration.word) || !validStringId(declaration.nativeSymbol) ||
            !validStringId(declaration.stackSignature) || !validStringId(declaration.category) ||
            !validStringId(declaration.binding) || !validStringId(declaration.storesNativeHandle) ||
            !validStringId(declaration.workerAllowed) || !validStringId(declaration.notes)) {
            view.validation_ = fail("invalid_declaration_reference", "static slot table image declaration references an invalid string id");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.declarations_.push_back(declaration);
    }

    view.values_.reserve(header.valueCount);
    for (uint32_t i = 0; i < header.valueCount; ++i) {
        StaticSlotTableValueRecord value;
        if (!readValueRecord(bytes, size, cursor, value)) {
            view.validation_ = fail("invalid_value_record", "static slot table image value record is truncated");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        if (value.kind == StaticSlotValueKind::String && !validStringId(value.stringId)) {
            view.validation_ = fail("invalid_value_reference", "static slot table string value references an invalid string id");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        if (value.kind == StaticSlotValueKind::Object && (value.first >= header.treeCount || value.count != 1)) {
            view.validation_ = fail("invalid_value_reference", "static slot table object value references invalid tree id");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        if (value.kind == StaticSlotValueKind::List && (value.first > header.listEntryCount || value.count > header.listEntryCount || value.first + value.count > header.listEntryCount)) {
            view.validation_ = fail("invalid_value_reference", "static slot table list value references invalid entry range");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        if (value.kind != StaticSlotValueKind::String && value.kind != StaticSlotValueKind::Object && value.kind != StaticSlotValueKind::List) {
            view.validation_ = fail("invalid_value_kind", "static slot table value kind is unsupported");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.values_.push_back(value);
    }

    view.trees_.reserve(header.treeCount);
    for (uint32_t i = 0; i < header.treeCount; ++i) {
        StaticSlotTableTreeRecord tree;
        if (!readTreeRecord(bytes, size, cursor, tree) ||
            tree.firstItem > header.itemCount || tree.itemCount > header.itemCount || tree.firstItem + tree.itemCount > header.itemCount) {
            view.validation_ = fail("invalid_tree_record", "static slot table tree record is invalid");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.trees_.push_back(tree);
    }

    view.items_.reserve(header.itemCount);
    for (uint32_t i = 0; i < header.itemCount; ++i) {
        StaticSlotTableItemRecord item;
        if (!readItemRecord(bytes, size, cursor, item) || !validStringId(item.name) || item.value >= header.valueCount) {
            view.validation_ = fail("invalid_item_record", "static slot table item record is invalid");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.items_.push_back(item);
    }

    for (const auto& tree : view.trees_) {
        for (uint32_t i = 1; i < tree.itemCount; ++i) {
            const auto& previous = view.items_[tree.firstItem + i - 1];
            const auto& current = view.items_[tree.firstItem + i];
            if (view.stringAt(previous.name) > view.stringAt(current.name)) {
                view.validation_ = fail("unsorted_tree_items", "static slot table tree items must be sorted by field name");
                if (error) {
                    *error = view.validation_.message;
                }
                return view;
            }
        }
    }

    view.listEntries_.reserve(header.listEntryCount);
    for (uint32_t i = 0; i < header.listEntryCount; ++i) {
        uint32_t entry = 0;
        if (!readU32(bytes, size, cursor, entry) || entry >= header.valueCount) {
            view.validation_ = fail("invalid_list_entry", "static slot table list entry is invalid");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.listEntries_.push_back(entry);
    }

    view.mappedRegion_ = std::move(region);
    view.stringBytes_ = bytes + cursor;
    view.stringBytesSize_ = static_cast<size_t>(header.stringBytes);
    view.moduleName_ = header.moduleName;
    view.rootValueId_ = header.rootValue;
    if (view.rootValueId_ >= header.valueCount || view.valueKind(view.rootValueId_) != StaticSlotValueKind::List) {
        view.validation_ = fail("invalid_root_reference", "static slot table root value is invalid");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    if (view.listValueCount(view.rootValueId_) != header.declarationCount) {
        view.validation_ = fail("invalid_root_list", "static slot table root list does not match declaration count");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    for (uint32_t i = 0; i < header.declarationCount; ++i) {
        const uint32_t objectId = view.listValueAt(view.rootValueId_, i);
        if (objectId >= header.valueCount || view.valueKind(objectId) != StaticSlotValueKind::Object) {
            view.validation_ = fail("invalid_root_list", "static slot table root list entry is not an object");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
        view.declarationValueIds_.push_back(objectId);
    }
    if (view.moduleName_ >= header.stringCount) {
        view.validation_ = fail("invalid_module_reference", "static slot table image module name references an invalid string id");
        if (error) {
            *error = view.validation_.message;
        }
        return view;
    }
    for (size_t i = 0; i < view.declarations_.size(); ++i) {
        if (view.stringAt(view.declarations_[i].storesNativeHandle) != "false") {
            view.validation_ = fail("native_handles_forbidden", "static slot table declaration stores a native handle");
            if (error) {
                *error = view.validation_.message;
            }
            return view;
        }
    }

    view.validation_ = ValidationResult{true, "ok", "static slot table image is valid"};
    return view;
}

} // namespace agentc::edict::static_image
