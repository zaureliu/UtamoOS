/* SPDX-License-Identifier: MIT */
#include <utamo/net_services.h>
#include <utamo/e1000_ring.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks,failures;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x);} } while(0)
static const unsigned char mac[6]={0x52,0x54,0,0x12,0x34,0x56};
static unsigned char frame[1514],ip[1500],udp[1480],dhcp[300],dns[512];
static void wire_tests(void)
{
    uint32_t address=123u;
    CHECK(net_ip_parse("10.23.0.100",&address) && address==0x0a170064u);
    CHECK(!net_ip_parse("256.0.0.1",&address) && address==0x0a170064u);
    const char *bad[]={"","1","1.2.3","1.2.3.4.5","1..2.3","1.2.3.-1","1.2.3.4 ","0000.1.2.3"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i){CHECK(!net_ip_parse(bad[i],&address));}
    CHECK(net_mask_valid(0xffffff00u) && !net_mask_valid(0xff00ff00u) &&
          !net_mask_valid(0u) && !net_mask_valid(UINT32_MAX));
    const unsigned char known[]={0x00,0x01,0xf2,0x03,0xf4,0xf5,0xf6,0xf7};
    CHECK(net_checksum(known,sizeof(known))==0x220du);
    const unsigned char odd[]={0x01,0x02,0x03};CHECK(net_checksum(odd,3u)==0xfbfdu);
    size_t usize,isize,fsize;
    CHECK(udp_build(udp,sizeof(udp),0x0a170064u,0x0a170002u,50000u,53u,"odd",3u,&usize));
    CHECK(ipv4_build(ip,sizeof(ip),0x0a170064u,0x0a170002u,17u,0x1234u,udp,usize,&isize));
    CHECK(ethernet_build(frame,sizeof(frame),mac,mac,0x800u,ip,isize,&fsize));
    struct ethernet_packet e;struct ipv4_packet v;struct udp_packet u;
    CHECK(ethernet_decode(frame,fsize,&e) && e.type==0x800u && e.bytes==isize);
    CHECK(ipv4_decode(e.payload,e.bytes,&v) && v.source==0x0a170064u && v.destination==0x0a170002u);
    CHECK(udp_decode(&v,&u) && u.source==50000u && u.destination==53u && u.bytes==3u &&
          memcmp(u.payload,"odd",3u)==0);
    for(size_t i=0u;i<14u;++i){CHECK(!ethernet_decode(frame,i,&e));}
    for(size_t i=0u;i<isize;++i){CHECK(!ipv4_decode(ip,i,&v));}
    unsigned char options[24]={0x46u,0u,0u,24u};
    options[8]=64u;net_put16(options+10u,net_checksum(options,sizeof(options)));
    CHECK(!ipv4_decode(options,sizeof(options),&v)); /* Options are explicitly unsupported. */
    const unsigned char saved=ip[10];ip[10]^=1u;CHECK(!ipv4_decode(ip,isize,&v));ip[10]=saved;
    const uint16_t fragments[]={1u,0x2000u,0x8000u,0x3fffu};
    for(size_t i=0u;i<4u;++i){
        net_put16(ip+6u,fragments[i]);net_put16(ip+10u,0u);net_put16(ip+10u,net_checksum(ip,20u));
        CHECK(!ipv4_decode(ip,isize,&v));
    }
    net_put16(ip+6u,0x4000u);net_put16(ip+10u,0u);net_put16(ip+10u,net_checksum(ip,20u));
    CHECK(ipv4_decode(ip,isize,&v));
    ip[isize-1u]^=1u;CHECK(!udp_decode(&v,&u));ip[isize-1u]^=1u;
    net_put16(ip+20u+6u,0u);CHECK(udp_decode(&v,&u)); /* IPv4 permits omitted UDP checksum. */
    net_put16(ip+20u+4u,7u);CHECK(!udp_decode(&v,&u));
    unsigned char echo[9]={8u,0u,0u,0u,0x55u,0x54u,0u,7u,0xa5u};
    net_put16(echo+2u,net_checksum(echo,sizeof(echo)));bool reply;uint16_t id,seq;
    CHECK(icmp_echo_decode(echo,sizeof(echo),&reply,&id,&seq) && !reply && id==0x5554u && seq==7u);
    echo[8]^=1u;CHECK(!icmp_echo_decode(echo,sizeof(echo),&reply,&id,&seq));
    struct arp_packet a={.operation=1u,.sender_ip=0x0a170064u,.target_ip=0x0a170002u},parsed;
    memcpy(a.sender_mac,mac,6u);
    CHECK(arp_build(frame,sizeof(frame),&a) && arp_decode(frame,28u,&parsed));
    CHECK(parsed.target_ip==a.target_ip && memcmp(parsed.sender_mac,mac,6u)==0);
    for(size_t i=0u;i<28u;++i){CHECK(!arp_decode(frame,i,&parsed));}
    frame[4]=255u;CHECK(!arp_decode(frame,28u,&parsed));
    bool discarding=false;
    CHECK(e1000_rx_valid(1514u,3u,0u,&discarding) && !discarding);
    CHECK(!e1000_rx_valid(2049u,3u,0u,&discarding));
    CHECK(!e1000_rx_valid(100u,1u,0u,&discarding) && discarding);
    CHECK(!e1000_rx_valid(100u,3u,0u,&discarding) && !discarding);
    CHECK(e1000_rx_valid(60u,3u,0u,&discarding));
    CHECK(!e1000_rx_valid(60u,3u,1u,&discarding));
    struct e1000_tx_descriptor tx,old;
    memset(&old,0xa5,sizeof(old));tx=old;
    CHECK(!e1000_tx_prepare(&tx,0x100001u,60u) && memcmp(&old,&tx,sizeof(tx))==0);
    CHECK(!e1000_tx_prepare(&tx,0x100000u,1515u));
    CHECK(e1000_tx_prepare(&tx,0x100000u,60u) && tx.command==11u && tx.length==60u && tx.status==0u);
}
static void dhcp_fixture(void)
{
    memset(dhcp,0,sizeof(dhcp));dhcp[0]=2u;dhcp[1]=1u;dhcp[2]=6u;
    net_put32(dhcp+4u,0x12345678u);net_put32(dhcp+16u,0x0a170064u);memcpy(dhcp+28u,mac,6u);
    net_put32(dhcp+236u,0x63825363u);
    const unsigned char options[]={53,1,2,1,4,255,255,255,0,3,4,10,23,0,2,
        6,4,10,23,0,3,51,4,0,0,14,16,54,4,10,23,0,2,255};
    memcpy(dhcp+240u,options,sizeof(options));
}
static void dhcp_tests(void)
{
    struct dhcp_offer out,old;
    memset(&old,0xa5,sizeof(old));out=old;dhcp_fixture();
    CHECK(dhcp_decode(dhcp,sizeof(dhcp),0x12345678u,mac,&out));
    CHECK(out.type==2u && out.address==0x0a170064u && out.gateway==0x0a170002u &&
          out.mask==0xffffff00u && out.dns==0x0a170003u && out.lease_seconds==3600u);
    for(size_t i=0u;i<274u;++i){out=old;CHECK(!dhcp_decode(dhcp,i,0x12345678u,mac,&out));CHECK(memcmp(&out,&old,sizeof(out))==0);}
    CHECK(!dhcp_decode(dhcp,sizeof(dhcp),1u,mac,&out));
    const struct{size_t offset;unsigned char value;} bad[]={
        {0,1},{1,2},{2,16},{3,1},{4,0},{12,1},{24,1},{28,0},{236,0},
        {241,2},{242,5+1},{244,255},{245,0},{254,100},{254,0},{254,255},
        {260,0},{260,255},{260,100},{272,0},{272,255},{272,100},{273,0}
    };
    for(size_t i=0u;i<sizeof(bad)/sizeof(bad[0]);++i){
        dhcp_fixture();dhcp[bad[i].offset]=bad[i].value;out=old;
        CHECK(!dhcp_decode(dhcp,sizeof(dhcp),0x12345678u,mac,&out));
        CHECK(memcmp(&out,&old,sizeof(out))==0);
    }
    dhcp_fixture();CHECK(dhcp_decode(dhcp,sizeof(dhcp),0x12345678u,mac,&out));
    size_t written=0u;
    CHECK(dhcp_build(frame,sizeof(frame),3u,0x11111111u,mac,&out,&written) && written==300u &&
          net_get32(frame+4u)==0x11111111u && net_get16(frame+10u)==0x8000u);
    CHECK(frame[240]==53u && frame[242]==3u && frame[263]==50u && net_get32(frame+265u)==out.address);
    CHECK(!dhcp_build(frame,299u,1u,1u,mac,NULL,&written));
}
static size_t dns_fixture(void)
{
    size_t written;
    if(!dns_build_query(dns,sizeof(dns),"fixture.test",0x4567u,&written)){return 0u;}
    net_put16(dns+2u,0x8180u);net_put16(dns+6u,1u);
    const unsigned char answer[]={0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,192,0,2,123};
    memcpy(dns+written,answer,sizeof(answer));return written+sizeof(answer);
}
static void dns_tests(void)
{
    uint32_t address=0x11223344u;const size_t bytes=dns_fixture();
    CHECK(bytes==46u && dns_decode_reply(dns,bytes,"FIXTURE.test",0x4567u,&address) && address==0xc000027bu);
    for(size_t i=0u;i<bytes;++i){address=0x11223344u;CHECK(!dns_decode_reply(dns,i,"fixture.test",0x4567u,&address));CHECK(address==0x11223344u);}
    CHECK(!dns_decode_reply(dns,bytes,"other.test",0x4567u,&address));
    CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4568u,&address));
    const char *invalid[]={"",".","a.","a..b",".a","a/b","a b","a\\b"};
    for(size_t i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i){CHECK(!dns_name_valid(invalid[i]));}
    char oversized[66];memset(oversized,'a',65u);oversized[65]='\0';CHECK(!dns_name_valid(oversized));
    char decoded[254],old[254];memset(old,0xa5,sizeof(old));size_t consumed=55u;
    dns[30]=0xc0u;dns[31]=30u;memcpy(decoded,old,sizeof(old));
    CHECK(!dns_decode_name(dns,bytes,30u,decoded,&consumed) && consumed==55u && memcmp(old,decoded,sizeof(old))==0);
    dns[30]=0xffu;dns[31]=0xffu;CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address));
    (void)dns_fixture();dns[30]=0xc0u;dns[31]=31u;CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address));
    (void)dns_fixture();dns[30]=0xc0u;dns[31]=0u;CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address));
    (void)dns_fixture();dns[40]=0xffu;dns[41]=0xffu;CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address));
    (void)dns_fixture();dns[2]|=2u;CHECK(!dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address));
    /* Compressed CNAME plus A, followed by a CNAME cycle. */
    size_t alias_bytes;
    CHECK(dns_build_query(dns,sizeof(dns),"fixture.test",0x4567u,&alias_bytes));
    net_put16(dns+2u,0x8180u);net_put16(dns+6u,2u);
    const unsigned char cname[]={0xc0,0x0c,0,5,0,1,0,0,0,60,0,8,5,'a','l','i','a','s',0xc0,0x14};
    memcpy(dns+alias_bytes,cname,sizeof(cname));alias_bytes+=sizeof(cname);
    const unsigned char alias_a[]={0xc0,0x2a,0,1,0,1,0,0,0,60,0,4,192,0,2,123};
    memcpy(dns+alias_bytes,alias_a,sizeof(alias_a));alias_bytes+=sizeof(alias_a);
    CHECK(dns_decode_reply(dns,alias_bytes,"fixture.test",0x4567u,&address) && address==0xc000027bu);
    dns[52]=0u;dns[53]=5u; /* second RR CNAME alias.test -> fixture.test */
    dns[60]=0u;dns[61]=2u;dns[62]=0xc0u;dns[63]=0x0cu;
    address=0x11223344u;
    CHECK(!dns_decode_reply(dns,64u,"fixture.test",0x4567u,&address) && address==0x11223344u);
    /* Deterministic hostile packets: safety and unchanged outputs on rejection. */
    uint32_t random=0x41535452u;
    for(size_t iteration=0u;iteration<1500u;++iteration){
        (void)dns_fixture();random=random*1664525u+1013904223u;
        dns[random%bytes]^=(unsigned char)((random>>24u)|1u);
        address=0x11223344u;
        const bool valid=dns_decode_reply(dns,bytes,"fixture.test",0x4567u,&address);
        CHECK(valid || address==0x11223344u);
        dhcp_fixture();random=random*1664525u+1013904223u;
        dhcp[random%sizeof(dhcp)]^=(unsigned char)((random>>24u)|1u);
        struct dhcp_offer offer,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));offer=sentinel;
        const bool accepted=dhcp_decode(dhcp,sizeof(dhcp),0x12345678u,mac,&offer);
        CHECK(accepted || memcmp(&offer,&sentinel,sizeof(offer))==0);
    }
}
int main(void)
{
    wire_tests();dhcp_tests();dns_tests();
    printf("Network packet host tests: %u checks, %u failures\n",checks,failures);
    return failures==0u?0:1;
}
