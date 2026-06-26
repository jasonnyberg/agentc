// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include <string>

namespace {

class DemoAdapterCore {
public:
    long long scale(long long value) const {
        return value * 3 + 7;
    }

    std::string providerName() const {
        return "agentc-deltagui-adapter-probe";
    }
};

} // namespace

extern "C" long long agentc_tcc_test_scale(long long value) {
    static DemoAdapterCore core;
    return core.scale(value);
}

extern "C" const char* agentc_tcc_test_provider_name() {
    static DemoAdapterCore core;
    static std::string name = core.providerName();
    return name.c_str();
}
