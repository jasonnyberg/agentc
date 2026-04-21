#include "agentc_stdlib.h"

#include "../cartographer/boxing.h"
#include "../cartographer/ltv_api.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

static LTV decode_ltv_handle(ltv value) {
    return LTV(static_cast<uint16_t>(value & 0xffffu),
               static_cast<uint16_t>((value >> 16) & 0xffffu));
}

static ltv encode_ltv_handle(LTV value) {
    return static_cast<ltv>(static_cast<uint32_t>(value.first)
                            | (static_cast<uint32_t>(value.second) << 16));
}

static std::string ltv_to_string(LTV value) {
    const void* data = ltv_data(value);
    const size_t length = ltv_length(value);
    if (!data || length == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(data), length);
}

static size_t scalar_size_from_ltv(ltv ctype_name) {
    const std::string ctype = ltv_to_string(decode_ltv_handle(ctype_name));
    if (ctype.empty()) {
        return 0;
    }
    return agentc::cartographer::Boxing::scalarSize(ctype);
}

} // namespace

extern "C" void* agentc_ext_memory_alloc(unsigned long size) {
    return std::malloc(static_cast<size_t>(size));
}

extern "C" void* agentc_ext_memory_calloc(unsigned long count, unsigned long size) {
    return std::calloc(static_cast<size_t>(count), static_cast<size_t>(size));
}

extern "C" void agentc_ext_memory_free(void* ptr) {
    std::free(ptr);
}

extern "C" void agentc_ext_memory_zero(void* ptr, unsigned long size) {
    if (!ptr) {
        return;
    }
    std::memset(ptr, 0, static_cast<size_t>(size));
}

extern "C" void* agentc_ext_memory_slice(void* ptr, unsigned long offset) {
    if (!ptr) {
        return nullptr;
    }
    return static_cast<unsigned char*>(ptr) + static_cast<size_t>(offset);
}

extern "C" void* agentc_ext_string_to_cstr_ltv(ltv value) {
    const LTV handle = decode_ltv_handle(value);
    const void* data = ltv_data(handle);
    const size_t length = ltv_length(handle);
    char* copy = static_cast<char*>(std::malloc(length + 1));
    if (!copy) {
        return nullptr;
    }
    if (data && length > 0) {
        std::memcpy(copy, data, length);
    }
    copy[length] = '\0';
    return copy;
}

extern "C" ltv agentc_ext_string_from_cstr(void* ptr) {
    if (!ptr) {
        return 0;
    }
    const char* text = static_cast<const char*>(ptr);
    return encode_ltv_handle(ltv_create_string(text, std::strlen(text)));
}

extern "C" unsigned long agentc_ext_string_length_ltv(ltv value) {
    return static_cast<unsigned long>(ltv_length(decode_ltv_handle(value)));
}

extern "C" unsigned long agentc_ext_type_size_ltv(ltv ctype_name) {
    return static_cast<unsigned long>(scalar_size_from_ltv(ctype_name));
}

extern "C" int agentc_ext_memory_write_scalar_ltv(void* dest, unsigned long offset, ltv ctype_name, ltv value) {
    if (!dest) {
        return -1;
    }
    const std::string ctype = ltv_to_string(decode_ltv_handle(ctype_name));
    if (ctype.empty()) {
        return 1;
    }
    unsigned char* bytes = static_cast<unsigned char*>(dest) + static_cast<size_t>(offset);
    return ltv_pack_scalar(ctype.c_str(), decode_ltv_handle(value), bytes);
}

extern "C" ltv agentc_ext_memory_read_scalar_ltv(void* src, unsigned long offset, ltv ctype_name) {
    if (!src) {
        return 0;
    }
    const std::string ctype = ltv_to_string(decode_ltv_handle(ctype_name));
    if (ctype.empty()) {
        return 0;
    }
    const unsigned char* bytes = static_cast<const unsigned char*>(src) + static_cast<size_t>(offset);
    return encode_ltv_handle(ltv_unpack_scalar(ctype.c_str(), bytes));
}

