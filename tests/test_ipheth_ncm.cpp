#include <stdio.h>
#include <string.h>
#include "ipheth_ncm.h"

static unsigned gCount=0; static unsigned gLens[4];
static bool cb(const BYTE* p,unsigned n,void*){ if(!p||gCount>=4)return false;gLens[gCount++]=n;return true; }
static void le16(BYTE*p,unsigned v){p[0]=(BYTE)v;p[1]=(BYTE)(v>>8);} static void le32(BYTE*p,unsigned long v){p[0]=(BYTE)v;p[1]=(BYTE)(v>>8);p[2]=(BYTE)(v>>16);p[3]=(BYTE)(v>>24);}
int main(){
 BYTE b[512]; memset(b,0,sizeof(b));
 le32(b,0x484D434Eul); le16(b+4,12); le16(b+6,1); le16(b+8,180); le16(b+10,12);
 le32(b+12,0x304D434Eul); le16(b+16,96); le16(b+18,0);
 le16(b+20,108); le16(b+22,42); le16(b+24,150); le16(b+26,30); le16(b+28,0); le16(b+30,0);
 memset(b+108,0x11,42); memset(b+150,0x22,30);
 unsigned frames=0,ann=0; if(!it360_net::DecodeIphethRx(b,sizeof(b),true,cb,0,&frames,&ann)) return 1;
 if(frames!=2||ann!=180||gCount!=2||gLens[0]!=42||gLens[1]!=30)return 2;
 // malformed datagram outside block must fail
 le16(b+20,181); if(it360_net::DecodeIphethRx(b,sizeof(b),true,0,0,0,0))return 3; le16(b+20,108);
 // Linux-compatible 4-byte control frame is ignored successfully
 memset(b,0,sizeof(b));b[0]=0;b[1]=1;if(!it360_net::DecodeIphethRx(b,sizeof(b),true,0,0,&frames,&ann)||frames!=0)return 4;
 // legacy mode: two-byte alignment + ARP Ethernet frame
 memset(b,0,sizeof(b));b[2+12]=0x08;b[2+13]=0x06;gCount=0;
 if(!it360_net::DecodeIphethRx(b,sizeof(b),false,cb,0,&frames,&ann)||frames!=1||gLens[0]!=42||ann!=44)return 5;
 puts("Batch 9C ipheth/NCM tests: PASS");return 0;
}
