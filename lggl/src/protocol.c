#include "protocol.h"
#include <string.h>

static void be16(uint8_t *p, uint16_t v) { p[0]=v>>8; p[1]=v; }
static void be32(uint8_t *p, uint32_t v) {
    p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v;
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c=~0u;
    while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1))); }
    return ~c;
}
static size_t tlv(uint8_t *out, size_t at, uint16_t type, const void *v, size_t n) {
    be16(out+at,type); be16(out+at+2,n); memcpy(out+at+4,v,n); return at+4+n;
}
size_t lggl_reply(uint16_t protocol,const uint8_t *in,size_t len,
                  const uint8_t mac[6],uint64_t uptime,uint8_t out[283],uint16_t *tx) {
    static const uint8_t magic[]={0xaa,0xaa,3,0,7,0x70,0,0x11};
    if(protocol==0x0807) {
        if(len<23 || memcmp(in,magic,8)) return 0;
        memset(out,0,283); memcpy(out,in,22);
        out[7]=0x12; out[8]=0x12; out[9]=1; out[11]=0; out[12]=1;
        out[13]=out[14]=out[15]=0; be16(out+16,261);
        size_t at=22; uint8_t zero=0,one=1,four=4,flags[]={1,1,1,1};
        char vendor[16]="Davolink",model[16]="GAPD-7300",hw[16]="VER1";
        uint8_t versions[66]={0,1};
        memcpy(versions+2,"VER1.0",6); memcpy(versions+34,"VER1.0pre",9);
        char serial[16]="SN0000000",board[24]="BC0000000",chip[32]={0};
        memcpy(chip,"Realtek",7); memcpy(chip+16,"Realtek wl chip",15);
        uint8_t age[5]={(uptime/86400)>>8,uptime/86400,(uptime/3600)%24,(uptime/60)%60,uptime%60};
        at=tlv(out,at,0xf001,&zero,1);
        at=tlv(out,at,0x21,vendor,16); at=tlv(out,at,0x22,model,16);
        at=tlv(out,at,0x23,hw,16); at=tlv(out,at,0x24,versions,66);
        at=tlv(out,at,0x25,serial,16); at=tlv(out,at,0x26,flags,4);
        at=tlv(out,at,0x27,&one,1); at=tlv(out,at,0x28,&four,1);
        at=tlv(out,at,0x29,age,5); at=tlv(out,at,0x2a,board,24);
        at=tlv(out,at,0x2c,chip,32); at=tlv(out,at,0x501,&one,1);
        at=tlv(out,at,0x611,&one,1); at=tlv(out,at,0x2b,&one,1);
        be32(out+18,crc32(out+22,at-22)); *tx=0x0807; return at;
    }
    if(protocol==0x1200) {
        if(len<16 || memcmp(in,"RPC\0",4)) return 0;
        uint32_t command=read32(in+8);
        memset(out,0,50); out[0]=0x0f; out[1]=0xfe; out[2]=0x40;
        memcpy(out+4,in,16); be32(out+12,command==201 || command==202 ? 0 : 1);
        be32(out+16,command==201 ? 16 : 0);
        if(command==201) memcpy(out+20,mac,6);
        *tx=0x8100; return 50;
    }
    return 0;
}
