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

#include "runtime_ffi.h"

#include <limits>
#include <string>
#include <vector>

#include "logic_evaluator.h"

namespace {

using agentc::ListreeValue;

struct HandleSlot {
    uint32_t generation = 1;
    uint32_t refcount = 0;
    ::CPtr<ListreeValue> value;
    bool occupied = false;
};

struct RuntimeContextImpl {
    std::vector<HandleSlot> slots;
    std::vector<uint32_t> freeSlots;
    std::string lastError;

    void clearError() {
        lastError.clear();
    }

    void setError(const std::string& error) {
        lastError = error;
    }
};

static_assert(sizeof(agentc_value) == sizeof(uint64_t), "agentc_value must be 64-bit");

uint32_t handleIndex(agentc_value handle) {
    return static_cast<uint32_t>(handle & 0xFFFFFFFFu);
}

uint32_t handleGeneration(agentc_value handle) {
    return static_cast<uint32_t>((handle >> 32) & 0xFFFFFFFFu);
}

agentc_value makeHandle(uint32_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(index);
}

HandleSlot* resolveSlot(RuntimeContextImpl* ctx, agentc_value handle, bool setError) {
    if (!ctx || handle == AGENTC_VALUE_NULL) {
        if (ctx && setError) {
            ctx->setError("invalid runtime value handle");
        }
        return nullptr;
    }

    uint32_t index = handleIndex(handle);
    uint32_t generation = handleGeneration(handle);
    if (index == 0 || index > ctx->slots.size()) {
        if (setError) {
            ctx->setError("runtime value handle is out of range");
        }
        return nullptr;
    }

    auto& slot = ctx->slots[index - 1];
    if (!slot.occupied || slot.generation != generation || !slot.value) {
        if (setError) {
            ctx->setError("runtime value handle is stale or invalid");
        }
        return nullptr;
    }

    return &slot;
}

agentc_value storeValue(RuntimeContextImpl* ctx, ::CPtr<ListreeValue> value) {
    if (!ctx) {
        return AGENTC_VALUE_NULL;
    }

    if (!value) {
        value = agentc::createNullValue();
    }

    uint32_t index = 0;
    if (!ctx->freeSlots.empty()) {
        index = ctx->freeSlots.back();
        ctx->freeSlots.pop_back();
    } else {
        if (ctx->slots.size() >= std::numeric_limits<uint32_t>::max()) {
            ctx->setError("runtime handle table exhausted");
            return AGENTC_VALUE_NULL;
        }
        ctx->slots.emplace_back();
        index = static_cast<uint32_t>(ctx->slots.size());
    }

    auto& slot = ctx->slots[index - 1];
    slot.occupied = true;
    slot.refcount = 1;
    slot.value = value;
    return makeHandle(index, slot.generation);
}

void releaseValue(RuntimeContextImpl* ctx, agentc_value handle) {
    auto* slot = resolveSlot(ctx, handle, false);
    if (!slot) {
        return;
    }

    if (slot->refcount > 1) {
        --slot->refcount;
        return;
    }

    slot->refcount = 0;
    slot->occupied = false;
    slot->value = nullptr;
    ++slot->generation;
    uint32_t index = handleIndex(handle);
    ctx->freeSlots.push_back(index);
}

} // namespace

struct agentc_runtime_ctx {
    RuntimeContextImpl impl;
};

extern "C" {

agentc_runtime_ctx* agentc_runtime_create(void) {
    return new agentc_runtime_ctx();
}

void agentc_runtime_destroy(agentc_runtime_ctx* ctx) {
    delete ctx;
}

agentc_value agentc_runtime_value_from_ltv(agentc_runtime_ctx* ctx, LTV value) {
    if (!ctx) {
        return AGENTC_VALUE_NULL;
    }

    ctx->impl.clearError();
    auto cptr = (value == LTV_NULL) ? agentc::createNullValue() : agentc::ltv_borrow(value);
    return storeValue(&ctx->impl, cptr);
}

LTV agentc_runtime_value_to_ltv(agentc_runtime_ctx* ctx, agentc_value value) {
    if (!ctx) {
        return LTV_NULL;
    }

    ctx->impl.clearError();
    auto* slot = resolveSlot(&ctx->impl, value, true);
    if (!slot) {
        return LTV_NULL;
    }

    ::CPtr<ListreeValue> owned = slot->value;
    return owned.release();
}

void agentc_runtime_value_retain(agentc_runtime_ctx* ctx, agentc_value value) {
    if (!ctx) {
        return;
    }

    ctx->impl.clearError();
    auto* slot = resolveSlot(&ctx->impl, value, true);
    if (!slot) {
        return;
    }

    ++slot->refcount;
}

void agentc_runtime_value_release(agentc_runtime_ctx* ctx, agentc_value value) {
    if (!ctx) {
        return;
    }

    ctx->impl.clearError();
    releaseValue(&ctx->impl, value);
}

agentc_value agentc_runtime_copy_last_error(agentc_runtime_ctx* ctx) {
    if (!ctx) {
        return AGENTC_VALUE_NULL;
    }

    auto errorValue = agentc::createStringValue(ctx->impl.lastError);
    return storeValue(&ctx->impl, errorValue);
}

agentc_value agentc_logic_eval(agentc_runtime_ctx* ctx, agentc_value spec) {
    if (!ctx) {
        return AGENTC_VALUE_NULL;
    }

    ctx->impl.clearError();
    auto* slot = resolveSlot(&ctx->impl, spec, true);
    if (!slot) {
        return AGENTC_VALUE_NULL;
    }

    auto result = agentc::kanren::evaluateLogicSpec(slot->value);
    if (!result.ok) {
        ctx->impl.setError(result.error);
        return AGENTC_VALUE_NULL;
    }

    return storeValue(&ctx->impl, result.value);
}

LTV agentc_logic_eval_ltv(LTV spec) {
    ::CPtr<ListreeValue> value = (spec == LTV_NULL) ? agentc::createNullValue() : agentc::ltv_borrow(spec);
    auto result = agentc::kanren::evaluateLogicSpec(value);
    if (!result.ok) {
        return LTV_NULL;
    }

    return result.value.release();
}

} // extern "C"
