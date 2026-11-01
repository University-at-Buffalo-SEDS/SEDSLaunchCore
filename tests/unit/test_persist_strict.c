/* Keep executable fault assertions active in the CI Release profile too. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "launchcore/persist.h"
#include "launchcore/storage.h"
#include "launchcore/image.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Existing mock's backing store, wrapped in test-only physical granule/1->0
 * semantics and deterministic partial program/erase cuts. Legacy fake unchanged. */
static const launchcore_storage_driver_t *backing;
static launchcore_storage_layout_t geometry;
static unsigned granule, init_calls, writes, erases, events, cut_after, read_calls, fail_read;
static uint8_t saved[16384];
static unsigned partial_read, partial_prefixes, value_reads;
static uint32_t corrupt_value_address;
#define KEY UINT32_C(0x42524745)
#define CAL UINT32_C(0x44414341)
static uint32_t aligned(uint32_t x) { return (x + granule - 1u) & ~(granule - 1u); }
static launchcore_storage_status_t init(void) { ++init_calls; return LAUNCHCORE_STORAGE_OK; }
static launchcore_storage_status_t read_data(uint32_t a, void *p, uint32_t n) {
    if (++read_calls == fail_read) {
        if(partial_read&&n!=0u){assert(backing->read(a,p,n>1u?n/2u:1u)==LAUNCHCORE_STORAGE_OK);++partial_prefixes;}
        return LAUNCHCORE_STORAGE_ERR_READ;
    }
    launchcore_storage_status_t result=backing->read(a,p,n);
    if(result==LAUNCHCORE_STORAGE_OK&&a==corrupt_value_address&&++value_reads==2u&&n!=0u)((uint8_t*)p)[0]^=1u;
    return result;
}
static bool cut(void) { unsigned current = events++; return cut_after != UINT32_MAX && current >= cut_after; }
static launchcore_storage_status_t program(uint32_t a,const void *p,uint32_t n) {
    ++writes;
    assert(a % granule == 0u);
    for(uint32_t o=0;o<n;o+=granule) {
        if(cut()) return LAUNCHCORE_STORAGE_ERR_WRITE;
        uint8_t block[16], old[16], next[16];
        uint32_t base=(a+o)&~15u, within=(a+o)-base;
        assert(backing->read(base,block,sizeof block)==LAUNCHCORE_STORAGE_OK);
        memcpy(old,block,sizeof old);memset(next,0xFF,sizeof next);
        uint32_t take=n-o<granule?n-o:granule;memcpy(next,(const uint8_t*)p+o,take);
        for(unsigned i=0;i<granule;++i) {
            assert((old[within+i]&next[i])==next[i]);block[within+i]=next[i];
        }
        assert(backing->write(base,block,sizeof block)==LAUNCHCORE_STORAGE_OK);
    }
    return LAUNCHCORE_STORAGE_OK;
}
static launchcore_storage_status_t erase_data(uint32_t a,uint32_t n) {
    ++erases;assert(a%geometry.persistent_data_erase_size==0u);
    assert(n%geometry.persistent_data_erase_size==0u);
    /* Partial-sector erase is one bounded fault witness, not a claim that
     * real interrupted erase clears bytes in this deterministic order. */
    for(uint32_t o=0;o<n;o+=granule) {
        if(cut())return LAUNCHCORE_STORAGE_ERR_ERASE;
        uint8_t block[16];uint32_t base=(a+o)&~15u, within=(a+o)-base;
        assert(backing->read(base,block,sizeof block)==LAUNCHCORE_STORAGE_OK);
        memset(block+within,0xFF,granule);
        assert(backing->write(base,block,sizeof block)==LAUNCHCORE_STORAGE_OK);
    }
    return LAUNCHCORE_STORAGE_OK;
}
static const launchcore_storage_layout_t *layout(void){return &geometry;}
static const launchcore_storage_driver_t fake={.init=init,.read=read_data,.write=program,.erase=erase_data,.layout=layout};
static void counters(void){init_calls=writes=erases=events=read_calls=0;cut_after=fail_read=UINT32_MAX;partial_read=partial_prefixes=value_reads=0;corrupt_value_address=UINT32_MAX;}
static void reset(unsigned width) {
    granule=width;geometry=*backing->layout();geometry.persistent_data_size=width==8?4096:16384;
    geometry.persistent_data_erase_size=geometry.persistent_data_size/2u;geometry.persistent_data_write_size=width;
    assert(backing->erase(geometry.persistent_data_base,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);
    launchcore_storage_set_driver(&fake);counters();
}
static void capture(void){assert(backing->read(geometry.persistent_data_base,saved,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);}
static void restore(void){assert(backing->erase(geometry.persistent_data_base,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);assert(backing->write(geometry.persistent_data_base,saved,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);counters();}
static void poke(uint32_t address,const void *p,uint32_t n) {
    uint32_t base=address&~15u;uint8_t block[32];uint32_t span=aligned(address-base+n);span=(span+15u)&~15u;
    assert(span<=sizeof block);assert(backing->read(base,block,span)==LAUNCHCORE_STORAGE_OK);memcpy(block+address-base,p,n);assert(backing->write(base,block,span)==LAUNCHCORE_STORAGE_OK);
}
typedef struct __attribute__((packed)){uint32_t magic;uint16_t version,size;uint32_t generation,crc,committed;} bank_header;
typedef struct __attribute__((packed)){uint32_t magic,key,sequence;uint16_t size,flags;uint32_t value_crc,header_crc;} record_header;
static uint32_t last_record(uint32_t key) {
    uint32_t base=geometry.persistent_data_base,offset=aligned(sizeof(bank_header)),found=0;
    while(offset+sizeof(record_header)<=geometry.persistent_data_size/2u) {
        record_header h;assert(backing->read(base+offset,&h,sizeof h)==LAUNCHCORE_STORAGE_OK);
        if(h.magic==UINT32_MAX)break;
        assert(h.magic==UINT32_C(0x4c435052));
        if(h.key==key)found=base+offset;
        offset+=aligned(sizeof h+h.size);
    }
    assert(found);return found;
}
static void bank_generation(unsigned bank,uint32_t generation) {
    uint32_t a=geometry.persistent_data_base+bank*geometry.persistent_data_size/2u;bank_header h;
    assert(backing->read(a,&h,sizeof h)==LAUNCHCORE_STORAGE_OK);h.generation=generation;h.crc=launchcore_crc32(&h,offsetof(bank_header,crc));poke(a,&h,sizeof h);
}
static void no_mutation(unsigned a,unsigned b,unsigned c){assert(init_calls==a&&writes==b&&erases==c);}
static launchcore_persist_status_t strict_get(uint32_t key, uint64_t *value,launchcore_persist_strict_info_t *info) {
    size_t n=sizeof *value;unsigned a=init_calls,b=writes,c=erases;
    uint64_t before=*value;launchcore_persist_status_t s=launchcore_persist_get_strict(key,value,&n,info);
    no_mutation(a,b,c);if(s!=LAUNCHCORE_PERSIST_OK){assert(*value==before);assert(n==sizeof *value);}return s;
}
static void require_health(launchcore_persist_health_t health) {
    launchcore_persist_strict_info_t i;unsigned a=init_calls,b=writes,c=erases;
    launchcore_persist_status_t s=launchcore_persist_probe_strict(&i);no_mutation(a,b,c);
    assert(i.health==health);assert(s==((health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY||health==LAUNCHCORE_PERSIST_HEALTH_VIRGIN)?LAUNCHCORE_PERSIST_OK:LAUNCHCORE_PERSIST_CORRUPT));
}
static void seed(void){assert(launchcore_persist_init()==LAUNCHCORE_PERSIST_OK);uint64_t v=1;assert(launchcore_persist_set(KEY,&v,sizeof v)==LAUNCHCORE_PERSIST_OK);uint8_t cal[68];memset(cal,0x5A,sizeof cal);assert(launchcore_persist_set(CAL,cal,sizeof cal)==LAUNCHCORE_PERSIST_OK);}
static void controls(unsigned width) {
    reset(width);require_health(LAUNCHCORE_PERSIST_HEALTH_VIRGIN);launchcore_persist_strict_info_t i;
    uint64_t v=UINT64_C(0xaaaaaaaaaaaaaaaa);assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_NOT_FOUND&&i.health==LAUNCHCORE_PERSIST_HEALTH_VIRGIN&&i.record==LAUNCHCORE_PERSIST_RECORD_ABSENT);
    assert(launchcore_persist_init()==LAUNCHCORE_PERSIST_OK);require_health(LAUNCHCORE_PERSIST_HEALTH_HEALTHY);assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_NOT_FOUND&&i.record==LAUNCHCORE_PERSIST_RECORD_ABSENT);
    v=1;assert(launchcore_persist_set(KEY,&v,sizeof v)==LAUNCHCORE_PERSIST_OK);v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_OK&&v==1&&i.record==LAUNCHCORE_PERSIST_RECORD_PRESENT);
    uint8_t bytes[8];memset(bytes,0xA5,sizeof bytes);size_t n=7;assert(launchcore_persist_get_strict(KEY,bytes,&n,&i)==LAUNCHCORE_PERSIST_NO_SPACE&&n==7);for(unsigned k=0;k<8;++k)assert(bytes[k]==0xA5);
    assert(launchcore_persist_delete(KEY)==LAUNCHCORE_PERSIST_OK);v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_NOT_FOUND&&i.record==LAUNCHCORE_PERSIST_RECORD_DELETED);
    unsigned reads=read_calls;assert(launchcore_persist_get_strict(KEY,bytes,NULL,&i)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT);assert(read_calls==reads);
    assert(launchcore_persist_probe_strict(NULL)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT);assert(read_calls==reads);
    reset(width);seed();v=2;assert(launchcore_persist_set(KEY,&v,sizeof v)==LAUNCHCORE_PERSIST_OK);
    uint8_t bad=0;poke(last_record(KEY)+sizeof(record_header),&bad,1);
    v=99;size_t length=sizeof v;assert(launchcore_persist_get(KEY,&v,&length)==LAUNCHCORE_PERSIST_OK&&v==1); /* exact old fallback preserved */
    require_health(LAUNCHCORE_PERSIST_HEALTH_CORRUPT);v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_CORRUPT);
    reset(width);seed();record_header rh;uint32_t a=last_record(KEY);assert(backing->read(a,&rh,sizeof rh)==LAUNCHCORE_STORAGE_OK);rh.sequence=UINT32_MAX;rh.header_crc=launchcore_crc32(&rh,offsetof(record_header,header_crc));poke(a,&rh,sizeof rh);require_health(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    reset(width);seed();bank_generation(0,UINT32_MAX);require_health(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    reset(width);seed();uint32_t zero=0;poke(geometry.persistent_data_base,&zero,4);require_health(LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    reset(width);uint32_t tail=0;poke(geometry.persistent_data_base+geometry.persistent_data_size-16,&tail,4);require_health(LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    reset(width);seed();uint8_t bank[8192];uint32_t bank_size=geometry.persistent_data_size/2u;assert(backing->read(geometry.persistent_data_base,bank,bank_size)==LAUNCHCORE_STORAGE_OK);assert(backing->write(geometry.persistent_data_base+bank_size,bank,bank_size)==LAUNCHCORE_STORAGE_OK);require_health(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    bank_generation(1,4);require_health(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    reset(width);seed();bank_size=geometry.persistent_data_size/2u;
    assert(backing->read(geometry.persistent_data_base,bank,sizeof(bank_header))==LAUNCHCORE_STORAGE_OK);
    memset(bank+sizeof(bank_header),0xFF,aligned(sizeof(bank_header))-sizeof(bank_header));
    assert(backing->write(geometry.persistent_data_base+bank_size,bank,aligned(sizeof(bank_header)))==LAUNCHCORE_STORAGE_OK);
    bank_generation(1,2);require_health(LAUNCHCORE_PERSIST_HEALTH_HEALTHY);v=99;
    assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_CORRUPT&&i.health==LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    /* A genuinely empty predecessor/successor pair is structurally healthy;
     * absence is still not evidence that a control mode was never enabled. */
    reset(width);assert(launchcore_persist_init()==LAUNCHCORE_PERSIST_OK);
    assert(backing->read(geometry.persistent_data_base,bank,sizeof(bank_header))==LAUNCHCORE_STORAGE_OK);
    memset(bank+sizeof(bank_header),0xFF,aligned(sizeof(bank_header))-sizeof(bank_header));
    assert(backing->write(geometry.persistent_data_base+bank_size,bank,aligned(sizeof(bank_header)))==LAUNCHCORE_STORAGE_OK);
    bank_generation(1,2);v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_NOT_FOUND&&i.health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY);
    reset(width);seed();
    assert(backing->read(geometry.persistent_data_base,bank,bank_size)==LAUNCHCORE_STORAGE_OK);
    assert(backing->write(geometry.persistent_data_base+bank_size,bank,bank_size)==LAUNCHCORE_STORAGE_OK);
    bank_generation(1,2);v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_OK&&v==1);
    uint32_t changed=last_record(KEY)+bank_size;record_header changed_header;
    assert(backing->read(changed,&changed_header,sizeof changed_header)==LAUNCHCORE_STORAGE_OK);
    uint64_t changed_value=9;changed_header.value_crc=launchcore_crc32(&changed_value,sizeof changed_value);
    changed_header.header_crc=launchcore_crc32(&changed_header,offsetof(record_header,header_crc));
    poke(changed,&changed_header,sizeof changed_header);poke(changed+sizeof changed_header,&changed_value,sizeof changed_value);
    v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_CORRUPT&&i.health==LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);
    reset(width);seed();assert(launchcore_persist_delete(KEY)==LAUNCHCORE_PERSIST_OK);
    uint64_t filler=0;
    for(unsigned fill=0;fill<512;++fill) {
        assert(launchcore_persist_set(77,&filler,sizeof filler)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_probe_strict(&i)==LAUNCHCORE_PERSIST_OK);
        if(i.bank_generation==2)break;
    }
    assert(i.bank_generation==2);v=99;
    assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_NOT_FOUND&&i.record==LAUNCHCORE_PERSIST_RECORD_ABSENT);
    reset(width);seed();unsigned mutation=writes+erases+init_calls;fail_read=read_calls+1;v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_STORAGE_ERROR&&i.health==LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);assert(mutation==writes+erases+init_calls);
    reset(width);seed();capture();read_calls=0;v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_OK);unsigned all_reads=read_calls;
    for(unsigned partial=0;partial<2;++partial)for(unsigned fail=1;fail<=all_reads;++fail) {
        restore();fail_read=fail;partial_read=partial;v=99;
        assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_STORAGE_ERROR);
        assert(i.health==LAUNCHCORE_PERSIST_HEALTH_UNKNOWN);
        if(partial)assert(partial_prefixes==1);
    }
    restore();corrupt_value_address=last_record(KEY)+sizeof(record_header);value_reads=0;v=99;
    assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_CORRUPT&&value_reads==2);
    assert(i.health==LAUNCHCORE_PERSIST_HEALTH_CORRUPT);
    printf("granule%u every-read faults %u positions x2 complete/partial plus postvalidation bitflip: PASS\n",width,all_reads);
    const uint32_t malformed_sizes[] = {89u,97u,104u};
    for(unsigned k=0;k<sizeof(malformed_sizes)/sizeof(malformed_sizes[0]);++k) {
        reset(width);geometry.persistent_data_erase_size=0;
        geometry.persistent_data_size=malformed_sizes[k];
        if(malformed_sizes[k]==97u){uint8_t tail=0;poke(geometry.persistent_data_base+96u,&tail,1u);}
        v=99;assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT&&read_calls==0);
        assert(launchcore_persist_probe_strict(&i)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT&&read_calls==0);
    }
    reset(width);geometry.persistent_data_write_size=3;v=99;
    assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT&&read_calls==0);
    reset(width);geometry.persistent_data_base=UINT32_MAX-geometry.persistent_data_erase_size+1u;v=99;
    assert(strict_get(KEY,&v,&i)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT&&read_calls==0);
    printf("granule%u controls healthy/virgin/absence/deletion/corrupt-newest/header/tail/generation/IO/outbuffer: PASS\n",width);
}
static void cuts(unsigned width,bool gc) {
    reset(width);seed();uint64_t next=2;
    if(gc) {
        /* Fill active bank exactly far enough that the next boot record needs
         * GC, using actual legacy append/compact rather than handcrafted bytes. */
        uint32_t used=aligned(20)+aligned(32)+aligned(24+68);uint64_t filler=0;
        while(used+aligned(32)<=geometry.persistent_data_size/2u) {
            assert(launchcore_persist_set(77,&filler,sizeof filler)==LAUNCHCORE_PERSIST_OK);used+=aligned(32);
        }
    }
    capture();events=0;assert(launchcore_persist_set(KEY,&next,sizeof next)==LAUNCHCORE_PERSIST_OK);unsigned total=events;assert(total>0);
    require_health(LAUNCHCORE_PERSIST_HEALTH_HEALTHY);
    for(unsigned cutpoint=0;cutpoint<=total;++cutpoint) {
        restore();cut_after=cutpoint;(void)launchcore_persist_set(KEY,&next,sizeof next);cut_after=UINT32_MAX;
        launchcore_persist_strict_info_t info;unsigned a=init_calls,b=writes,c=erases;launchcore_persist_status_t s=launchcore_persist_probe_strict(&info);no_mutation(a,b,c);
        uint64_t value=99;launchcore_persist_status_t got=strict_get(KEY,&value,&info);
        if(s==LAUNCHCORE_PERSIST_OK) {
            assert(got==LAUNCHCORE_PERSIST_OK&&(value==1||value==2));uint8_t cal[68];size_t n=sizeof cal;assert(launchcore_persist_get_strict(CAL,cal,&n,&info)==LAUNCHCORE_PERSIST_OK);for(unsigned j=0;j<68;++j)assert(cal[j]==0x5A);
        } else assert(got==LAUNCHCORE_PERSIST_CORRUPT);
    }
    restore();assert(launchcore_persist_set(KEY,&next,sizeof next)==LAUNCHCORE_PERSIST_OK);require_health(LAUNCHCORE_PERSIST_HEALTH_HEALTHY);
    printf("granule%u %s cuts%u complete/torn/GC/nonmutation/calibration: PASS\n",width,gc?"GC":"append",total+1);
}
int main(int argc,char **argv) {
    assert(launchcore_storage_init()==LAUNCHCORE_STORAGE_OK);backing=launchcore_storage_driver();
    if(argc>1) {
        reset(8);
        if(strcmp(argv[1],"old-virgin")==0) { uint64_t v;size_t n=sizeof v;(void)launchcore_persist_get(KEY,&v,&n);printf("legacy virgin get writes%u erases%u (strict target zero): RED\n",writes,erases);return writes+erases==0?0:1; }
        seed();uint64_t v=2;assert(launchcore_persist_set(KEY,&v,sizeof v)==LAUNCHCORE_PERSIST_OK);uint8_t bad=0;poke(last_record(KEY)+sizeof(record_header),&bad,1);v=99;size_t n=sizeof v;assert(launchcore_persist_get(KEY,&v,&n)==LAUNCHCORE_PERSIST_OK);printf("legacy corrupt-newest returned%llu (strict target refuse): RED\n",(unsigned long long)v);return v==2?0:1;
    }
    controls(8);controls(16);cuts(8,false);cuts(16,false);cuts(8,true);cuts(16,true);puts("strict actual journal: PASS");return 0;
}
