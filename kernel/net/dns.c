/* SPDX-License-Identifier: MIT */
#include <utamo/net_services.h>
#include <utamo/string.h>
static bool character(unsigned char c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
        (c>='0' && c<='9') || c=='-' || c=='_';
}
static char lower(unsigned char c) { return (char)(c>='A' && c<='Z'?c+('a'-'A'):c); }
bool dns_name_valid(const char *name)
{
    if (name==NULL) { return false; }
    size_t label=0u;
    for (size_t i=0u;i<UTAMO_DNS_NAME_MAX;++i) {
        const unsigned char c=(unsigned char)name[i];
        if(c==0u) { return i!=0u && label!=0u; }
        if(c=='.') { if(label==0u){return false;}label=0u; }
        else if(!character(c) || ++label>63u) { return false; }
    }
    return false;
}
bool dns_decode_name(const void *message, size_t bytes, size_t offset,
                      char out[UTAMO_DNS_NAME_MAX], size_t *consumed)
{
    if(message==NULL || out==NULL || consumed==NULL || bytes>UTAMO_DNS_MESSAGE_MAX ||
       offset>=bytes) { return false; }
    const unsigned char *p=message;
    char result[UTAMO_DNS_NAME_MAX];
    size_t position=offset,used=0u,encoded=0u;
    bool jumped=false;
    for(size_t hops=0u;hops<128u;++hops) {
        if(position>=bytes) { return false; }
        const unsigned char length=p[position];
        if((length&0xc0u)==0xc0u) {
            if(position+1u>=bytes) { return false; }
            const size_t target=((size_t)(length&63u)<<8u)|p[position+1u];
            /* RFC1035 pointers reference earlier names. Backward-only plus a
             * hop bound rejects cycles and arbitrary forward references. */
            if(target<12u || target>=position) { return false; }
            if(!jumped) { encoded+=2u; }
            jumped=true;position=target;continue;
        }
        if((length&0xc0u)!=0u) { return false; }
        ++position;if(!jumped){++encoded;}
        if(length==0u) {
            result[used]='\0';memcpy(out,result,used+1u);*consumed=encoded;return true;
        }
        if(length>bytes-position || used+(used!=0u?1u:0u)+length>=UTAMO_DNS_NAME_MAX) {
            return false;
        }
        if(used!=0u) { result[used++]='.'; }
        for(size_t i=0u;i<length;++i) {
            if(!character(p[position+i])) { return false; }
            result[used++]=lower(p[position+i]);
        }
        position+=length;if(!jumped){encoded+=length;}
    }
    return false;
}
bool dns_build_query(void *out, size_t capacity, const char *name, uint16_t id, size_t *written)
{
    if(out==NULL || written==NULL || !dns_name_valid(name)) { return false; }
    const size_t length=strlen(name),needed=length+18u;
    if(capacity<needed || needed>UTAMO_DNS_MESSAGE_MAX) { return false; }
    unsigned char *p=out;memset(p,0,12u);
    net_put16(p,id);net_put16(p+2u,0x100u);net_put16(p+4u,1u);
    size_t at=12u,begin=0u;
    for(size_t i=0u;i<=length;++i) {
        if(name[i]=='.' || name[i]=='\0') {
            p[at++]=(unsigned char)(i-begin);
            for(size_t j=begin;j<i;++j) { p[at++]=(unsigned char)lower((unsigned char)name[j]); }
            begin=i+1u;
        }
    }
    p[at++]=0u;net_put16(p+at,1u);net_put16(p+at+2u,1u);at+=4u;
    *written=at;return true;
}
struct dns_record {
    char owner[UTAMO_DNS_NAME_MAX],target[UTAMO_DNS_NAME_MAX];
    uint16_t type;
    uint32_t address;
};
bool dns_decode_reply(const void *data, size_t bytes, const char *name, uint16_t id, uint32_t *address)
{
    if(data==NULL || address==NULL || bytes<12u || bytes>UTAMO_DNS_MESSAGE_MAX ||
       !dns_name_valid(name)) { return false; }
    const unsigned char *p=data;const uint16_t flags=net_get16(p+2u);
    const size_t answers=net_get16(p+6u);
    const size_t records=answers+net_get16(p+8u)+net_get16(p+10u);
    if(net_get16(p)!=id || (flags&0x8000u)==0u || (flags&0x7a4fu)!=0u ||
       net_get16(p+4u)!=1u || answers==0u || answers>16u || records>32u) { return false; }
    char expected[UTAMO_DNS_NAME_MAX],decoded[UTAMO_DNS_NAME_MAX];
    const size_t name_length=strlen(name);
    for(size_t i=0u;i<=name_length;++i) { expected[i]=lower((unsigned char)name[i]); }
    size_t used;
    if(!dns_decode_name(p,bytes,12u,decoded,&used) || strcmp(decoded,expected)!=0 ||
       used>bytes-12u || bytes-12u-used<4u ||
       net_get16(p+12u+used)!=1u || net_get16(p+14u+used)!=1u) { return false; }
    size_t offset=16u+used;
    struct dns_record answer[16];
    memset(answer,0,sizeof(answer));
    for(size_t i=0u;i<records;++i) {
        if(!dns_decode_name(p,bytes,offset,decoded,&used) || used>bytes-offset) { return false; }
        offset+=used;
        if(bytes-offset<10u) { return false; }
        const uint16_t type=net_get16(p+offset),cls=net_get16(p+offset+2u);
        const size_t length=net_get16(p+offset+8u);offset+=10u;
        if(length>bytes-offset) { return false; }
        if(i<answers && cls==1u) {
            memcpy(answer[i].owner,decoded,strlen(decoded)+1u);
            answer[i].type=type;
            if(type==1u) {
                if(length!=4u) { return false; }
                answer[i].address=net_get32(p+offset);
            } else if(type==5u) {
                if(!dns_decode_name(p,bytes,offset,answer[i].target,&used) ||
                   used!=length || answer[i].target[0]=='\0') { return false; }
            }
        }
        offset+=length;
    }
    if(offset!=bytes) { return false; }
    for(size_t hop=0u;hop<=answers;++hop) {
        bool alias=false;
        for(size_t i=0u;i<answers;++i) {
            if(answer[i].type==1u && strcmp(answer[i].owner,expected)==0) {
                *address=answer[i].address;return true;
            }
        }
        for(size_t i=0u;i<answers;++i) {
            if(answer[i].type==5u && strcmp(answer[i].owner,expected)==0) {
                memcpy(expected,answer[i].target,strlen(answer[i].target)+1u);
                alias=true;break;
            }
        }
        if(!alias) { return false; }
    }
    return false;
}
