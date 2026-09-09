#include <stdio.h>
#include <string.h>
#include <vector>
#include <boost/multiprecision/cpp_int.hpp>
#include <openssl/sha.h>
#include "../src/der_x509.h"
#include "../src/pair_identity_generated.h"
#include "../src/sha256.h"
using boost::multiprecision::cpp_int;
static cpp_int QwNeToInt(const unsigned char*p,size_t n){std::vector<unsigned char> x(n);for(size_t i=0;i<n;i+=8)for(int j=0;j<8;j++)x[i+j]=p[i+7-j];cpp_int v=0;for(size_t i=0;i<n;i++)v += cpp_int(x[i]) << (8*i);return v;}
static void IntToQwNe(cpp_int v,unsigned char*out,size_t n){std::vector<unsigned char> le(n);for(size_t i=0;i<n;i++){le[i]=(unsigned char)(v&0xff);v>>=8;}for(size_t i=0;i<n;i+=8)for(int j=0;j<8;j++)out[i+j]=le[i+7-j];}
static void RevQw(const unsigned char*in,unsigned char*out,size_t n){for(size_t q=0;q<n/8;q++)memcpy(out+q*8,in+(n/8-1-q)*8,8);}
static bool H1(const uint8_t*d,size_t n,uint8_t o[20]){SHA1(d,n,o);return true;}
static bool Sign(const uint8_t*t,size_t n,uint8_t sig[256]){uint8_t h[32];sha256::Hash(t,n,h);static const uint8_t di[]={0x30,0x31,0x30,0x0D,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};uint8_t em[256];memset(em,0xff,256);em[0]=0;em[1]=1;size_t sep=256-sizeof(di)-32-1;em[sep]=0;memcpy(em+sep+1,di,sizeof(di));memcpy(em+sep+1+sizeof(di),h,32);uint8_t iq[256];RevQw(em,iq,256);cpp_int c=QwNeToInt(iq,256);const unsigned char*k=pair_identity_generated::kRootPrivateXeCrypt;cpp_int p=QwNeToInt(k+0x110,128),q=QwNeToInt(k+0x190,128),dp=QwNeToInt(k+0x210,128),dq=QwNeToInt(k+0x290,128),u=QwNeToInt(k+0x310,128);cpp_int m1=boost::multiprecision::powm(c,dp,p),m2=boost::multiprecision::powm(c,dq,q),hh=(u*(m1-m2))%p;if(hh<0)hh+=p;cpp_int m=m2+hh*q;uint8_t oq[256];IntToQwNe(m,oq,256);RevQw(oq,sig,256);return true;}
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 3;fseek(f,0,SEEK_END);long z=ftell(f);rewind(f);std::vector<uint8_t>b(z);fread(&b[0],1,z,f);fclose(f);char pem[4096];size_t pn=0;if(!der_x509::BuildDeviceCertificatePem(&b[0],b.size(),H1,Sign,pem,sizeof(pem),&pn))return 4;FILE*o=fopen("/tmp/b9b-device.pem","wb");fwrite(pem,1,pn,o);fclose(o);o=fopen("/tmp/b9b-root.pem","wb");fwrite(pair_identity_generated::kRootCertificatePem,1,strlen(pair_identity_generated::kRootCertificatePem),o);fclose(o);printf("PASS built %zu-byte device PEM\n",pn);return 0;}
