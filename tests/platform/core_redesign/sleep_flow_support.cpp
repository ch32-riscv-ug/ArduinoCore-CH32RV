// Probe support only; all linked bytes are included in the image measurement.
#include <stddef.h>
#include <stdint.h>
extern "C" void* memset(void* destination,int value,size_t length){
    auto out=static_cast<volatile uint8_t*>(destination);
    for(size_t i=0;i<length;++i)out[i]=uint8_t(value);
    return destination;
}
extern "C" void* memcpy(void* destination,const void* source,size_t length){
    auto out=static_cast<volatile uint8_t*>(destination);
    auto in=static_cast<const volatile uint8_t*>(source);
    for(size_t i=0;i<length;++i)out[i]=in[i];
    return destination;
}
