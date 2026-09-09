#include "../src/pair_session.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
using namespace it360;

struct O { int kind; };
static bool rnd(uint8_t* p,size_t n){ for(size_t i=0;i<n;i++)p[i]=(uint8_t)i; return true; }
static bool key(void**o){static O objs[2];static int i=0;if(i>=2)return false;objs[i].kind=i;*o=&objs[i++];return true;}
static bool cert1(void*,void**o){static O x;*o=&x;return true;}
static bool cert2(void*,void*,void*,void**o){static O x;*o=&x;return true;}
static bool cert3(const uint8_t*p,size_t n,void*,void*,void**o){static O x;if(!p||!n)return false;*o=&x;return true;}
static bool ek(void*,uint8_t*out,size_t cap,size_t*w){const char*s="-----BEGIN PRIVATE KEY-----\nTEST\n-----END PRIVATE KEY-----\n";size_t n=strlen(s);if(cap<n)return false;memcpy(out,s,n);*w=n;return true;}
static bool ec(void*,uint8_t*out,size_t cap,size_t*w){const char*s="-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";size_t n=strlen(s);if(cap<n)return false;memcpy(out,s,n);*w=n;return true;}
static void fr(void*){}

int main(){
 PairCryptoBackend c={rnd,key,cert1,cert2,cert3,ek,ec,fr,fr};
 uint8_t f[65536]; PairSession s; size_t n=s.Begin(f,sizeof(f)); assert(n>4); assert(s.State()==PAIR_SESSION_GET_VALUE_SENT);
 assert(strstr((char*)f+4,"<key>Key</key><string>DevicePublicKey</string>")!=0);
 const char*x="<plist><dict><key>Value</key><data>LS0tLS1CRUdJTiBQVUJMSUMgS0VZLS0tLS0KVEVTVAotLS0tLUVORCBQVUJMSUMgS0VZLS0tLS0K</data></dict></plist>";
 n=s.OnDevicePublicKeyReply(x,c,f,sizeof(f)); assert(n>4); assert(s.State()==PAIR_SESSION_PAIR_SENT);
 assert(strstr((char*)f+4,"<key>Request</key><string>Pair</string>")!=0);
 assert(strstr((char*)f+4,"<key>DeviceCertificate</key>")!=0);
 assert(strstr((char*)f+4,"<key>PairingOptions</key>")!=0);
 assert(strstr((char*)f+4,"ExtendedPairingErrors")!=0);
 assert(strstr((char*)f+4,"HostPrivateKey")==0); assert(strstr((char*)f+4,"RootPrivateKey")==0);
 assert(s.OnPairReply("<plist><dict><key>Request</key><string>Pair</string><key>Error</key><string>PairingDialogResponsePending</string></dict></plist>")==PAIR_REPLY_PENDING_TRUST);
 assert(s.State()==PAIR_SESSION_PENDING_TRUST);
 assert(s.OnPairReply("<plist><dict><key>Request</key><string>Pair</string></dict></plist>")==PAIR_REPLY_SUCCESS);
 assert(s.State()==PAIR_SESSION_PAIRED);
 puts("Batch 9B pair-session tests: PASS"); return 0;
}
