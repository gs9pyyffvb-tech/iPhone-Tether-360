#include "../src/pair_session.h"
#include "../src/pair_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace it360;

static void Fill(PairIdentity& i) {
 memset(&i,0,sizeof(i));
 strcpy(i.host_id,"11111111-2222-4333-8444-555555555555");
 strcpy(i.system_buid,"AAAAAAAA-BBBB-4CCC-8DDD-EEEEEEEEEEEE");
 strcpy(i.root_certificate_b64,"Uk9PVENFUlQ=");
 strcpy(i.root_private_key_b64,"Uk9PVFBSSVY=");
 strcpy(i.host_certificate_b64,"SE9TVENFUlQ=");
 strcpy(i.host_private_key_b64,"SE9TVFBSSVY=");
 strcpy(i.device_certificate_b64,"REVWSUNFQ0VSVA==");
}

int main() {
 uint8_t frame[32768];
 size_t n=BuildGetUniqueDeviceIdFrame(frame,sizeof(frame)); assert(n>4);
 assert(strstr((char*)frame+4,"<key>Key</key><string>UniqueDeviceID</string>")!=0);
 char value[128]; assert(ParseGetValueStringXml("<plist><dict><key>Value</key><string>abc-123</string></dict></plist>",value,sizeof(value))); assert(!strcmp(value,"abc-123"));
 assert(!ParseGetValueStringXml("<plist><dict><key>UniqueDeviceID</key><string>wrong</string></dict></plist>",value,sizeof(value)));

 PairIdentity id; Fill(id);
 n=BuildPairFrame(id,frame,sizeof(frame)); assert(n>4);
 const char* pair=(const char*)frame+4;
 assert(strstr(pair,"<key>Request</key><string>Pair</string>")!=0);
 assert(strstr(pair,"ExtendedPairingErrors")!=0);
 assert(strstr(pair,"RootPrivateKey")==0 && strstr(pair,"HostPrivateKey")==0);
 n=BuildValidatePairFrame(id,frame,sizeof(frame)); assert(n>4);
 const char* validate=(const char*)frame+4;
 assert(strstr(validate,"<key>Request</key><string>ValidatePair</string>")!=0);
 assert(strstr(validate,"PairingOptions")==0);
 assert(ParsePairReplyXml("<plist><dict><key>Request</key><string>Pair</string></dict></plist>")==PAIR_REPLY_SUCCESS);
 assert(ParsePairReplyXml("<plist><dict></dict></plist>")==PAIR_REPLY_INVALID);
 assert(ParsePairReplyXml("<plist><dict><key>Request</key><string>Pair</string><key>Error</key><string>UserDeniedPairing</string></dict></plist>")==PAIR_REPLY_DENIED);
 assert(ParseValidatePairReplyXml("<plist><dict><key>Request</key><string>ValidatePair</string><key>Error</key><string>InvalidHostID</string></dict></plist>")==PAIR_REPLY_INVALID_HOST);

 StoredPairRecord a,b; ClearStoredPairRecord(a); Fill(a.identity);
 strcpy(a.udid,"00008110-001234567890001E"); strcpy(a.wifi_address,"AA:BB:CC:DD:EE:FF"); strcpy(a.escrow_bag_b64,"RVNDUk9X");
 static char text[kPairRecordTextCap]; n=SerializePairRecord(a,text,sizeof(text)); assert(n>0);
 assert(strstr(text,"RootPrivateKey=Uk9PVFBSSVY=")!=0);
 assert(DeserializePairRecord(text,b));
 assert(!strcmp(a.udid,b.udid)); assert(!strcmp(a.identity.host_private_key_b64,b.identity.host_private_key_b64)); assert(!strcmp(a.escrow_bag_b64,b.escrow_bag_b64));
 assert(SavePairRecord(a)); ClearStoredPairRecord(b); assert(LoadPairRecord(a.udid,b)); assert(!strcmp(b.identity.host_id,a.identity.host_id)); assert(DeletePairRecord(a.udid));
 puts("Batch 9B protocol + persistence tests: PASS");
 return 0;
}
