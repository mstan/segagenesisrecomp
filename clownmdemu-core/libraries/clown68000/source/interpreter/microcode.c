case INSTRUCTION_ABCD:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_BCDX(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_BCDX(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ABCD(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	Overflow_ADD(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_ADD:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADD(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardCarry(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_ADD(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_ADDA:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeSized(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADDA(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ADDAQ:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADDQ(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ADDI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADD(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardCarry(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_ADD(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_ADDQ:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADDQ(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardCarry(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_ADD(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_ADDX:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_BCDX(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_BCDX(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ADDX(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardCarry(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_ADD(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_AND:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_AND(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ANDI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_AND(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ANDI_TO_CCR:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_ConditionCodeRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_AND(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ANDI_TO_SR:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_StatusRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_AND(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ASD_MEMORY:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ASD_MEMORY(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ASD_REGISTER:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ASD_REGISTER(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCC_SHORT:
	/* Do the actual instruction. */
	Action_BCC_SHORT(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCC_WORD:
	/* Decode source address mode. */
	DecodeSource_ImmediateDataWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_BCC_WORD(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCHG_DYNAMIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BCHG(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCHG_STATIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateDataByte(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BCHG(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCLR_DYNAMIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BCLR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BCLR_STATIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateDataByte(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BCLR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BRA_SHORT:
	/* Do the actual instruction. */
	Action_BRA_SHORT(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BRA_WORD:
	/* Decode source address mode. */
	DecodeSource_ImmediateDataWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_BRA_WORD(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BSET_DYNAMIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BSET(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BSET_STATIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateDataByte(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BSET(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BSR_SHORT:
	/* Do the actual instruction. */
	Action_BSR_SHORT(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BSR_WORD:
	/* Decode source address mode. */
	DecodeSource_ImmediateDataWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_BSR_WORD(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BTST_DYNAMIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BTST(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_BTST_STATIC:
	SetSize_LongwordRegisterByteMemory(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateDataByte(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_BTST(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CHK:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_CHK(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Undefined */
	/* Update OVERFLOW condition code */
	/* Undefined */
	/* Update ZERO condition code */
	/* Undefined */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CLR:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_CLR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CMP:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_CMP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CMPA:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeSized(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_CMPA(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CMPI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_CMPI(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_CMPM:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_AddressRegisterPrimaryPostIncrement(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondaryPostIncrement(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_CMPM(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_DBCC:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_DBCC(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_DIVS:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_DIVS(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_DIVU:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_DIVU(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EOR:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_EOR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EORI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_EOR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EORI_TO_CCR:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_ConditionCodeRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_EOR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EORI_TO_SR:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_StatusRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_EOR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EXG:
	/* Do the actual instruction. */
	Action_EXG(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_EXT:
	SetSize_Ext(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_EXT(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ILLEGAL:
	/* Do the actual instruction. */
	Action_ILLEGAL(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_JMP:
	/* Decode source address mode. */
	DecodeSource_MemoryAddressPrimary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_JMP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_JSR:
	/* Decode source address mode. */
	DecodeSource_MemoryAddressPrimary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_JSR(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_LEA:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_MemoryAddressPrimary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondary(&stuff);

	/* Do the actual instruction. */
	Action_LEA(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_LINK:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_LINK(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_LSD_MEMORY:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_LSD_MEMORY(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_LSD_REGISTER:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_LSD_REGISTER(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVE:
	SetSize_Move(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_SecondaryAddressMode(&stuff);

	/* Do the actual instruction. */
	Action_MOVE(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVE_FROM_SR:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_StatusRegister(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Do the actual instruction. */
	Action_MOVE(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVE_TO_CCR:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_ConditionCodeRegister(&stuff);

	/* Do the actual instruction. */
	Action_MOVE(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVE_TO_SR:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_StatusRegister(&stuff);

	/* Do the actual instruction. */
	Action_MOVE(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVE_USP:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	/* Do the actual instruction. */
	Action_MOVE_USP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVEA:
	SetSize_Move(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondaryFull(&stuff);

	/* Do the actual instruction. */
	Action_MOVEA(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVEM:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_MOVEM(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_MOVEM(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVEP:
	/* Decode destination address mode. */
	DecodeDestination_MOVEP(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_MOVEP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MOVEQ:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Do the actual instruction. */
	Action_MOVEQ(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MULS:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_MULS(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_MULU:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeWord(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_MULU(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_NBCD:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_NBCD(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_NEG:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_NEG(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_NEG(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_NEG(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_NEGX:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_NEGX(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_NEG(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_NEG(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_NOP:
	/* Do the actual instruction. */
	Action_NOP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_NOT:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_NOT(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_OR:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_OR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ORI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_OR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ORI_TO_CCR:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_ConditionCodeRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_OR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ORI_TO_SR:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_StatusRegister(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_OR(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_PEA:
	/* Decode source address mode. */
	DecodeSource_MemoryAddressPrimary(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_PEA(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_RESET:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	/* Do the actual instruction. */
	Action_RESET(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ROD_MEMORY:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ROD_MEMORY(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ROD_REGISTER:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ROD_REGISTER(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ROXD_MEMORY:
	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ROXD_MEMORY(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_ROXD_REGISTER:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_ROXD_REGISTER(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_RTE:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	/* Do the actual instruction. */
	Action_RTE(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_RTR:
	/* Do the actual instruction. */
	Action_RTR(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_RTS:
	/* Do the actual instruction. */
	Action_RTS(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_SBCD:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_BCDX(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_BCDX(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SBCD(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_SCC:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SCC(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_STOP:
	/* Only allow this instruction in supervisor mode. */
	SupervisorCheck(&stuff);

	SetSize_Word(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_STOP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_SUB:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterSecondaryOrPrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUB(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_SUBA:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressModeSized(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_AddressRegisterSecondary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUBA(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_SUBAQ:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUBQ(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_SUBI:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_ImmediateData(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUB(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_SUBQ:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUBQ(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_SUBX:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_BCDX(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_BCDX(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SUBX(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_StandardBorrow(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_SUB(&stuff);
	/* Update ZERO condition code */
	Zero_ClearIfNonZeroUnaffectedOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	Extend_SetToCarry(&stuff);

	break;

case INSTRUCTION_SWAP:
	SetSize_Longword(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_DataRegisterPrimary(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_SWAP(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_TAS:
	SetSize_Byte(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode destination address mode. */
	DecodeDestination_PrimaryAddressMode(&stuff);

	/* Read destination operand. */
	ReadDestination(&stuff);

	/* Do the actual instruction. */
	Action_TAS(&stuff);

	/* Write destination operand. */
	WriteDestination(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_TRAP:
	/* Do the actual instruction. */
	Action_TRAP(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_TRAPV:
	/* Do the actual instruction. */
	Action_TRAPV(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_TST:
	SetSize_Standard(&stuff);
	SetMSBBitIndex(&stuff);

	/* Decode source address mode. */
	DecodeSource_PrimaryAddressMode(&stuff);

	/* Read source operand. */
	ReadSource(&stuff);

	/* Do the actual instruction. */
	Action_TST(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	Carry_Clear(&stuff);
	/* Update OVERFLOW condition code */
	Overflow_Clear(&stuff);
	/* Update ZERO condition code */
	Zero_SetIfZeroClearOtherwise(&stuff);
	/* Update NEGATIVE condition code */
	Negative_SetIfNegativeClearOtherwise(&stuff);
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_UNLK:
	/* Do the actual instruction. */
	Action_UNLK(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_UNIMPLEMENTED_1:
	/* Do the actual instruction. */
	Action_UNIMPLEMENTED_1(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

case INSTRUCTION_UNIMPLEMENTED_2:
	/* Do the actual instruction. */
	Action_UNIMPLEMENTED_2(&stuff);

	/* Update the condition codes in the following order: */
	/* CARRY, OVERFLOW, ZERO, NEGATIVE, EXTEND */

	/* Update CARRY condition code */
	/* Unaffected */
	/* Update OVERFLOW condition code */
	/* Unaffected */
	/* Update ZERO condition code */
	/* Unaffected */
	/* Update NEGATIVE condition code */
	/* Unaffected */
	/* Update EXTEND condition code */
	/* Unaffected */

	break;

