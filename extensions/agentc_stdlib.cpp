#include "agentc_stdlib.h"

#include "../cartographer/boxing.h"
#include "../cartographer/ltv_api.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
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

static std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (ch < 0x20) {
                    std::ostringstream escaped;
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                    out += escaped.str();
                } else {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    return out;
}

static std::string cstr_to_string(void* ptr) {
    if (!ptr) {
        return {};
    }
    return std::string(static_cast<const char*>(ptr));
}

static void* malloc_string_copy(const std::string& payload) {
    char* copy = static_cast<char*>(std::malloc(payload.size() + 1));
    if (!copy) {
        return nullptr;
    }
    std::memcpy(copy, payload.c_str(), payload.size());
    copy[payload.size()] = '\0';
    return copy;
}

static std::string json_error_object(const std::string& code, const std::string& message) {
    return std::string("{\"code\":\"") + json_escape(code) + "\",\"message\":\"" + json_escape(message) + "\"}";
}

static std::string tool_failure_json(const std::string& op,
                                     const std::string& path_or_command,
                                     const std::string& code,
                                     const std::string& message) {
    return std::string("{\"ok\":[],\"op\":\"") + json_escape(op) +
           "\",\"target\":\"" + json_escape(path_or_command) +
           "\",\"error\":" + json_error_object(code, message) + "}";
}

static std::string errno_message(const std::string& prefix) {
    return prefix + ": " + std::strerror(errno);
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

extern "C" ltv agentc_ext_string_equals_ltv_value(ltv left, ltv right) {
    const bool equal = ltv_to_string(decode_ltv_handle(left)) == ltv_to_string(decode_ltv_handle(right));
    if (equal) {
        return encode_ltv_handle(ltv_create_string("ok", 2));
    }
    return encode_ltv_handle(ltv_create_null());
}

extern "C" void* agentc_ext_stdin_read_line_cstr(void) {
    std::string line;
    if (!std::getline(std::cin, line)) {
        return nullptr;
    }

    char* copy = static_cast<char*>(std::malloc(line.size() + 1));
    if (!copy) {
        return nullptr;
    }
    std::memcpy(copy, line.c_str(), line.size());
    copy[line.size()] = '\0';
    return copy;
}

extern "C" void* agentc_ext_stdin_read_line_status_json_cstr(void) {
    std::string line;
    std::string payload;
    if (!std::getline(std::cin, line)) {
        payload = R"({"line":null,"eof":["eof"]})";
    } else {
        payload = std::string("{\"line\":\"") + json_escape(line) + "\",\"eof\":[]}";
    }

    char* copy = static_cast<char*>(std::malloc(payload.size() + 1));
    if (!copy) {
        return nullptr;
    }
    std::memcpy(copy, payload.c_str(), payload.size());
    copy[payload.size()] = '\0';
    return copy;
}

extern "C" int agentc_ext_stdout_write_cstr(void* ptr) {
    if (!ptr) {
        return -1;
    }
    std::cout << static_cast<const char*>(ptr);
    std::cout.flush();
    return 0;
}

extern "C" void* agentc_ext_file_read_json_cstr(void* path_ptr, unsigned long max_bytes) {
    const std::string pathText = cstr_to_string(path_ptr);
    if (pathText.empty()) {
        return malloc_string_copy(tool_failure_json("file_read", pathText, "path_empty", "Path is empty"));
    }

    std::ifstream in(pathText, std::ios::binary);
    if (!in.good()) {
        return malloc_string_copy(tool_failure_json("file_read", pathText, "open_failed", errno_message("Failed to open file for read")));
    }

    const size_t limit = static_cast<size_t>(max_bytes == 0 ? 65536ul : max_bytes);
    std::string content;
    content.reserve(std::min<size_t>(limit, 4096));
    std::array<char, 4096> buffer{};
    bool truncated = false;
    while (in.good()) {
        const size_t remaining = limit > content.size() ? limit - content.size() : 0;
        if (remaining == 0) {
            truncated = (in.peek() != EOF);
            break;
        }
        const size_t want = std::min(buffer.size(), remaining);
        in.read(buffer.data(), static_cast<std::streamsize>(want));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            content.append(buffer.data(), static_cast<size_t>(got));
        }
        if (static_cast<size_t>(got) < want) {
            break;
        }
    }

    std::string payload = std::string("{\"ok\":[\"ok\"],\"op\":\"file_read\",\"path\":\"") + json_escape(pathText) +
                          "\",\"content\":\"" + json_escape(content) +
                          "\",\"bytes\":" + std::to_string(content.size()) +
                          ",\"truncated\":" + (truncated ? "[\"truncated\"]" : "[]") +
                          ",\"error\":null}";
    return malloc_string_copy(payload);
}

