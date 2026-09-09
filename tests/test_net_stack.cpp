#include <stdio.h>
#include <string.h>
#include "net_stack.h"

static BYTE sent[32][1600]; static unsigned slen[32]; static unsigned scount=0; static unsigned events[32]; static unsigned ecount=0;
static bool sendcb(const BYTE*p,unsigned n,void*){if(scount>=32||n>1600)return false;memcpy(sent[scount],p,n);slen[scount++]=n;return true;}
static void eventcb(it360_net::NetEvent e,const it360_net::NetConfig*,DWORD,void*){if(ecount<32)events[ecount++]=(unsigned)e;}
static unsigned r16(const BYTE*p){return ((unsigned)p[0]<<8)|p[1];} static DWORD r32(const BYTE*p){return ((DWORD)p[0]<<24)|((DWORD)p[1]<<16)|((DWORD)p[2]<<8)|p[3];}
static void w16(BYTE*p,unsigned v){p[0]=(BYTE)(v>>8);p[1]=(BYTE)v;} static void w32(BYTE*p,DWORD v){p[0]=(BYTE)(v>>24);p[1]=(BYTE)(v>>16);p[2]=(BYTE)(v>>8);p[3]=(BYTE)v;}
static unsigned make_udp(BYTE*out,const BYTE dstmac[6],const BYTE srcmac[6],DWORD sip,DWORD dip,unsigned sp,unsigned dp,const BYTE*pl,unsigned pn){
 memcpy(out,dstmac,6);memcpy(out+6,srcmac,6);w16(out+12,0x0800);BYTE*ip=out+14;memset(ip,0,20);ip[0]=0x45;w16(ip+2,20+8+pn);ip[8]=64;ip[9]=17;w32(ip+12,sip);w32(ip+16,dip);BYTE*u=ip+20;w16(u,sp);w16(u+2,dp);w16(u+4,8+pn);w16(u+6,0);memcpy(u+8,pl,pn);return 14+20+8+pn;}
