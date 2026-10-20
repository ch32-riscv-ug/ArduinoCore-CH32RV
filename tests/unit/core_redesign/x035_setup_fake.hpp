#pragma once
// Included after the frozen I2C Fake in the generated host translation unit.
struct BoardBus:Fake {
    struct Access {unsigned kind;uintptr_t address;uint32_t value;};
    static inline uint32_t words[6]={3,0,0,0,0,0};
    static inline unsigned ignored=0;
    static inline std::vector<Access> accesses;
    static unsigned index(uintptr_t address) {
        using namespace x035_i2c_setup_experiment;
        const uintptr_t addresses[]={RccControl,RccConfig,Apb2,Apb1,Remap,PinConfig};
        for(unsigned i=0;i<6;++i)if(addresses[i]==address)return i;
        assert(false);return 0;
    }
    static void accessible(unsigned i) {
        if(i==4)assert(words[2]&1);
        if(i==5)assert(words[2]&4);
    }
    static uint32_t read32(uintptr_t address) {
        const auto i=index(address);accessible(i);accesses.push_back({0,address,words[i]});return words[i];
    }
    static void write32(uintptr_t address,uint32_t value) {
        const auto i=index(address);accessible(i);accesses.push_back({1,address,value});
        if(!(ignored&(1u<<i)))words[i]=value;
    }
    static uint16_t read16(uintptr_t address) {
        assert(words[3]&x035_i2c_setup_experiment::Gates1);return Fake::read16(address);
    }
    static void write16(uintptr_t address,uint16_t value) {
        assert(words[3]&x035_i2c_setup_experiment::Gates1);Fake::write16(address,value);
    }
};
