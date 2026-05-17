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

#include "../listree/listree.h"

namespace agentc::edict::intern {

CPtr<agentc::ListreeValue> activeCount();
CPtr<agentc::ListreeValue> run(CPtr<agentc::ListreeValue> task,
                               bool allowUnsafeFfiCalls = false);
CPtr<agentc::ListreeValue> start(CPtr<agentc::ListreeValue> task,
                                 bool allowUnsafeFfiCalls = false);
CPtr<agentc::ListreeValue> drainEvents(CPtr<agentc::ListreeValue> jobOrRequest);
CPtr<agentc::ListreeValue> collect(CPtr<agentc::ListreeValue> jobOrRequest,
                                   CPtr<agentc::ListreeValue> events);
CPtr<agentc::ListreeValue> sync(CPtr<agentc::ListreeValue> jobOrRequest);
CPtr<agentc::ListreeValue> cancel(CPtr<agentc::ListreeValue> jobOrRequest);

} // namespace agentc::edict::intern
