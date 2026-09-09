#include "der_x509.h"
#include "base64.h"
#include <string.h>
namespace der_x509 {
struct Scratch {
    uint8_t normalizeBs[1024];
    uint8_t normalizeBody[1200];
    uint8_t pubDer[1024];
    uint8_t spki[1200];
    uint8_t validityBody[64];
    uint8_t validity[80];
    uint8_t exb[256];
    uint8_t exseq[280];
    uint8_t exexplicit[300];
    uint8_t tbsBody[1800];
    uint8_t tbs[1900];
    uint8_t sig[256];
    uint8_t bitString[257];
    uint8_t certBody[2400];
    uint8_t cert[2500];
};
static Scratch gScratch;
struct W{uint8_t*p;size_t n,c;W(uint8_t*x,size_t z):p(x),n(0),c(z){}bool add(const void*x,size_t z){if(n+z>c)return false;memcpy(p+n,x,z);n+=z;return true;}bool b(uint8_t x){return add(&x,1);}};
static size_t LenBytes(size_t n,uint8_t*out){if(n<128){out[0]=(uint8_t)n;return 1;}if(n<=255){out[0]=0x81;out[1]=(uint8_t)n;return 2;}out[0]=0x82;out[1]=(uint8_t)(n>>8);out[2]=(uint8_t)n;return 3;}
static bool Tlv(W&w,uint8_t tag,const void*p,size_t n){uint8_t l[3];size_t z=LenBytes(n,l);return w.b(tag)&&w.add(l,z)&&w.add(p,n);}
static const uint8_t kSha256RsaAlg[]={0x30,0x0D,0x06,0x09,0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B,0x05,0x00};
static const uint8_t kRsaAlg[]={0x30,0x0D,0x06,0x09,0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01,0x05,0x00};
static const char* FindN(const char*s,size_t n,const char*q){size_t m=strlen(q);if(m>n)return 0;for(size_t i=0;i+m<=n;i++)if(!memcmp(s+i,q,m))return s+i;return 0;}
static bool PemDer(const uint8_t*p,size_t n,uint8_t*der,size_t cap,size_t*dn,bool*pkcs1){const char*s=(const char*)p;const char*h1="-----BEGIN PUBLIC KEY-----";const char*h2="-----BEGIN RSA PUBLIC KEY-----";const char*e1="-----END PUBLIC KEY-----";const char*e2="-----END RSA PUBLIC KEY-----";const char*h=FindN(s,n,h1);const char*e=0;size_t hl=0;if(h){*pkcs1=false;hl=strlen(h1);e=FindN(h+hl,n-(size_t)(h+hl-s),e1);}else{h=FindN(s,n,h2);if(!h)return false;*pkcs1=true;hl=strlen(h2);e=FindN(h+hl,n-(size_t)(h+hl-s),e2);}if(!e)return false;const char*b=h+hl;size_t bl=(size_t)(e-b);*dn=b64::Decode(b,bl,der,cap);return *dn>0;}
static bool ReadLen(const uint8_t*d,size_t n,size_t*pos,size_t*out){if(*pos>=n)return false;uint8_t b=d[(*pos)++];if(!(b&0x80)){*out=b;return *pos+*out<=n;}unsigned c=b&0x7F;if(!c||c>2||*pos+c>n)return false;size_t v=0;while(c--)v=(v<<8)|d[(*pos)++];*out=v;return *pos+v<=n;}
static bool ReadTlv(const uint8_t*d,size_t n,size_t*pos,uint8_t tag,const uint8_t**v,size_t*z){if(*pos>=n||d[(*pos)++]!=tag)return false;if(!ReadLen(d,n,pos,z))return false;*v=d+*pos;*pos+=*z;return true;}
static bool NormalizeSpki(const uint8_t* der, size_t dn, bool pkcs1,
                          uint8_t* spki, size_t sc, size_t* sn,
                          const uint8_t** rsa, size_t* rn) {
 if (pkcs1) {
  W bw(gScratch.normalizeBs, sizeof(gScratch.normalizeBs));
  if (!bw.b(0) || !bw.add(der, dn)) return false;
  W body(gScratch.normalizeBody, sizeof(gScratch.normalizeBody));
  if (!body.add(kRsaAlg, sizeof(kRsaAlg)) || !Tlv(body, 0x03, gScratch.normalizeBs, bw.n)) return false;
  W outer(spki, sc);
  if (!Tlv(outer, 0x30, gScratch.normalizeBody, body.n)) return false;
  *sn = outer.n;
  *rsa = der;
  *rn = dn;
  return true;
 }
 if (dn > sc) return false;
 memcpy(spki, der, dn);
 *sn = dn;
 size_t p = 0;
 const uint8_t* seq = 0;
 size_t qn = 0;
 if (!ReadTlv(der, dn, &p, 0x30, &seq, &qn)) return false;
 size_t q = 0;
 const uint8_t* alg = 0;
 size_t an = 0;
 if (!ReadTlv(seq, qn, &q, 0x30, &alg, &an)) return false;
 const uint8_t* bits = 0;
 size_t bn = 0;
 if (!ReadTlv(seq, qn, &q, 0x03, &bits, &bn) || bn < 2 || bits[0] != 0) return false;
 *rsa = bits + 1;
 *rn = bn - 1;
 return true;
}
static bool ExtBasic(W&w){uint8_t v[]={0x30,0x00};uint8_t b[32];W x(b,sizeof(b));uint8_t oid[]={0x55,0x1D,0x13};uint8_t yes=0xFF;if(!Tlv(x,0x06,oid,3)||!Tlv(x,0x01,&yes,1)||!Tlv(x,0x04,v,sizeof(v)))return false;return Tlv(w,0x30,b,x.n);}
static bool ExtKeyUsage(W&w){uint8_t inner[]={0x03,0x02,0x05,0xA0};uint8_t b[32];W x(b,sizeof(b));uint8_t oid[]={0x55,0x1D,0x0F};uint8_t yes=0xFF;if(!Tlv(x,0x06,oid,3)||!Tlv(x,0x01,&yes,1)||!Tlv(x,0x04,inner,sizeof(inner)))return false;return Tlv(w,0x30,b,x.n);}
static bool ExtSki(W&w,const uint8_t ski[20]){uint8_t inner[24];W i(inner,sizeof(inner));if(!Tlv(i,0x04,ski,20))return false;uint8_t b[64];W x(b,sizeof(b));uint8_t oid[]={0x55,0x1D,0x0E};if(!Tlv(x,0x06,oid,3)||!Tlv(x,0x04,inner,i.n))return false;return Tlv(w,0x30,b,x.n);}
bool BuildDeviceCertificatePem(const uint8_t*devicePublicPem,size_t pemLen,Sha1Fn sha1,SignSha256Fn sign,char*outPem,size_t outCap,size_t*outLen){
 if(!devicePublicPem||!sha1||!sign||!outPem||!outLen)return false;
 memset(&gScratch,0,sizeof(gScratch));
 size_t pd=0,sn=0,rn=0;bool pk=false;const uint8_t*rsa=0;
 if(!PemDer(devicePublicPem,pemLen,gScratch.pubDer,sizeof(gScratch.pubDer),&pd,&pk)||!NormalizeSpki(gScratch.pubDer,pd,pk,gScratch.spki,sizeof(gScratch.spki),&sn,&rsa,&rn))return false;
 uint8_t ski[20];if(!sha1(rsa,rn,ski))return false;
 W vb(gScratch.validityBody,sizeof(gScratch.validityBody));const char nb[]="260810000000Z",na[]="360808000000Z";if(!Tlv(vb,0x17,nb,13)||!Tlv(vb,0x17,na,13))return false;
 W vv(gScratch.validity,sizeof(gScratch.validity));if(!Tlv(vv,0x30,gScratch.validityBody,vb.n))return false;
 W ex(gScratch.exb,sizeof(gScratch.exb));if(!ExtBasic(ex)||!ExtSki(ex,ski)||!ExtKeyUsage(ex))return false;
 W es(gScratch.exseq,sizeof(gScratch.exseq));if(!Tlv(es,0x30,gScratch.exb,ex.n))return false;
 W ee(gScratch.exexplicit,sizeof(gScratch.exexplicit));if(!Tlv(ee,0xA3,gScratch.exseq,es.n))return false;
 W tb(gScratch.tbsBody,sizeof(gScratch.tbsBody));uint8_t ver[]={0x02,0x01,0x02};uint8_t serial[]={0x02,0x01,0x01};uint8_t emptyName[]={0x30,0x00};
 if(!Tlv(tb,0xA0,ver,sizeof(ver))||!tb.add(serial,sizeof(serial))||!tb.add(kSha256RsaAlg,sizeof(kSha256RsaAlg))||!tb.add(emptyName,sizeof(emptyName))||!tb.add(gScratch.validity,vv.n)||!tb.add(emptyName,sizeof(emptyName))||!tb.add(gScratch.spki,sn)||!tb.add(gScratch.exexplicit,ee.n))return false;
 W tw(gScratch.tbs,sizeof(gScratch.tbs));if(!Tlv(tw,0x30,gScratch.tbsBody,tb.n))return false;
 if(!sign(gScratch.tbs,tw.n,gScratch.sig))return false;
 gScratch.bitString[0]=0;memcpy(gScratch.bitString+1,gScratch.sig,256);
 W cb(gScratch.certBody,sizeof(gScratch.certBody));if(!cb.add(gScratch.tbs,tw.n)||!cb.add(kSha256RsaAlg,sizeof(kSha256RsaAlg))||!Tlv(cb,0x03,gScratch.bitString,sizeof(gScratch.bitString)))return false;
 W cw(gScratch.cert,sizeof(gScratch.cert));if(!Tlv(cw,0x30,gScratch.certBody,cb.n))return false;
 const char head[]="-----BEGIN CERTIFICATE-----\n",foot[]="-----END CERTIFICATE-----\n";size_t hn=sizeof(head)-1,fn=sizeof(foot)-1;if(outCap<hn+fn+16)return false;
 memcpy(outPem,head,hn);size_t enc=b64::Encode(gScratch.cert,cw.n,outPem+hn,outCap-hn-fn,true);if(!enc)return false;
 memcpy(outPem+hn+enc,foot,fn);size_t total=hn+enc+fn;if(total>=outCap)return false;outPem[total]=0;*outLen=total;return true;
}
}
