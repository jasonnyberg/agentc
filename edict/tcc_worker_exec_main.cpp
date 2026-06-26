// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "tcc_runtime.h"

int main(int argc, char** argv) {
    return agentc::edict::tcc::runTccWorkerExecChildMain(argc, argv);
}
