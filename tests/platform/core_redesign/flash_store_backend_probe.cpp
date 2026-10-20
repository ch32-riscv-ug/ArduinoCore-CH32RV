// Opaque cost boundary, compiled without LTO. No Flash/MMIO is accessed.
#include <stdint.h>
#include <stddef.h>
volatile uint32_t backend_accept,backend_offset,backend_observed;
extern "C" uint8_t flash_probe_read(uint32_t offset){backend_offset=offset;return uint8_t(backend_accept);}
extern "C" bool flash_probe_erase(uint32_t offset,unsigned size){backend_offset=offset;backend_observed=size;return backend_accept!=0;}
extern "C" bool flash_probe_program(uint32_t offset,const uint8_t* data,unsigned size){
    uint32_t hash=0;
    for(unsigned i=0;i<size;++i)hash=(hash<<1)^data[i];
    backend_offset=offset;backend_observed=hash;return backend_accept!=0;
}
extern "C" void* memset(void* destination,int value,size_t length){
    auto p=static_cast<volatile uint8_t*>(destination);
    for(size_t i=0;i<length;++i)p[i]=uint8_t(value);
    return destination;
}
extern "C" void* memcpy(void* destination,const void* source,size_t length){
    auto p=static_cast<volatile uint8_t*>(destination);
    auto q=static_cast<const volatile uint8_t*>(source);
    for(size_t i=0;i<length;++i)p[i]=q[i];
    return destination;
}
