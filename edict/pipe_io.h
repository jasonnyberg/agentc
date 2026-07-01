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

/// @file pipe_io.h
/// Shared inline pipe I/O primitives used by the TCC coordinator
/// (tcc_runtime.cpp, compiled into libedict.so) and the TCC worker exec
/// (tcc_worker_native.cpp, compiled into agentc_tcc_worker_exec).
///
/// All functions handle EINTR transparently. Do NOT use these as a replacement
/// for the copies in edict_worker_runtime.cpp or cartographer/service.cpp —
/// those files have different EINTR-handling semantics and separate consumers.

#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>
#include <unistd.h>

namespace agentc::edict::tcc {

inline bool writeAll(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, bytes, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        bytes += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

inline bool readAll(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const ssize_t count = ::read(fd, bytes, remaining);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        bytes += count;
        remaining -= static_cast<std::size_t>(count);
    }
    return true;
}

inline bool writeString(int fd, const std::string& value) {
    const uint64_t length = static_cast<uint64_t>(value.size());
    return writeAll(fd, &length, sizeof(length)) &&
           (value.empty() || writeAll(fd, value.data(), value.size()));
}

inline bool readString(int fd, std::string& value) {
    uint64_t length = 0;
    if (!readAll(fd, &length, sizeof(length))) {
        return false;
    }
    value.assign(static_cast<std::size_t>(length), '\0');
    return length == 0 || readAll(fd, value.data(), value.size());
}

inline bool writeStringVector(int fd, const std::vector<std::string>& values) {
    const uint64_t count = static_cast<uint64_t>(values.size());
    if (!writeAll(fd, &count, sizeof(count))) {
        return false;
    }
    for (const auto& value : values) {
        if (!writeString(fd, value)) {
            return false;
        }
    }
    return true;
}

inline bool readStringVector(int fd, std::vector<std::string>& values) {
    uint64_t count = 0;
    if (!readAll(fd, &count, sizeof(count))) {
        return false;
    }
    values.clear();
    values.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        std::string value;
        if (!readString(fd, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

} // namespace agentc::edict::tcc
