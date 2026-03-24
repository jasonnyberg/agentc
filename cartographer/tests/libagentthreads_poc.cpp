#include "libagentthreads_poc.h"

#include "../ltv_api.h"
#include "../../listree/listree.h"

#include <pthread.h>
#include <cstdio>
#include <mutex>

struct agentc_thread_handle {
    pthread_t thread{};
    LtvUnaryOp entry = nullptr;
    LTV arg = LTV_NULL;
    LTV result = LTV_NULL;
    std::mutex mutex;
    bool finished = false;
    bool joined = false;
    bool detached = false;
};

struct agentc_shared_value {
    std::mutex mutex;
    LTV value = LTV_NULL;
};

namespace {

LTV snapshot_ltv(LTV value) {
    if (value == LTV_NULL) {
        return LTV_NULL;
    }

    CPtr<agentc::ListreeValue> borrowed = agentc::ltv_borrow(value);
    if (!borrowed) {
        return LTV_NULL;
    }

    CPtr<agentc::ListreeValue> copied = borrowed->copy();
    return copied ? copied.release() : LTV_NULL;
}

void debug_print_ltv(const char* label, LTV value) {
    std::fprintf(stderr, "[agentthreads] %s handle=%u\n", label, static_cast<unsigned>(value));
    if (value == LTV_NULL) {
        std::fprintf(stderr, "[agentthreads] %s value=<null>\n", label);
        return;
    }

    CPtr<agentc::ListreeValue> borrowed = agentc::ltv_borrow(value);
    if (!borrowed) {
        std::fprintf(stderr, "[agentthreads] %s borrow failed\n", label);
        return;
    }

    std::fprintf(stderr,
                 "[agentthreads] %s ptr=%p len=%zu list=%d data=%p\n",
                 label,
                 static_cast<void*>(borrowed.operator->()),
                 borrowed->getLength(),
                 borrowed->isListMode() ? 1 : 0,
                 borrowed->getData());
    if (borrowed->getData() && borrowed->getLength() > 0) {
        std::fprintf(stderr,
                     "[agentthreads] %s text='%.*s'\n",
                     label,
                     static_cast<int>(borrowed->getLength()),
                     static_cast<const char*>(borrowed->getData()));
    }
}

void cleanup_thread_handle(agentc_thread_handle* handle) {
    if (!handle) {
        return;
    }
    if (handle->arg != LTV_NULL) {
        ltv_unref(handle->arg);
        handle->arg = LTV_NULL;
    }
    if (handle->result != LTV_NULL) {
        ltv_unref(handle->result);
        handle->result = LTV_NULL;
    }
    delete handle;
}

void* thread_main(void* user_data) {
    auto* handle = static_cast<agentc_thread_handle*>(user_data);
    if (!handle) {
        return nullptr;
    }

    LTV arg = LTV_NULL;
    LtvUnaryOp entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        arg = handle->arg;
        handle->arg = LTV_NULL;
        entry = handle->entry;
    }

    std::fprintf(stderr, "[agentthreads] thread_main entry=%p\n", reinterpret_cast<void*>(entry));
    debug_print_ltv("thread_main.arg", arg);

    LTV raw_result = LTV_NULL;
    if (entry) {
        raw_result = entry(arg);
        debug_print_ltv("thread_main.raw_result", raw_result);
    } else if (arg != LTV_NULL) {
        ltv_unref(arg);
    }

    LTV result_snapshot = LTV_NULL;
    if (raw_result != LTV_NULL) {
        result_snapshot = snapshot_ltv(raw_result);
        debug_print_ltv("thread_main.result_snapshot", result_snapshot);
        ltv_unref(raw_result);
    }

    bool self_delete = false;
    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        handle->result = result_snapshot;
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
    std::fprintf(stderr, "[agentthreads] spawn entry=%p\n", reinterpret_cast<void*>(entry));
    debug_print_ltv("spawn.arg", arg);
    debug_print_ltv("spawn.arg_snapshot", handle->arg);

    if (pthread_create(&handle->thread, nullptr, thread_main, handle) != 0) {
        cleanup_thread_handle(handle);
        return nullptr;
    }

    return handle;
}

ltv agentc_thread_join_ltv(agentc_thread_handle* handle) {
    if (!handle) {
        return LTV_NULL;
    }

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        if (handle->detached) {
            return LTV_NULL;
        }
    }

    if (!handle->joined) {
        if (pthread_join(handle->thread, nullptr) != 0) {
            return LTV_NULL;
        }
        handle->joined = true;
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    debug_print_ltv("join.stored_result", handle->result);
    LTV joined = snapshot_ltv(handle->result);
    debug_print_ltv("join.snapshot", joined);
    return joined;
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
    debug_print_ltv("shared.create.initial", initial);
    debug_print_ltv("shared.create.snapshot", cell->value);
    return cell;
}

void agentc_shared_destroy(agentc_shared_value* cell) {
    if (!cell) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(cell->mutex);
        if (cell->value != LTV_NULL) {
            ltv_unref(cell->value);
            cell->value = LTV_NULL;
        }
    }
    delete cell;
}

ltv agentc_shared_read_ltv(agentc_shared_value* cell) {
    if (!cell) {
        return LTV_NULL;
    }
    std::lock_guard<std::mutex> lock(cell->mutex);
    debug_print_ltv("shared.read.stored", cell->value);
    LTV out = snapshot_ltv(cell->value);
    debug_print_ltv("shared.read.snapshot", out);
    return out;
}

int agentc_shared_write_ltv(agentc_shared_value* cell, ltv replacement) {
    if (!cell) {
        return 0;
    }

    LTV snapshot = snapshot_ltv(replacement);
    debug_print_ltv("shared.write.replacement", replacement);
    debug_print_ltv("shared.write.snapshot", snapshot);
    std::lock_guard<std::mutex> lock(cell->mutex);
    if (cell->value != LTV_NULL) {
        ltv_unref(cell->value);
    }
    cell->value = snapshot;
    return 1;
}

} // extern "C"
