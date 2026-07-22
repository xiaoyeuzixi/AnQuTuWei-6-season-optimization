#pragma once
/*
 * Security.h — 综合安全防护模块 (DMA_AnQu 专用, 移植自 ARS_AnQu_WB)
 * 移除了内核驱动保护 (DriverProtection), 仅保留卡密验证 + 反作弊 + 加密通信
 * DMA项目使用FPGA硬件读取, 不需要内核驱动
 */

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif

#include <Windows.h>
#include <winhttp.h>
#include <intrin.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include <sstream>
#include <random>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace Security {

namespace detail {

class SHA256 {
public:
    SHA256() { reset(); }
    void reset() {
        m_dataLen = 0; m_bitLen = 0;
        m_state[0]=0x6a09e667; m_state[1]=0xbb67ae85; m_state[2]=0x3c6ef372; m_state[3]=0xa54ff53a;
        m_state[4]=0x510e527f; m_state[5]=0x9b05688c; m_state[6]=0x1f83d9ab; m_state[7]=0x5be0cd19;
        memset(m_data,0,64);
    }
    void update(const uint8_t* data, size_t len) {
        for (size_t i=0;i<len;i++){ m_data[m_dataLen++]=data[i]; if(m_dataLen==64){transform();m_bitLen+=512;m_dataLen=0;} }
    }
    std::string final() {
        uint32_t i=m_dataLen;
        if(m_dataLen<56){ m_data[i++]=0x80; while(i<56)m_data[i++]=0; }
        else { m_data[i++]=0x80; while(i<64)m_data[i++]=0; transform(); memset(m_data,0,56); }
        m_bitLen+=(uint64_t)m_dataLen*8;
        m_data[63]=(uint8_t)(m_bitLen); m_data[62]=(uint8_t)(m_bitLen>>8); m_data[61]=(uint8_t)(m_bitLen>>16);
        m_data[60]=(uint8_t)(m_bitLen>>24); m_data[59]=(uint8_t)(m_bitLen>>32); m_data[58]=(uint8_t)(m_bitLen>>40);
        m_data[57]=(uint8_t)(m_bitLen>>48); m_data[56]=(uint8_t)(m_bitLen>>56);
        transform();
        std::string result;
        for(int j=0;j<8;j++) for(i=0;i<4;i++){ char hex[4]; sprintf_s(hex,"%02x",((m_state[j]>>(24-i*8))&0xFF)); result+=hex; }
        return result;
    }
private:
    uint8_t m_data[64]; uint32_t m_dataLen,m_state[8]; uint64_t m_bitLen;
    static uint32_t ROTR(uint32_t x,uint32_t n){return(x>>n)|(x<<(32-n));}
    static uint32_t CH(uint32_t x,uint32_t y,uint32_t z){return(x&y)^(~x&z);}
    static uint32_t MAJ(uint32_t x,uint32_t y,uint32_t z){return(x&y)^(x&z)^(y&z);}
    static uint32_t EP0(uint32_t x){return ROTR(x,2)^ROTR(x,13)^ROTR(x,22);}
    static uint32_t EP1(uint32_t x){return ROTR(x,6)^ROTR(x,11)^ROTR(x,25);}
    static uint32_t SIG0(uint32_t x){return ROTR(x,7)^ROTR(x,18)^(x>>3);}
    static uint32_t SIG1(uint32_t x){return ROTR(x,17)^ROTR(x,19)^(x>>10);}
    static const uint32_t K[64];
    void transform(){
        uint32_t m[64];
        for(uint32_t i=0,j=0;i<16;i++,j+=4) m[i]=((uint32_t)m_data[j]<<24)|((uint32_t)m_data[j+1]<<16)|((uint32_t)m_data[j+2]<<8)|(uint32_t)m_data[j+3];
        for(uint32_t i=16;i<64;i++) m[i]=SIG1(m[i-2])+m[i-7]+SIG0(m[i-15])+m[i-16];
        uint32_t a=m_state[0],b=m_state[1],c=m_state[2],d=m_state[3],e=m_state[4],f=m_state[5],g=m_state[6],h=m_state[7];
        for(uint32_t i=0;i<64;i++){ uint32_t t1=h+EP1(e)+CH(e,f,g)+K[i]+m[i]; uint32_t t2=EP0(a)+MAJ(a,b,c); h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        m_state[0]+=a;m_state[1]+=b;m_state[2]+=c;m_state[3]+=d;m_state[4]+=e;m_state[5]+=f;m_state[6]+=g;m_state[7]+=h;
    }
};
const uint32_t SHA256::K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

inline std::string sha256(const std::string& input){ SHA256 ctx; ctx.update(reinterpret_cast<const uint8_t*>(input.c_str()),input.size()); return ctx.final(); }

inline std::string base64Encode(const std::string& input){
    static const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result; int i=0,j=0; unsigned char char3[3],char4[4];
    for(size_t pos=0;pos<input.size();pos++){ char3[i++]=input[pos]; if(i==3){ char4[0]=(char3[0]&0xFC)>>2; char4[1]=((char3[0]&0x03)<<4)+((char3[1]&0xF0)>>4); char4[2]=((char3[1]&0x0F)<<2)+((char3[2]&0xC0)>>6); char4[3]=char3[2]&0x3F; for(i=0;i<4;i++)result+=table[char4[i]]; i=0; } }
    if(i>0){ for(j=i;j<3;j++)char3[j]='\0'; char4[0]=(char3[0]&0xFC)>>2; char4[1]=((char3[0]&0x03)<<4)+((char3[1]&0xF0)>>4); char4[2]=((char3[1]&0x0F)<<2)+((char3[2]&0xC0)>>6); for(j=0;j<i+1;j++)result+=table[char4[j]]; while(i++<3)result+='='; }
    return result;
}
inline std::string base64Decode(const std::string& input){
    static const int8_t table[256]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    std::string result; int i=0; unsigned char char4[4];
    for(size_t pos=0;pos<input.size();pos++){ if(input[pos]=='=')break; int8_t val=table[(unsigned char)input[pos]]; if(val<0)continue; char4[i++]=(unsigned char)val; if(i==4){ result+=(char)((char4[0]<<2)|(char4[1]>>4)); result+=(char)((char4[1]<<4)|(char4[2]>>2)); result+=(char)((char4[2]<<6)|char4[3]); i=0; } }
    if(i>0){ for(int j=i;j<4;j++)char4[j]=0; result+=(char)((char4[0]<<2)|(char4[1]>>4)); if(i>=3)result+=(char)((char4[1]<<4)|(char4[2]>>2)); }
    return result;
}
inline std::string getCPUId(){ int cpuInfo[4]={0}; __cpuid(cpuInfo,0); char buf[33]={0}; sprintf_s(buf,"%08X%08X%08X%08X",cpuInfo[0],cpuInfo[1],cpuInfo[2],cpuInfo[3]); return std::string(buf); }
inline std::string getMachineGuid(){ HKEY hKey; char guid[128]={0}; DWORD size=sizeof(guid); if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,"SOFTWARE\\Microsoft\\Cryptography",0,KEY_READ|KEY_WOW64_64KEY,&hKey)==ERROR_SUCCESS){ RegQueryValueExA(hKey,"MachineGuid",nullptr,nullptr,reinterpret_cast<LPBYTE>(guid),&size); RegCloseKey(hKey); } return std::string(guid); }
inline std::string getComputerName(){ char name[MAX_COMPUTERNAME_LENGTH+1]={0}; DWORD size=sizeof(name); GetComputerNameA(name,&size); return std::string(name); }
inline std::string getDeviceFingerprint(){ return sha256(getCPUId()+"|"+getMachineGuid()+"|"+getComputerName()); }
inline std::string xorEncryptString(const std::string& data,const std::string& key){ std::string r=data; for(size_t i=0;i<r.size();i++)r[i]^=key[i%key.size()]; return r; }
inline std::string encryptPacket(const std::string& data,const std::string& key){ std::string xored=xorEncryptString(data,key); return base64Encode(xored); }
inline std::string genNonce(){ std::random_device rd; std::stringstream ss; ss<<std::hex<<std::setfill('0'); ss<<std::setw(8)<<rd()<<std::setw(8)<<rd()<<std::setw(8)<<rd()<<std::setw(8)<<rd(); return ss.str(); }

