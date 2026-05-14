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

// EdictVM is implemented across purpose-oriented translation units:
// - edict_vm_core.cpp: VM lifecycle, core stack/eval/control/rewrite/cursor ops,
//   closures, and the centralized opcode dispatch loop.
// - edict_vm_ffi.cpp: Cartographer/import/native FFI opcode handlers.
// - edict_vm_bootstrap.cpp: built-in registration and startup curation prelude.
