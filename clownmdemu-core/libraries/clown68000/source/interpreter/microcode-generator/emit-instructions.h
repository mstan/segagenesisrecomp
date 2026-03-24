/*
    clown68000 - A Motorola 68000 emulator.
    Copyright (C) 2021-2023  Clownacy

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef INCLUDE_GUARD_0BB73C0B_EA27_45D8_AA97_E98FBFD79EFC
#define INCLUDE_GUARD_0BB73C0B_EA27_45D8_AA97_E98FBFD79EFC

#include "../../../libraries/common/instruction.h"

void EmitInstructionSupervisorCheck(const Instruction instruction);
void EmitInstructionSize(const Instruction instruction);
void EmitInstructionSourceAddressMode(const Instruction instruction);
void EmitInstructionDestinationAddressMode(const Instruction instruction);
void EmitInstructionReadSourceOperand(const Instruction instruction);
void EmitInstructionReadDestinationOperand(const Instruction instruction);
void EmitInstructionAction(const Instruction instruction);
void EmitInstructionWriteDestinationOperand(const Instruction instruction);
void EmitInstructionConditionCodes(const Instruction instruction);

#endif /* INCLUDE_GUARD_0BB73C0B_EA27_45D8_AA97_E98FBFD79EFC */