struct HttpResp { int code=0; std::string body; bool ok=false; };
inline HttpResp httpPost(const std::string& host,int port,const std::string& path,const std::string& jsonBody,bool useSSL=false,const std::string& extraHeader="",const std::string& contentType="application/json"){
    HttpResp r; if(!port)port=useSSL?443:80;
    HINTERNET hSession=WinHttpOpen(L"SecurityGuard/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hSession)return r;
    int wlen=MultiByteToWideChar(CP_UTF8,0,host.c_str(),-1,nullptr,0); std::wstring whost(wlen,0); MultiByteToWideChar(CP_UTF8,0,host.c_str(),-1,&whost[0],wlen);
    HINTERNET hConnect=WinHttpConnect(hSession,whost.c_str(),(INTERNET_PORT)port,0);
    if(!hConnect){WinHttpCloseHandle(hSession);return r;}
    int wplen=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0); std::wstring wpath(wplen,0); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,&wpath[0],wplen);
    DWORD flags=useSSL?WINHTTP_FLAG_SECURE:0;
    HINTERNET hRequest=WinHttpOpenRequest(hConnect,L"POST",wpath.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!hRequest){WinHttpCloseHandle(hConnect);WinHttpCloseHandle(hSession);return r;}
    DWORD timeout=30000; WinHttpSetOption(hRequest,WINHTTP_OPTION_CONNECT_TIMEOUT,&timeout,sizeof(timeout)); WinHttpSetOption(hRequest,WINHTTP_OPTION_SEND_TIMEOUT,&timeout,sizeof(timeout)); WinHttpSetOption(hRequest,WINHTTP_OPTION_RECEIVE_TIMEOUT,&timeout,sizeof(timeout));
    std::wstring headers=L"Content-Type: "+std::wstring(contentType.begin(),contentType.end())+L"\r\n";
    if(!extraHeader.empty()){ int ehLen=MultiByteToWideChar(CP_UTF8,0,extraHeader.c_str(),-1,nullptr,0); std::wstring wExtra(ehLen,0); MultiByteToWideChar(CP_UTF8,0,extraHeader.c_str(),-1,&wExtra[0],ehLen); if(!wExtra.empty()&&wExtra.back()==L'\0')wExtra.pop_back(); headers+=wExtra; }
    BOOL bResult=WinHttpSendRequest(hRequest,headers.c_str(),(DWORD)-1,(LPVOID)jsonBody.c_str(),(DWORD)jsonBody.size(),(DWORD)jsonBody.size(),0);
    if(bResult&&WinHttpReceiveResponse(hRequest,nullptr)){ DWORD statusCode=0,sz=sizeof(statusCode); WinHttpQueryHeaders(hRequest,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&statusCode,&sz,WINHTTP_NO_HEADER_INDEX); r.code=(int)statusCode; r.ok=(statusCode==200); DWORD avail=0; while(WinHttpQueryDataAvailable(hRequest,&avail)&&avail>0){ std::vector<char> buf(avail+1,0); DWORD bytesRead=0; if(WinHttpReadData(hRequest,buf.data(),avail,&bytesRead))r.body.append(buf.data(),bytesRead); } }
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return r;
}
inline std::string jStr(const std::string& j,const std::string& key){ std::string needle="\""+key+"\""; size_t pos=j.find(needle); if(pos==std::string::npos)return""; pos=j.find(':',pos+needle.size()); if(pos==std::string::npos)return""; pos++; while(pos<j.size()&&(j[pos]==' '||j[pos]=='\t'||j[pos]=='\n'))pos++; if(pos>=j.size())return""; if(j[pos]=='"'){ pos++; size_t endPos=j.find('"',pos); if(endPos==std::string::npos)return""; return j.substr(pos,endPos-pos); } else { size_t endPos=pos; while(endPos<j.size()&&j[endPos]!=','&&j[endPos]!='}'&&j[endPos]!=']'&&j[endPos]!=' '&&j[endPos]!='\n')endPos++; return j.substr(pos,endPos-pos); } }
inline bool jBool(const std::string& j,const std::string& key){ std::string needle="\""+key+"\":"; size_t pos=j.find(needle); if(pos==std::string::npos)return false; pos+=needle.size(); while(pos<j.size()&&(j[pos]==' '||j[pos]=='\t'))pos++; return(pos+4<=j.size()&&j.substr(pos,4)=="true"); }
inline int jInt(const std::string& j,const std::string& key){ std::string needle="\""+key+"\":"; size_t pos=j.find(needle); if(pos==std::string::npos)return 0; pos+=needle.size(); while(pos<j.size()&&(j[pos]==' '||j[pos]=='\t'))pos++; std::string num; while(pos<j.size()&&j[pos]>='0'&&j[pos]<='9')num+=j[pos++]; if(num.empty())return 0; try{return std::stoi(num);}catch(...){return 0;} }

} // namespace detail

