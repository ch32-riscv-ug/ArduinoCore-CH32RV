#include <stddef.h>
void *memset(void *destination,int value,size_t count) {
    volatile unsigned char *out=destination;
    for(size_t i=0;i<count;++i)out[i]=(unsigned char)value;
    return destination;
}
void *memcpy(void *destination,const void *source,size_t count) {
    volatile unsigned char *out=destination;
    const volatile unsigned char *in=source;
    for(size_t i=0;i<count;++i)out[i]=in[i];
    return destination;
}
