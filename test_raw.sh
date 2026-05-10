#!/bin/bash
./build/edict/edict - << 'INNER'
["build/extensions/libagentc_extensions.so"] ["cpp-agent/include/agentc_extensions/agentc_extensions.h"] resolver.import ! @ext
["build/cpp-agent/libagent_runtime.so"] ["cpp-agent/include/agentc_runtime/agentc_runtime.h"] resolver.import ! @runtimeffi
INNER