extern "C" int agentc_ext_memory_write_array_scalar_ltv(void* dest,
                                                          unsigned long index,
                                                          unsigned long stride,
                                                          unsigned long field_offset,
                                                          ltv ctype_name,
                                                          ltv value) {
    const size_t offset = static_cast<size_t>(index) * static_cast<size_t>(stride) + static_cast<size_t>(field_offset);
    return agentc_ext_memory_write_scalar_ltv(dest, static_cast<unsigned long>(offset), ctype_name, value);
}

extern "C" ltv agentc_ext_memory_read_array_scalar_ltv(void* src,
                                                         unsigned long index,
                                                         unsigned long stride,
                                                         unsigned long field_offset,
                                                         ltv ctype_name) {
    const size_t offset = static_cast<size_t>(index) * static_cast<size_t>(stride) + static_cast<size_t>(field_offset);
    return agentc_ext_memory_read_scalar_ltv(src, static_cast<unsigned long>(offset), ctype_name);
}

extern "C" ltv agentc_ext_binary_pack_scalar_ltv(ltv ctype_name, ltv value) {
    const std::string ctype = ltv_to_string(decode_ltv_handle(ctype_name));
    const size_t size = scalar_size_from_ltv(ctype_name);
    if (ctype.empty() || size == 0) {
        return 0;
    }
    std::vector<unsigned char> bytes(size);
    if (ltv_pack_scalar(ctype.c_str(), decode_ltv_handle(value), bytes.data()) != 0) {
        return 0;
    }
    return encode_ltv_handle(ltv_create_binary(bytes.data(), bytes.size()));
}

extern "C" ltv agentc_ext_binary_concat_ltv(ltv left, ltv right) {
    const LTV leftHandle = decode_ltv_handle(left);
    const LTV rightHandle = decode_ltv_handle(right);
    const unsigned char* leftData = static_cast<const unsigned char*>(ltv_data(leftHandle));
    const unsigned char* rightData = static_cast<const unsigned char*>(ltv_data(rightHandle));
    const size_t leftLength = ltv_length(leftHandle);
    const size_t rightLength = ltv_length(rightHandle);

    std::vector<unsigned char> bytes;
    bytes.reserve(leftLength + rightLength);
    if (leftData && leftLength > 0) {
        bytes.insert(bytes.end(), leftData, leftData + leftLength);
    }
    if (rightData && rightLength > 0) {
        bytes.insert(bytes.end(), rightData, rightData + rightLength);
    }
    return encode_ltv_handle(ltv_create_binary(bytes.data(), bytes.size()));
}

extern "C" ltv agentc_ext_binary_slice_ltv(ltv binary, unsigned long offset, unsigned long length) {
    const LTV handle = decode_ltv_handle(binary);
    const unsigned char* data = static_cast<const unsigned char*>(ltv_data(handle));
    const size_t total = ltv_length(handle);
    const size_t start = static_cast<size_t>(offset);
    if (!data || start > total) {
        return 0;
    }
    const size_t requested = static_cast<size_t>(length);
    const size_t available = total - start;
    const size_t count = requested < available ? requested : available;
    return encode_ltv_handle(ltv_create_binary(data + start, count));
}

extern "C" ltv agentc_ext_binary_view_scalar_ltv(ltv binary, unsigned long offset, ltv ctype_name) {
    const LTV handle = decode_ltv_handle(binary);
    const unsigned char* data = static_cast<const unsigned char*>(ltv_data(handle));
    const size_t total = ltv_length(handle);
    const size_t size = scalar_size_from_ltv(ctype_name);
    const size_t start = static_cast<size_t>(offset);
    if (!data || size == 0 || start + size > total) {
        return 0;
    }
    return agentc_ext_memory_read_scalar_ltv(const_cast<unsigned char*>(data), static_cast<unsigned long>(start), ctype_name);
}
