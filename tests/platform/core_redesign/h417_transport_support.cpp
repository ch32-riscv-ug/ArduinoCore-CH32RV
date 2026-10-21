#include <stddef.h>
#include <stdint.h>
// Opaque link-cost boundary only. No physical exclusion or shared controller.
volatile uint32_t transport_gate_accept,transport_gate_slot,transport_gate_releases;
extern "C" bool transport_gate_try(unsigned slot){transport_gate_slot=slot;return transport_gate_accept!=0;}
extern "C" void transport_gate_leave(unsigned slot){transport_gate_slot=slot;++transport_gate_releases;}
extern "C" void* memset(void* destination,int value,size_t length){auto p=static_cast<volatile uint8_t*>(destination);for(size_t i=0;i<length;++i)p[i]=uint8_t(value);return destination;}
extern "C" void* memcpy(void* destination,const void* source,size_t length){auto p=static_cast<volatile uint8_t*>(destination);auto q=static_cast<const volatile uint8_t*>(source);for(size_t i=0;i<length;++i)p[i]=q[i];return destination;}
