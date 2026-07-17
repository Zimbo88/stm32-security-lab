#include <stdint.h>
#include "platform.h"
#include "uart.h"
#define REG32(a) (*(volatile uint32_t *)(a))
#define RCC_CSR REG32(0x40023874UL)
#define SCB_CFSR REG32(0xE000ED28UL)
#define SCB_HFSR REG32(0xE000ED2CUL)
#define SCB_DFSR REG32(0xE000ED30UL)
#define SCB_MMFAR REG32(0xE000ED34UL)
#define SCB_BFAR REG32(0xE000ED38UL)
#define SCB_AFSR REG32(0xE000ED3CUL)
#define MAGIC 0x46544C52UL
typedef struct { uint32_t magic,version,sequence,reset_cause; uint32_t r0,r1,r2,r3,r12,lr,pc,xpsr,msp,psp,control,primask,basepri,faultmask; uint32_t cfsr,hfsr,dfsr,mmfar,bfar,afsr,crc32; } fault_record_t;
static fault_record_t record __attribute__((section(".noinit"),used));
static uint32_t crc32(const uint8_t *p, uint32_t n) { uint32_t c=0xFFFFFFFFUL; for(uint32_t i=0;i<n;i++){c^=p[i];for(uint32_t b=0;b<8;b++)c=(c>>1)^((c&1U)?0xEDB88320UL:0U);} return c^0xFFFFFFFFUL; }
static uint32_t record_crc(void){return crc32((const uint8_t*)&record,(uint32_t)((uintptr_t)&record.crc32-(uintptr_t)&record));}
int fault_valid(void){return record.magic==MAGIC && record.version==1U && record.crc32==record_crc();}
void fault_clear(void){record.magic=0U;}
void fault_show(void);
__attribute__((naked)) void HardFault_Handler(void){__asm volatile("tst lr,#4\n ite eq\n mrseq r0,msp\n mrsne r0,psp\n b fault_capture");}
void MemManage_Handler(void){HardFault_Handler();} void BusFault_Handler(void){HardFault_Handler();} void UsageFault_Handler(void){HardFault_Handler();}
void fault_capture(const uint32_t *s){ uint32_t seq=record.sequence+1U; record.magic=MAGIC;record.version=1U;record.sequence=seq;record.reset_cause=RCC_CSR; record.r0=s[0];record.r1=s[1];record.r2=s[2];record.r3=s[3];record.r12=s[4];record.lr=s[5];record.pc=s[6];record.xpsr=s[7]; __asm volatile("mrs %0,msp":"=r"(record.msp));__asm volatile("mrs %0,psp":"=r"(record.psp));__asm volatile("mrs %0,control":"=r"(record.control));__asm volatile("mrs %0,primask":"=r"(record.primask));__asm volatile("mrs %0,basepri":"=r"(record.basepri));__asm volatile("mrs %0,faultmask":"=r"(record.faultmask)); record.cfsr=SCB_CFSR;record.hfsr=SCB_HFSR;record.dfsr=SCB_DFSR;record.mmfar=SCB_MMFAR;record.bfar=SCB_BFAR;record.afsr=SCB_AFSR;record.crc32=record_crc(); for(;;){} }
void fault_show(void){ if(!fault_valid()){uart_puts("fault: none\n");return;} uart_puts("fault: valid seq=");uart_put_u32(record.sequence);uart_puts(" pc=");uart_put_hex32(record.pc);uart_puts(" cfsr=");uart_put_hex32(record.cfsr);uart_puts(" hfsr=");uart_put_hex32(record.hfsr);uart_puts("\n"); }