extern "C" void* agentc_ext_file_write_json_cstr(void* path_ptr, void* content_ptr) {
    const std::string pathText = cstr_to_string(path_ptr);
    const std::string content = cstr_to_string(content_ptr);
    if (pathText.empty()) {
        return malloc_string_copy(tool_failure_json("file_write", pathText, "path_empty", "Path is empty"));
    }

    std::error_code ec;
    const std::filesystem::path path(pathText);
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return malloc_string_copy(tool_failure_json("file_write", pathText, "mkdir_failed", ec.message()));
        }
    }

    std::ofstream out(pathText, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return malloc_string_copy(tool_failure_json("file_write", pathText, "open_failed", errno_message("Failed to open file for write")));
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out.good()) {
        return malloc_string_copy(tool_failure_json("file_write", pathText, "write_failed", errno_message("Failed to write file")));
    }

    std::string payload = std::string("{\"ok\":[\"ok\"],\"op\":\"file_write\",\"path\":\"") + json_escape(pathText) +
                          "\",\"bytes\":" + std::to_string(content.size()) +
                          ",\"error\":null}";
    return malloc_string_copy(payload);
}

extern "C" void* agentc_ext_file_replace_json_cstr(void* path_ptr, void* old_ptr, void* new_ptr) {
    const std::string pathText = cstr_to_string(path_ptr);
    const std::string oldText = cstr_to_string(old_ptr);
    const std::string newText = cstr_to_string(new_ptr);
    if (pathText.empty()) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText, "path_empty", "Path is empty"));
    }
    if (oldText.empty()) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText, "old_text_empty", "Old text must be non-empty"));
    }

    std::ifstream in(pathText, std::ios::binary);
    if (!in.good()) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText, "open_failed", errno_message("Failed to open file for replace")));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();

    size_t count = 0;
    size_t pos = 0;
    size_t match = std::string::npos;
    while ((pos = content.find(oldText, pos)) != std::string::npos) {
        ++count;
        match = pos;
        pos += oldText.size();
        if (count > 1) {
            break;
        }
    }
    if (count != 1) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText,
                                                    count == 0 ? "old_text_not_found" : "old_text_not_unique",
                                                    count == 0 ? "Old text was not found" : "Old text matched more than once"));
    }

    content.replace(match, oldText.size(), newText);
    std::ofstream out(pathText, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText, "open_failed", errno_message("Failed to open file for write")));
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out.good()) {
        return malloc_string_copy(tool_failure_json("file_replace", pathText, "write_failed", errno_message("Failed to write replacement")));
    }

    std::string payload = std::string("{\"ok\":[\"ok\"],\"op\":\"file_replace\",\"path\":\"") + json_escape(pathText) +
                          "\",\"replacements\":1,\"bytes\":" + std::to_string(content.size()) +
                          ",\"error\":null}";
    return malloc_string_copy(payload);
}

extern "C" void* agentc_ext_shell_exec_json_cstr(void* command_ptr, unsigned long max_bytes) {
    const std::string command = cstr_to_string(command_ptr);
    if (command.empty()) {
        return malloc_string_copy(tool_failure_json("shell_exec", command, "command_empty", "Command is empty"));
    }

    const size_t limit = static_cast<size_t>(max_bytes == 0 ? 65536ul : max_bytes);
    const std::string shellCommand = command + " 2>&1";
    FILE* pipe = popen(shellCommand.c_str(), "r");
    if (!pipe) {
        return malloc_string_copy(tool_failure_json("shell_exec", command, "popen_failed", errno_message("Failed to start command")));
    }

    std::string output;
    output.reserve(std::min<size_t>(limit, 4096));
    std::array<char, 4096> buffer{};
    bool truncated = false;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        const std::string chunk(buffer.data());
        if (output.size() < limit) {
            const size_t remaining = limit - output.size();
            output.append(chunk.data(), std::min(chunk.size(), remaining));
            if (chunk.size() > remaining) {
                truncated = true;
            }
        } else {
            truncated = true;
        }
    }

    const int closeStatus = pclose(pipe);
    int exitCode = closeStatus;
    bool exited = false;
    if (closeStatus != -1) {
        if (WIFEXITED(closeStatus)) {
            exited = true;
            exitCode = WEXITSTATUS(closeStatus);
        } else if (WIFSIGNALED(closeStatus)) {
            exitCode = 128 + WTERMSIG(closeStatus);
        }
    }
    const bool ok = (closeStatus != -1 && exited && exitCode == 0);
    const std::string error = ok ? "null" : json_error_object("command_failed", "Command exited with status " + std::to_string(exitCode));
    std::string payload = std::string("{\"ok\":") + (ok ? "[\"ok\"]" : "[]") +
                          ",\"op\":\"shell_exec\",\"command\":\"" + json_escape(command) +
                          "\",\"exit_code\":" + std::to_string(exitCode) +
                          ",\"output\":\"" + json_escape(output) +
                          "\",\"bytes\":" + std::to_string(output.size()) +
                          ",\"truncated\":" + (truncated ? "[\"truncated\"]" : "[]") +
                          ",\"error\":" + error + "}";
    return malloc_string_copy(payload);
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
