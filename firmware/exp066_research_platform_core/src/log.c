#include "log.h"
#include "platform.h"
#include "uart.h"
#define LOG_CAPACITY 32U
#define LOG_PAYLOAD 12U
typedef struct { uint32_t sequence,timestamp; uint16_t source,event; uint8_t severity,length; uint8_t payload[LOG_PAYLOAD]; } log_record_t;
static log_record_t records[LOG_CAPACITY];
static volatile uint32_t next_sequence, count, dropped;
static void copy_bytes(uint8_t *d,const uint8_t *s,uint32_t n){for(uint32_t i=0U;i<n;i++)d[i]=s[i];}
void log_init(void){next_sequence=1U;count=0U;dropped=0U;}
void log_write(log_severity_t severity,uint16_t source,uint16_t event,const void *payload,uint8_t length){
 uint32_t primask; __asm volatile("mrs %0, primask\ncpsid i":"=r"(primask)::"memory");
 if(length>LOG_PAYLOAD){length=LOG_PAYLOAD;dropped++;}
 uint32_t slot=(next_sequence-1U)%LOG_CAPACITY; log_record_t *r=&records[slot];
 r->sequence=next_sequence++; r->timestamp=platform_millis();r->source=source;r->event=event;r->severity=(uint8_t)severity;r->length=length;
 if(payload!=0 && length!=0U)copy_bytes(r->payload,(const uint8_t*)payload,length);
 if(count<LOG_CAPACITY)count++;else dropped++;
 if((primask&1U)==0U)__asm volatile("cpsie i":::"memory");
}
uint32_t log_count(void){return count;}
void log_clear(void){uint32_t p;__asm volatile("mrs %0, primask\ncpsid i":"=r"(p)::"memory");count=0U;dropped=0U;if((p&1U)==0U)__asm volatile("cpsie i":::"memory");}
void log_show(void){uint32_t c=count, first=next_sequence-c;uart_puts("records=");uart_put_u32(c);uart_puts(" dropped=");uart_put_u32(dropped);uart_puts("\n");for(uint32_t s=first;s<next_sequence;s++){const log_record_t*r=&records[(s-1U)%LOG_CAPACITY];uart_puts("seq=");uart_put_u32(r->sequence);uart_puts(" ms=");uart_put_u32(r->timestamp);uart_puts(" sev=");uart_put_u32(r->severity);uart_puts(" src=");uart_put_u32(r->source);uart_puts(" event=");uart_put_u32(r->event);uart_puts("\n");}}
