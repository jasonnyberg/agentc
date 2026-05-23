// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#pragma once

#include "static_declaration_image.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agentc::edict::static_image {

enum class StaticSlotValueKind : uint32_t {
    Invalid = 0,
    String = 1,
    Object = 2,
    List = 3,
};

struct StaticSlotTableDeclaration {
    uint32_t word = 0;
    uint32_t nativeSymbol = 0;
    uint32_t stackSignature = 0;
    uint32_t category = 0;
    uint32_t binding = 0;
    uint32_t storesNativeHandle = 0;
    uint32_t workerAllowed = 0;
    uint32_t notes = 0;
};

struct StaticSlotTableValueRecord {
    StaticSlotValueKind kind = StaticSlotValueKind::Invalid;
    uint32_t stringId = 0;
    uint32_t first = 0;
    uint32_t count = 0;
};

struct StaticSlotTableItemRecord {
    uint32_t name = 0;
    uint32_t value = 0;
};

struct StaticSlotTableTreeRecord {
    uint32_t firstItem = 0;
    uint32_t itemCount = 0;
};

class StaticSlotTableView {
public:
    StaticSlotTableView() = default;

    bool ok() const { return validation_.ok; }
    const ValidationResult& validation() const { return validation_; }

    size_t stringCount() const { return stringOffsets_.size(); }
    size_t declarationCount() const { return declarations_.size(); }
    size_t valueCount() const { return values_.size(); }
    uint32_t rootValueId() const { return rootValueId_; }

    std::string stringAt(uint32_t id) const;
    StaticSlotValueKind valueKind(uint32_t valueId) const;
    size_t listValueCount(uint32_t valueId) const;
    uint32_t listValueAt(uint32_t valueId, size_t index) const;
    std::string objectStringField(uint32_t valueId, const std::string& fieldName) const;
    uint32_t declarationValueId(size_t index) const;
    std::string declarationWord(size_t index) const;
    std::string declarationNativeSymbol(size_t index) const;
    std::string declarationStackSignature(size_t index) const;
    std::string declarationCategory(size_t index) const;
    std::string moduleName() const { return stringAt(moduleName_); }

private:
    friend StaticSlotTableView readStaticSlotTableImageMmapReadOnly(const std::string&, std::string*);

    ValidationResult validation_{false, "uninitialized", "static slot table view is uninitialized"};
    std::shared_ptr<void> mappedRegion_;
    const char* stringBytes_ = nullptr;
    size_t stringBytesSize_ = 0;
    uint32_t moduleName_ = 0;
    uint32_t rootValueId_ = 0;
    std::vector<uint64_t> stringOffsets_;
    std::vector<uint64_t> stringLengths_;
    std::vector<StaticSlotTableDeclaration> declarations_;
    std::vector<uint32_t> declarationValueIds_;
    std::vector<StaticSlotTableValueRecord> values_;
    std::vector<StaticSlotTableTreeRecord> trees_;
    std::vector<StaticSlotTableItemRecord> items_;
    std::vector<uint32_t> listEntries_;
};

bool writeStaticSlotTableImage(CPtr<agentc::ListreeValue> declarationImage,
                               const std::string& path,
                               std::string* error = nullptr);
StaticSlotTableView readStaticSlotTableImageMmapReadOnly(const std::string& path,
                                                         std::string* error = nullptr);

} // namespace agentc::edict::static_image
