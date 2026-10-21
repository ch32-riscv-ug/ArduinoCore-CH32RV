// Observation harness for the frozen upstream OLED/INA implementation.
// Only I2C is replaced. No MCU MMIO implementation is linked or executed.
#include "oled.h"
#include "ina219.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Leg {unsigned address,tx_count,rx_count;uint8_t tx[300],rx[2],ack[2];};
struct Transaction {unsigned count;struct Leg legs[2];};
static struct Transaction transactions[4];
static unsigned count=0,active=0,read_index=0;
static uint8_t read_values[2];
static struct Leg* leg(void){assert(active&&count<4);return &transactions[count].legs[transactions[count].count-1];}
void I2C_start(uint8_t address){assert(!active&&count<4);active=1;transactions[count].count=1;transactions[count].legs[0].address=address;}
void I2C_restart(uint8_t address){assert(active&&transactions[count].count==1);transactions[count].count=2;transactions[count].legs[1].address=address;}
void I2C_stop(void){assert(active);active=0;++count;}
void I2C_write(uint8_t value){struct Leg* l=leg();assert(!(l->address&1)&&l->tx_count<300);l->tx[l->tx_count++]=value;}
uint8_t I2C_read(uint8_t ack){struct Leg* l=leg();assert((l->address&1)&&l->rx_count<2&&read_index<2&&ack<2);uint8_t value=read_values[read_index++];l->rx[l->rx_count]=value;l->ack[l->rx_count++]=ack;return value;}

int main(int argc,char** argv){
    assert(argc>=2&&argc<=4);unsigned a=argc>2?(unsigned)strtoul(argv[2],NULL,10):0,b=argc>3?(unsigned)strtoul(argv[3],NULL,10):0;
    int result=0;read_values[0]=(uint8_t)(b>>8);read_values[1]=(uint8_t)b;
    if(!strcmp(argv[1],"init"))OLED_init();
    else if(!strcmp(argv[1],"cursor")){assert(a<128&&b<4);OLED_setCursor((uint8_t)a,(uint8_t)b);}
    else if(!strcmp(argv[1],"clear-line")){assert(a<3);OLED_clearLine((uint8_t)a);}
    else if(!strcmp(argv[1],"clear"))OLED_clearScreen();
    else if(!strcmp(argv[1],"value")){assert(a<=65535);OLED_printVal((uint16_t)a);}
    else if(!strcmp(argv[1],"pps"))OLED_printStr(OLED_PPS);
    else if(!strcmp(argv[1],"ina-init"))INA_init();
    else if(!strcmp(argv[1],"ina-read")){assert(a<256&&b<=65535);result=INA_read((uint8_t)a);}
    else if(!strcmp(argv[1],"ina-v")){assert(b<=65535);result=INA_readVoltage();}
    else {assert(!strcmp(argv[1],"ina-i")&&b<=65535);result=INA_readCurrent();}
    assert(!active);
    printf("{\"result\":%d,\"transactions\":[",result);
    for(unsigned i=0;i<count;++i){
        printf("%s[",i?",":"");
        for(unsigned j=0;j<transactions[i].count;++j){
            struct Leg* l=&transactions[i].legs[j];printf("%s{\"address\":%u,\"tx\":[",j?",":"",l->address);
            for(unsigned k=0;k<l->tx_count;++k)printf("%s%u",k?",":"",l->tx[k]);
            printf("],\"rx\":[");for(unsigned k=0;k<l->rx_count;++k)printf("%s%u",k?",":"",l->rx[k]);
            printf("],\"ack\":[");for(unsigned k=0;k<l->rx_count;++k)printf("%s%u",k?",":"",l->ack[k]);printf("]}");
        }
        printf("]");
    }
    printf("]}\n");return 0;
}
