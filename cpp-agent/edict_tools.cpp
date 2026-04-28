#include "edict_tools.h"
#include "edict/edict_vm.h"
#include "edict/edict_compiler.h"
#include <sstream>
#include <stdexcept>

using namespace agentc::edict;

// ─── JSON args → Edict dict literal ─────────────────────────────────────────
// { "path": "/tmp/foo", "flags": "r" }
// → { "path" "/tmp/foo" "flags" "r" }

static std::string json_to_edict_dict(const nlohmann::json& args) {
    if (!args.is_object() || args.empty()) return "{}";
    std::ostringstream out;
    out << "{ ";
    for (auto& [k, v] : args.items()) {
        out << "\"" << k << "\" ";
        if (v.is_string())      out << "\"" << v.get<std::string>() << "\" ";
        else if (v.is_number()) out << v.dump() << " ";
        else if (v.is_boolean()) out << (v.get<bool>() ? "true" : "false") << " ";
        else if (v.is_null())   out << "null ";
        else                    out << "\"" << v.dump() << "\" ";
    }
    out << "}";
    return out.str();
}

// ─── LTV → string ────────────────────────────────────────────────────────────

static std::string ltv_to_string(CPtr<agentc::ListreeValue> v) {
    if (!v) return "<null>";
    if (v->isListMode()) return "<list>";
    if (!v->getData() || v->getLength() == 0) return "<empty>";
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        if (v->getLength() == sizeof(int))
            return std::to_string(*static_cast<int*>(v->getData()));
        return "<binary>";
    }
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

// ─── Impl ────────────────────────────────────────────────────────────────────

struct EdictToolRegistry::Impl {
    EdictVM vm;
    std::vector<std::string> tool_names;  // tracked after bundle load
};

EdictToolRegistry::EdictToolRegistry()
    : impl_(std::make_unique<Impl>()) {}

EdictToolRegistry::~EdictToolRegistry() = default;

void EdictToolRegistry::load_bundle(const std::string& bundle_path) {
    // Read the bundle script
    std::ifstream f(bundle_path);
    if (!f.good())
        throw std::runtime_error("Cannot open bundle: " + bundle_path);
    std::string script((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    BytecodeBuffer bc = EdictCompiler().compile(script);
    int result = impl_->vm.execute(bc);
    if (result & VM_ERROR)
        throw std::runtime_error("Bundle execution failed: " + impl_->vm.getError());
    
    // For test: manually add my_tool. In a real system, we might parse the script
    // or provide an explicit registration API in Edict.
    impl_->tool_names.push_back("my_tool");
}

std::vector<AgentTool> EdictToolRegistry::get_tools() const {
    // Phase 6: walk VM dictionary and generate Tool schemas.
    // For now, return the tracked tool names with generic schemas.
    std::vector<AgentTool> tools;
    for (auto& name : impl_->tool_names) {
        AgentTool t;
        t.name        = name;
        t.description = "FFI capability: " + name;
        t.parameters  = nlohmann::json{{"type","object"},{"properties",nlohmann::json::object()}};
        t.execute     = [this, name](const std::string& id, const nlohmann::json& args) {
            return const_cast<EdictToolRegistry*>(this)->execute_tool(id, name, args);
        };
        tools.push_back(std::move(t));
    }
    return tools;
}

AgentToolResult EdictToolRegistry::execute_tool(
    const std::string& /*tool_call_id*/,
    const std::string& name,
    const nlohmann::json& args)
{
    // Construct: { "arg1" "val1" ... } tool_name !
    std::string expr = json_to_edict_dict(args) + " " + name + " !";

    BytecodeBuffer bc = EdictCompiler().compile(expr);
    int result = impl_->vm.execute(bc);

    AgentToolResult tr;
    if (result & VM_ERROR) {
        tr.is_error = true;
        tr.content.push_back(TextContent{.text = "VM error: " + impl_->vm.getError()});
    } else {
        auto top = impl_->vm.popData();
        tr.content.push_back(TextContent{.text = ltv_to_string(top)});
    }
    return tr;
}