static unsigned make_dhcp(BYTE*out,const BYTE clientmac[6],DWORD xid,BYTE mt,DWORD yi,DWORD server,DWORD mask,DWORD gw,DWORD dns){
 BYTE d[400];memset(d,0,sizeof(d));d[0]=2;d[1]=1;d[2]=6;w32(d+4,xid);w32(d+16,yi);w32(d+20,server);memcpy(d+28,clientmac,6);d[236]=99;d[237]=130;d[238]=83;d[239]=99;unsigned o=240;
 d[o++]=53;d[o++]=1;d[o++]=mt;d[o++]=54;d[o++]=4;w32(d+o,server);o+=4;d[o++]=1;d[o++]=4;w32(d+o,mask);o+=4;d[o++]=3;d[o++]=4;w32(d+o,gw);o+=4;d[o++]=6;d[o++]=4;w32(d+o,dns);o+=4;d[o++]=51;d[o++]=4;w32(d+o,3600);o+=4;d[o++]=255;
 BYTE sm[6]={2,0,0,0,0,1};return make_udp(out,clientmac,sm,server,0xFFFFFFFFu,67,68,d,o);
}
static unsigned make_arp_reply(BYTE*out,const BYTE client[6],const BYTE router[6],DWORD clientip,DWORD routerip){memcpy(out,client,6);memcpy(out+6,router,6);w16(out+12,0x0806);BYTE*a=out+14;w16(a,1);w16(a+2,0x0800);a[4]=6;a[5]=4;w16(a+6,2);memcpy(a+8,router,6);w32(a+14,routerip);memcpy(a+18,client,6);w32(a+24,clientip);return 42;}
static unsigned make_tcp(BYTE*out,const BYTE client[6],const BYTE router[6],DWORD sip,DWORD dip,unsigned sp,unsigned dp,DWORD seq,DWORD ack,BYTE flags,const BYTE*pl,unsigned pn){memcpy(out,client,6);memcpy(out+6,router,6);w16(out+12,0x0800);BYTE*ip=out+14;memset(ip,0,20);ip[0]=0x45;w16(ip+2,20+20+pn);ip[8]=64;ip[9]=6;w32(ip+12,sip);w32(ip+16,dip);BYTE*t=ip+20;memset(t,0,20);w16(t,sp);w16(t+2,dp);w32(t+4,seq);w32(t+8,ack);t[12]=0x50;t[13]=flags;w16(t+14,32768);if(pn)memcpy(t+20,pl,pn);return 14+20+20+pn;}
static int find_last_ipproto(unsigned proto){for(int i=(int)scount-1;i>=0;--i)if(slen[i]>=34&&r16(sent[i]+12)==0x0800&&sent[i][23]==proto)return i;return -1;}
int main(){
 it360_net::StandaloneNetStack n;BYTE mac[6]={0x02,0x39,0x43,0x11,0x22,0x33};if(!n.Start(mac,0x12345678,sendcb,eventcb,0))return 1;
 if(scount!=1||sent[0][23]!=17||r16(sent[0]+34)!=68||r16(sent[0]+36)!=67)return 2;
 DWORD xid=r32(sent[0]+46);
 // No offer: retry Discover after four one-second ticks.
 for(unsigned tick=0;tick<4;++tick)n.TickOneSecond();
 if(scount!=2||sent[1][23]!=17||r16(sent[1]+34)!=68||r16(sent[1]+36)!=67||r32(sent[1]+46)!=xid)return 16;
 const DWORD cip=0xAC14020Fu,server=0xAC140201u,mask=0xFFFFFFF0u,dns=0xAC140201u;BYTE f[1600];unsigned fl=make_dhcp(f,mac,xid,2,cip,server,mask,server,dns);if(!n.OnEthernetFrame(f,fl))return 3;
 if(scount<2||r16(sent[scount-1]+34)!=68)return 4;
 fl=make_dhcp(f,mac,xid,5,cip,server,mask,server,dns);
 if(!n.OnEthernetFrame(f,fl)||!n.IsConfigured())return 5;
 if(r16(sent[scount-1]+12)!=0x0806)return 6;
 BYTE rmac[6]={0x02,0xaa,0xbb,0xcc,0xdd,0xee};
 fl=make_arp_reply(f,mac,rmac,cip,server);
 if(!n.OnEthernetFrame(f,fl))return 7;
 if(find_last_ipproto(1)<0)return 17; // diagnostic ICMP echo emitted after gateway ARP
 int dnsq=find_last_ipproto(17);if(dnsq<0)return 8;BYTE*uq=sent[dnsq]+34;unsigned localDns=r16(uq);BYTE*dq=uq+8;unsigned did=r16(dq);
 BYTE dr[128];memset(dr,0,sizeof(dr));w16(dr,did);w16(dr+2,0x8180);w16(dr+4,1);w16(dr+6,1);unsigned o=12;const BYTE qname[]={7,'e','x','a','m','p','l','e',3,'c','o','m',0};memcpy(dr+o,qname,sizeof(qname));o+=sizeof(qname);w16(dr+o,1);o+=2;w16(dr+o,1);o+=2;dr[o++]=0xC0;dr[o++]=0x0C;w16(dr+o,1);o+=2;w16(dr+o,1);o+=2;w32(dr+o,60);o+=4;w16(dr+o,4);o+=2;DWORD web=0x5DB8D822u;w32(dr+o,web);o+=4;
 fl=make_udp(f,mac,rmac,dns,cip,53,localDns,dr,o);if(!n.OnEthernetFrame(f,fl))return 9;int syn=find_last_ipproto(6);if(syn<0)return 10;BYTE*st=sent[syn]+34;unsigned lp=r16(st);DWORD lnext=r32(st+4)+1;
 fl=make_tcp(f,mac,rmac,web,cip,80,lp,0xABCDEF00u,lnext,0x12,0,0);if(!n.OnEthernetFrame(f,fl))return 11;
 // Last two sends are ACK then HTTP PSH. Capture client's next sequence from HTTP segment.
 int http=find_last_ipproto(6);if(http<0)return 12;BYTE*ht=sent[http]+34;if((ht[13]&0x18)!=0x18)return 13;DWORD clientNext=r32(ht+4)+(slen[http]-54);
 const char resp[]="HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK";fl=make_tcp(f,mac,rmac,web,cip,80,lp,0xABCDEF01u,clientNext,0x18,(const BYTE*)resp,sizeof(resp)-1);if(!n.OnEthernetFrame(f,fl)||!n.InternetValidated())return 14;
 bool sawHttp=false;for(unsigned i=0;i<ecount;++i)if(events[i]==it360_net::NetEventHttpResponse)sawHttp=true;if(!sawHttp)return 15;
 // T1 renewal is lease/2. The ACK above advertised 3600 seconds.
 unsigned beforeRenew=scount;for(unsigned tick=0;tick<1800;++tick)n.TickOneSecond();
 if(scount<=beforeRenew)return 18;
 int renew=find_last_ipproto(17);
 if(renew<0||r16(sent[renew]+34)!=68||r16(sent[renew]+36)!=67)return 19;
 puts("Batch 9C standalone network end-to-end + retry/renew tests: PASS");return 0;
}
