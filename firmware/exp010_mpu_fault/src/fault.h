#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

void HardFault_Handler(void);
void MemManage_Handler(void);
void fault_handler_c(uint32_t *stack_frame, uint32_t exception_kind);

#endif
