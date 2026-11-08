#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "boot/uefi/memory_map.h"
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i=0; i<4; i++) p[i]=(uint8_t)(v >> (8u*i));
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i=0; i<8; i++) p[i]=(uint8_t)(v >> (8u*i));
}
static void desc(uint8_t *p, uint32_t type, uint64_t base, uint64_t pages) {
    for (unsigned i=0;i<48;i++) p[i]=0;
    put32(p,type); put64(p+8,base); put64(p+24,pages);
}
int main(void) {
    uint8_t raw[96];
    struct nova_uefi_region out[2];
    size_t count=99;
    desc(raw,7,0x100000,12);
    desc(raw+48,5,0x200000,2);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==0);
    assert(count==2 && out[0].base==0x100000 && out[0].length==49152);
    assert(out[0].type==1 && out[1].type==2 && out[1].reserved==0);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,1,&count)==-1 && count==0);
    assert(nova_uefi_normalize_map(raw,sizeof raw,39,out,2,&count)==-1);
    assert(nova_uefi_normalize_map(raw,95,48,out,2,&count)==-1);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,0)==-1);
    desc(raw,7,0x100001,1);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==-1);
    desc(raw,7,0x100000,0);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==-1);
    desc(raw,7,0x100000,UINT64_MAX);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==-1);
    desc(raw,7,UINT64_MAX-4095,2);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==-1);
    desc(raw,7,0x100000,1);
    desc(raw+48,5,0x100000,1);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==-1);
    desc(raw+48,5,0x200000,2);
    assert(nova_uefi_normalize_map(raw,sizeof raw,48,out,2,&count)==0);
    return 0;
}