struct Session { bool success=false; std::string message,token; int cardId=0,programId=0; std::string loginAt,expiresAt; int heartbeatInterval=60,heartbeatTimeout=180; std::string cardExpireTime; bool ok()const{return success;} };
struct HeartbeatResult { bool success=false,kicked=false; std::string expiresAt,reason; };
struct AntiCheatResult { bool passed; int detectedType; std::string reason; };

class Client {
public:
    Client(std::string host,int port,std::string apiKey,bool useSSL=false):m_host(std::move(host)),m_port(port),m_apiKey(std::move(apiKey)),m_ssl(useSSL),m_basePath("/v1/session"){}
    void setBasePath(const std::string&p){m_basePath=p;}
    void setXorKey(const std::string&key){m_xorKey=key;}
    Session login(const std::string& cardKey){
        Session s; std::string fingerprint=detail::getDeviceFingerprint(); std::string deviceName=detail::getComputerName();
        std::string json="{\"api_key\":\""+m_apiKey+"\",\"card_key\":\""+cardKey+"\",\"device_fingerprint\":\""+fingerprint+"\",\"device_name\":\""+deviceName+"\"}";
        auto r=send(m_basePath+"/login",json);
        if(r.code==0){s.message="无法连接服务器";return s;}
        s.success=detail::jBool(r.body,"success"); s.message=detail::jStr(r.body,"message"); if(s.message.empty())s.message=s.success?"验证成功":"验证失败";
        if(s.success){ s.token=detail::jStr(r.body,"session_token"); s.cardId=detail::jInt(r.body,"card_id"); s.programId=detail::jInt(r.body,"program_id"); s.loginAt=detail::jStr(r.body,"login_at"); s.expiresAt=detail::jStr(r.body,"expires_at"); s.heartbeatInterval=detail::jInt(r.body,"heartbeat_interval"); s.heartbeatTimeout=detail::jInt(r.body,"heartbeat_timeout"); if(!s.heartbeatInterval)s.heartbeatInterval=60; if(!s.heartbeatTimeout)s.heartbeatTimeout=180; s.cardExpireTime=detail::jStr(r.body,"card_expire_time"); }
        return s;
    }
    HeartbeatResult heartbeat(const std::string& token){
        HeartbeatResult h; if(token.empty())return h;
        std::string json="{\"session_token\":\""+token+"\"}"; auto r=send(m_basePath+"/heartbeat",json);
        if(r.code==0)return h; h.success=detail::jBool(r.body,"success"); h.kicked=detail::jBool(r.body,"kicked"); h.expiresAt=detail::jStr(r.body,"expires_at"); h.reason=detail::jStr(r.body,"reason"); return h;
    }
    void logout(const std::string& token){ if(token.empty())return; send(m_basePath+"/logout","{\"session_token\":\""+token+"\"}"); }
private:
    std::string m_host; int m_port; std::string m_apiKey; bool m_ssl; std::string m_basePath,m_xorKey;
    detail::HttpResp send(const std::string& path,const std::string& json){
        if(m_xorKey.empty())return detail::httpPost(m_host,m_port,path,json,m_ssl);
        std::string nonce=detail::genNonce(); std::string dynKey=detail::sha256(m_xorKey+nonce);
        std::string xored=detail::xorEncryptString(json,dynKey); std::string encoded=detail::base64Encode(xored);
        std::string headers="X-Encrypt: xor\r\nX-Nonce: "+nonce+"\r\n";
        auto r=detail::httpPost(m_host,m_port,path,encoded,m_ssl,headers,"text/plain");
        if(!r.body.empty()){ std::string decoded=detail::base64Decode(r.body); r.body=detail::xorEncryptString(decoded,dynKey); }
        return r;
    }
};

