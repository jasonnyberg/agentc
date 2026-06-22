// edict_vm_overlay.cpp — G093 Overlay Dictionary VM operations
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Implements explicit overlay dictionaries: shared frozen structure with
// worker-local shadow values.  The frozen shared base is never mutated;
// shadow overrides live in a mutable local object.  Lookup checks shadows
// first, then falls through to the frozen shared base.

#include "edict_vm.h"
#include "../listree/listree.h"

namespace agentc::edict {

static bool overlayToString(CPtr<agentc::ListreeValue> v, std::string& out) {
    if (!v || !v->getData() || v->getLength() == 0) return false;
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return false;
    out.assign(static_cast<const char*>(v->getData()), v->getLength());
    return true;
}

static CPtr<agentc::ListreeValue> overlayField(CPtr<agentc::ListreeValue> obj, const std::string& name) {
    if (!obj || obj->isListMode()) return nullptr;
    auto item = obj->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

// overlay.new! ( frozen_shared -- overlay )
// Creates {"shared": frozen_shared, "shadows": {}}
void EdictVM::op_OVERLAY_NEW() {
    auto shared = popData();
    if (!shared) {
        setError("overlay.new requires a shared base value");
        return;
    }
    auto overlay = agentc::createNullValue();
    agentc::addNamedItem(overlay, "shared", shared);
    agentc::addNamedItem(overlay, "shadows", agentc::createNullValue());
    pushData(overlay);
}

// overlay.set! ( overlay key value -- overlay )
// Sets a shadow value for key in the overlay's shadows dict.
void EdictVM::op_OVERLAY_SET() {
    auto value = popData();
    auto keyVal = popData();
    auto overlay = popData();

    std::string key;
    if (!overlayToString(keyVal, key)) {
        setError("overlay.set requires a string key");
        return;
    }
    if (!overlay) {
        setError("overlay.set requires an overlay");
        return;
    }

    auto shadows = overlayField(overlay, "shadows");
    if (!shadows) {
        setError("overlay.set: overlay has no shadows field");
        return;
    }

    agentc::addNamedItem(shadows, key, value ? value : agentc::createNullValue());
    pushData(overlay);
}

// overlay.get! ( overlay key -- value )
// Looks up key: shadow first, then frozen shared.
void EdictVM::op_OVERLAY_GET() {
    auto keyVal = popData();
    auto overlay = popData();

    std::string key;
    if (!overlayToString(keyVal, key)) {
        setError("overlay.get requires a string key");
        return;
    }
    if (!overlay) {
        setError("overlay.get requires an overlay");
        return;
    }

    auto shadows = overlayField(overlay, "shadows");
    if (shadows) {
        auto shadowItem = shadows->find(key);
        if (shadowItem) {
            pushData(shadowItem->getValue(false, false));
            return;
        }
    }

    auto shared = overlayField(overlay, "shared");
    if (shared) {
        auto sharedItem = shared->find(key);
        if (sharedItem) {
            pushData(sharedItem->getValue(false, false));
            return;
        }
    }

    pushData(agentc::createNullValue());
}

// overlay.has! ( overlay key -- ok )
// Returns ["ok"] if key exists in shadow or shared, [] otherwise.
void EdictVM::op_OVERLAY_HAS() {
    auto keyVal = popData();
    auto overlay = popData();

    std::string key;
    if (!overlayToString(keyVal, key)) {
        setError("overlay.has requires a string key");
        return;
    }
    if (!overlay) {
        setError("overlay.has requires an overlay");
        return;
    }

    bool found = false;
    auto shadows = overlayField(overlay, "shadows");
    if (shadows && shadows->find(key)) {
        found = true;
    }
    if (!found) {
        auto shared = overlayField(overlay, "shared");
        if (shared && shared->find(key)) {
            found = true;
        }
    }

    pushData(found ? agentc::createListValue() : agentc::createNullValue());
    if (found) {
        auto list = peekData();
        agentc::addListItem(list, agentc::createStringValue("ok"));
    }
}

// overlay.keys! ( overlay -- key_list )
// Merged key list: shadow keys first, then shared-only keys.
void EdictVM::op_OVERLAY_KEYS() {
    auto overlay = popData();
    if (!overlay) {
        setError("overlay.keys requires an overlay");
        return;
    }

    auto result = agentc::createListValue();
    std::unordered_set<std::string> seen;

    auto shadows = overlayField(overlay, "shadows");
    if (shadows) {
        shadows->forEachTree([&](const std::string& name, CPtr<ListreeItem>&) {
            if (seen.find(name) == seen.end()) {
                seen.insert(name);
                agentc::addListItem(result, agentc::createStringValue(name));
            }
        });
    }

    auto shared = overlayField(overlay, "shared");
    if (shared) {
        shared->forEachTree([&](const std::string& name, CPtr<ListreeItem>&) {
            if (seen.find(name) == seen.end()) {
                seen.insert(name);
                agentc::addListItem(result, agentc::createStringValue(name));
            }
        });
    }

    pushData(result);
}

// overlay.shadow_keys! ( overlay -- shadow_key_list )
// Only the keys that have local shadow overrides.
void EdictVM::op_OVERLAY_SHADOW_KEYS() {
    auto overlay = popData();
    if (!overlay) {
        setError("overlay.shadow_keys requires an overlay");
        return;
    }

    auto result = agentc::createListValue();
    auto shadows = overlayField(overlay, "shadows");
    if (shadows) {
        shadows->forEachTree([&](const std::string& name, CPtr<ListreeItem>&) {
            agentc::addListItem(result, agentc::createStringValue(name));
        });
    }

    pushData(result);
}

// overlay.commit! ( overlay -- shadows )
// Extracts the shadow dictionary for coordinator inspection.
void EdictVM::op_OVERLAY_COMMIT() {
    auto overlay = popData();
    if (!overlay) {
        setError("overlay.commit requires an overlay");
        return;
    }

    auto shadows = overlayField(overlay, "shadows");
    pushData(shadows ? shadows : agentc::createNullValue());
}

} // namespace agentc::edict
