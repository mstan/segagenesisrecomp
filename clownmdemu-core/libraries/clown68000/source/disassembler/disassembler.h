#ifndef INCLUDE_GUARD_B0B82496_E262_49CB_927E_C4CA05531920
#define INCLUDE_GUARD_B0B82496_E262_49CB_927E_C4CA05531920

#ifdef __cplusplus
extern "C" {
#endif

typedef long (*Clown68000_Disassemble_ReadCallback)(void *user_data);
typedef void (*Clown68000_Disassemble_PrintCallback)(void *user_data, const char *string);

void Clown68000_Disassemble(unsigned long address, unsigned int max_instructions, Clown68000_Disassemble_ReadCallback read_callback, Clown68000_Disassemble_PrintCallback print_callback, const void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_GUARD_B0B82496_E262_49CB_927E_C4CA05531920 */