class Heartbeat {
public:
    Heartbeat()=default; ~Heartbeat(){stop();}
    Heartbeat(const Heartbeat&)=delete; Heartbeat&operator=(const Heartbeat&)=delete;
    void start(Client&client,const std::string&token,std::function<void(const HeartbeatResult&)>onKicked=nullptr,int intervalSec=0,int maxFailures=5){
        stop(); m_client=&client; m_token=token; m_onKicked=std::move(onKicked); m_interval=intervalSec>0?intervalSec:60; m_maxFailures=maxFailures; m_failed=0; m_running=true; m_thread=std::thread(&Heartbeat::run,this);
    }
    void stop(){ m_running=false; if(m_thread.joinable())m_thread.join(); }
    bool isRunning()const{return m_running;}
private:
    Client*m_client=nullptr; std::string m_token; std::function<void(const HeartbeatResult&)>m_onKicked; int m_interval=60,m_maxFailures=5,m_failed=0; std::atomic<bool>m_running{false}; std::thread m_thread;
    void run(){
        while(m_running){ for(int i=0;i<m_interval&&m_running;i++)std::this_thread::sleep_for(std::chrono::seconds(1)); if(!m_running)break; auto h=m_client->heartbeat(m_token); if(h.success&&!h.kicked){m_failed=0;} else if(h.kicked){if(m_onKicked)m_onKicked(h);m_running=false;return;} else {m_failed++; if(m_failed>m_maxFailures){if(m_onKicked)m_onKicked(h);m_running=false;return;}} }
    }
};

