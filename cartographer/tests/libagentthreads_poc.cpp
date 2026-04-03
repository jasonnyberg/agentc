#include "libagentthreads_poc.h"

#include "../ltv_api.h"
#include "../../listree/listree.h"

#include <pthread.h>
#include <mutex>

struct agentc_thread_handle {
    pthread_t thread{};
    LtvUnaryOp entry = nullptr;
    PointerUnaryStatusOp status_entry = nullptr;
    ltv arg = 0;
    void* status_arg = nullptr;
    ltv result = 0;
    int status = 0;
    std::mutex mutex;
    bool finished = false;
    bool joined = false;
    bool detached = false;
};

struct agentc_shared_value {
    std::mutex mutex;
    ltv value = 0;
};

namespace {

static LTV decode_ltv_handle(ltv value) {
    return LTV(static_cast<uint16_t>(value & 0xffffu),
               static_cast<uint16_t>((value >> 16) & 0xffffu));
}

static ltv encode_ltv_handle(LTV value) {
    return static_cast<ltv>(static_cast<uint32_t>(value.first)
                            | (static_cast<uint32_t>(value.second) << 16));
}

ltv snapshot_ltv(ltv value) {
    if (value == 0) {
        return 0;
    }

    CPtr<agentc::ListreeValue> borrowed = agentc::ltv_borrow(decode_ltv_handle(value));
    if (!borrowed) {
        return 0;
    }

    CPtr<agentc::ListreeValue> copied = borrowed->copy();
    return copied ? encode_ltv_handle(copied.release()) : 0;
}

void cleanup_thread_handle(agentc_thread_handle* handle) {
    if (!handle) {
        return;
    }
    if (handle->arg != 0) {
        ltv_unref(decode_ltv_handle(handle->arg));
        handle->arg = 0;
    }
    if (handle->result != 0) {
        ltv_unref(decode_ltv_handle(handle->result));
        handle->result = 0;
    }
    delete handle;
}

void* thread_main(void* user_data) {
    auto* handle = static_cast<agentc_thread_handle*>(user_data);
    if (!handle) {
        return nullptr;
    }

    ltv arg = 0;
    LtvUnaryOp entry = nullptr;
    PointerUnaryStatusOp status_entry = nullptr;
    void* status_arg = nullptr;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        arg = handle->arg;
        handle->arg = 0;
        entry = handle->entry;
        status_entry = handle->status_entry;
        status_arg = handle->status_arg;
    }

    ltv raw_result = 0;
    int status = 0;
    if (entry) {
        raw_result = entry(arg);
    } else if (status_entry) {
        status = status_entry(status_arg);
    } else if (arg != 0) {
        ltv_unref(decode_ltv_handle(arg));
    }

    ltv result_snapshot = 0;
    if (raw_result != 0) {
        result_snapshot = snapshot_ltv(raw_result);
        ltv_unref(decode_ltv_handle(raw_result));
    }

    bool self_delete = false;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        handle->result = result_snapshot;
        handle->status = status;
        handle->finished = true;
        self_delete = handle->detached;
    }

    if (self_delete) {
        cleanup_thread_handle(handle);
    }
    return nullptr;
}

} // namespace

extern "C" {

agentc_thread_handle* agentc_thread_spawn_ltv(LtvUnaryOp entry, ltv arg) {
    if (!entry) {
        return nullptr;
    }

    auto* handle = new agentc_thread_handle();
    handle->entry = entry;
    handle->arg = snapshot_ltv(arg);

    if (pthread_create(&handle->thread, nullptr, thread_main, handle) != 0) {
        cleanup_thread_handle(handle);
        return nullptr;
    }

    return handle;
}

agentc_thread_handle* agentc_thread_spawn_status(PointerUnaryStatusOp entry, void* arg) {
    if (!entry) {
        return nullptr;
    }

    auto* handle = new agentc_thread_handle();
    handle->status_entry = entry;
    handle->status_arg = arg;

    if (pthread_create(&handle->thread, nullptr, thread_main, handle) != 0) {
        cleanup_thread_handle(handle);
        return nullptr;
    }

    return handle;
}

ltv agentc_thread_join_ltv(agentc_thread_handle* handle) {
    if (!handle) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (handle->detached) {
            return 0;
        }
    }

    if (!handle->joined) {
        if (pthread_join(handle->thread, nullptr) != 0) {
            return 0;
        }
        handle->joined = true;
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    ltv joined = snapshot_ltv(handle->result);
    return joined;
}

int agentc_thread_join_status(agentc_thread_handle* handle) {
    if (!handle) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (handle->detached) {
            return 0;
        }
    }

    if (!handle->joined) {
        if (pthread_join(handle->thread, nullptr) != 0) {
            return 0;
        }
        handle->joined = true;
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    return handle->status;
}

void agentc_thread_detach(agentc_thread_handle* handle) {
    if (!handle) {
        return;
    }

    bool delete_now = false;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (handle->joined || handle->detached) {
            return;
        }
        handle->detached = true;
        delete_now = handle->finished;
    }

    if (!delete_now) {
        pthread_detach(handle->thread);
        return;
    }

    cleanup_thread_handle(handle);
}

void agentc_thread_destroy(agentc_thread_handle* handle) {
    if (!handle) {
        return;
    }

    bool detached = false;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        detached = handle->detached;
    }

    if (!detached && !handle->joined) {
        if (pthread_join(handle->thread, nullptr) == 0) {
            handle->joined = true;
        }
    }

    if (detached) {
        bool finished = false;
        {
            std::lock_guard<std::mutex> lock(handle->mutex);
            finished = handle->finished;
        }
        if (!finished) {
            return;
        }
    }

    cleanup_thread_handle(handle);
}

agentc_shared_value* agentc_shared_create_ltv(ltv initial) {
    auto* cell = new agentc_shared_value();
    cell->value = snapshot_ltv(initial);
    return cell;
}

void agentc_shared_destroy(agentc_shared_value* cell) {
    if (!cell) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(cell->mutex);
        if (cell->value != 0) {
            ltv_unref(decode_ltv_handle(cell->value));
            cell->value = 0;
        }
    }
    delete cell;
}

ltv agentc_shared_read_ltv(agentc_shared_value* cell) {
    if (!cell) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(cell->mutex);
    ltv out = snapshot_ltv(cell->value);
    return out;
}

int agentc_shared_write_ltv(agentc_shared_value* cell, ltv replacement) {
    if (!cell) {
        return 0;
    }

    ltv snapshot = snapshot_ltv(replacement);
    std::lock_guard<std::mutex> lock(cell->mutex);
    if (cell->value != 0) {
        ltv_unref(decode_ltv_handle(cell->value));
    }
    cell->value = snapshot;
    return 1;
}

} // extern "C"