class AntiCheat {
public:
    static AntiCheatResult check(){
        AntiCheatResult result={true,0,""};
        if(isDebuggerPresent()){result.passed=false;result.detectedType=1;result.reason="检测到调试器";return result;}
        if(isVirtualMachine()){result.passed=false;result.detectedType=2;result.reason="检测到虚拟机环境";return result;}
        if(isAnalysisToolsPresent()){result.passed=false;result.detectedType=2;result.reason="检测到分析工具";return result;}
        return result;
    }
private:
    static bool isDebuggerPresent(){ BOOL dp=FALSE; if(CheckRemoteDebuggerPresent(GetCurrentProcess(),&dp)){if(dp)return true;} if(IsDebuggerPresent())return true; DWORD start=GetTickCount(); volatile int dummy=0; for(int i=0;i<1000;i++)dummy++; if(GetTickCount()-start>500)return true; return false; }
    static bool isVirtualMachine(){ int ci[4]={0}; __cpuid(ci,1); if(ci[2]&(1<<31))return true; int ci0[4]={0}; __cpuid(ci0,0); char v[13]={0}; *(int*)v=ci0[1]; *(int*)(v+4)=ci0[3]; *(int*)(v+8)=ci0[2]; if(strstr(v,"VMware")||strstr(v,"Microsoft")||strstr(v,"KVM")||strstr(v,"Xen")||strstr(v,"QEMU")||strstr(v,"Virtual"))return true; return false; }
    static bool isAnalysisToolsPresent(){ const char* tools[]={"OLLYDBG","WINDBG","x64dbg","x32dbg","ida","immunitydebugger","cheat engine","cheatengine","process hacker","processhacker"}; char wt[256]; for(const char* tool:tools){ if(FindWindowA(nullptr,tool))return true; memset(wt,0,sizeof(wt)); if(GetWindowTextA(GetForegroundWindow(),wt,256)>0){ char lt[256]; strncpy_s(lt,wt,255); for(char*p=lt;*p;p++)*p=tolower(*p); char ltool[64]; strncpy_s(ltool,tool,63); for(char*p=ltool;*p;p++)*p=tolower(*p); if(strstr(lt,ltool))return true; } } return false; }
};

// ═══ 全局变量 ═══
inline std::atomic<bool> g_kvKicked{false};
inline Client* g_kvClient=nullptr;
inline Heartbeat* g_kvHeartbeat=nullptr;
inline Session g_kvSession;
inline bool g_initialized=false;
inline bool g_cleanupRegistered=false;

inline BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType){
    if(ctrlType==CTRL_CLOSE_EVENT||ctrlType==CTRL_C_EVENT||ctrlType==CTRL_BREAK_EVENT){
        if(g_kvHeartbeat)g_kvHeartbeat->stop();
        if(g_kvClient&&g_kvSession.ok())g_kvClient->logout(g_kvSession.token);
        Sleep(500); exit(0); return TRUE;
    }
    return FALSE;
}

inline bool Init(const char* serverHost,int serverPort,const char* apiKey,bool useSSL=false){
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    if(!g_cleanupRegistered){ SetConsoleCtrlHandler(ConsoleCtrlHandler,TRUE); g_cleanupRegistered=true; }
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║           Security Guard 初始化                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    auto antiResult=AntiCheat::check();
    if(!antiResult.passed){ printf("\n[警告] %s\n",antiResult.reason.c_str()); printf("程序即将退出...\n"); Sleep(2000); return false; }
    g_kvClient=new Client(serverHost,serverPort,apiKey,useSSL);
    g_kvClient->setXorKey("KamiVault2024!");
    g_initialized=true; return true;
}

inline bool VerifyCard(const std::string& cardKey){
    if(!g_kvClient)return false;
    printf("\n[验证] 正在验证卡密...\n");
    for(int attempt=1;attempt<=3;attempt++){
        g_kvSession=g_kvClient->login(cardKey);
        if(g_kvSession.ok()){
            std::string remainText;
            std::string es=g_kvSession.cardExpireTime;
            if(!es.empty()&&es!="null"&&es.size()>=16){
                int y,m,d,h,mi,s;
                if(sscanf_s(es.c_str(),"%d-%d-%dT%d:%d:%d",&y,&m,&d,&h,&mi,&s)==6){
                    struct tm et={}; et.tm_year=y-1900; et.tm_mon=m-1; et.tm_mday=d; et.tm_hour=h; et.tm_min=mi; et.tm_sec=s; et.tm_isdst=-1;
                    time_t expireTime=mktime(&et); time_t now=time(nullptr);
                    if(expireTime>now){ double diff=difftime(expireTime,now); int totalHours=(int)(diff/3600); int days=totalHours/24; int hours=totalHours%24; char buf[128]; if(days>0)sprintf_s(buf,"剩余时长: %d天%d小时",days,hours); else sprintf_s(buf,"剩余时长: %d小时",hours); remainText=buf; }
                }
            }
            if(remainText.empty()&&es=="null")remainText="卡密已到期"; else if(remainText.empty())remainText="永久有效";
            printf("[验证] 验证成功! %s | 心跳间隔: %d秒\n",remainText.c_str(),g_kvSession.heartbeatInterval);
            return true;
        }
        printf("[验证] [%d/3] 验证失败: %s\n",attempt,g_kvSession.message.c_str());
        if(attempt<3){ printf("[验证] 3秒后重试...\n"); Sleep(3000); }
    }
    return false;
}

inline void StartHeartbeat(std::function<void()> onKicked=nullptr){
    if(!g_kvClient||!g_kvSession.ok())return;
    g_kvHeartbeat=new Heartbeat();
    g_kvHeartbeat->start(*g_kvClient,g_kvSession.token,[onKicked](const HeartbeatResult&r){
        if(r.kicked)printf("\n[心跳] 被踢下线! 原因: %s\n",r.reason.c_str());
        else printf("\n[心跳] 连续失败，断线退出\n");
        g_kvKicked=true; if(onKicked)onKicked();
    },g_kvSession.heartbeatInterval,3);
    printf("[心跳] 已启动，间隔 %d秒\n\n",g_kvSession.heartbeatInterval);
}

inline bool IsKicked(){ return g_kvKicked; }
inline void StopHeartbeat(){ if(g_kvHeartbeat){ g_kvHeartbeat->stop(); delete g_kvHeartbeat; g_kvHeartbeat=nullptr; printf("[心跳] 已停止\n"); } }

inline void Cleanup(){
    printf("\n[清理] 正在清理资源...\n");
    if(g_kvHeartbeat){ g_kvHeartbeat->stop(); delete g_kvHeartbeat; g_kvHeartbeat=nullptr; }
    if(g_kvClient){ if(g_kvSession.ok())g_kvClient->logout(g_kvSession.token); delete g_kvClient; g_kvClient=nullptr; }
    printf("[清理] 资源清理完成\n");
}

inline AntiCheatResult CheckAntiCheat(){ return AntiCheat::check(); }
inline std::string EncryptPacket(const std::string& data,const std::string& key){ return detail::encryptPacket(data,key); }

} // namespace Security
