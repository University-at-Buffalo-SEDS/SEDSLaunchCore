/* Boot repair of interrupted strict-journal writes. Reuses the physical
 * granule/1->0 fault driver; every write and erase step can be cut. */
#define main strict_reader_existing_main
#include "test_persist_strict.c"
#undef main

#define PEND UINT32_C(0x42525043) /* commissioning PENDING (Stop hold) marker stand-in */
static uint8_t staged[16384];
enum { STRICT_APPEND, STRICT_GC, STRICT_PROVISION, LEGACY_APPEND, LEGACY_GC, KINDS };
static const char *const kind_names[KINDS] = {"strict append", "strict GC", "strict provision", "legacy append", "legacy GC"};

static void small_reset(unsigned width) {
    reset(width);geometry.persistent_data_size=1024u;geometry.persistent_data_erase_size=512u;
}
static void stage(void){assert(backing->read(geometry.persistent_data_base,staged,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);}
static void unstage(void){assert(backing->erase(geometry.persistent_data_base,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);assert(backing->write(geometry.persistent_data_base,staged,geometry.persistent_data_size)==LAUNCHCORE_STORAGE_OK);counters();}
static void pend_value(uint8_t out[40]){memset(out,0xC3,40);out[0]=2;out[1]=1;}
static void seed_store(unsigned width,bool strict) {
    small_reset(width);launchcore_persist_strict_info_t info;uint64_t value=1;uint8_t cal[68],pend[40];
    memset(cal,0x5A,sizeof cal);pend_value(pend);
    if(strict) {
        assert(launchcore_persist_provision_virgin_strict(KEY,&value,sizeof value,&info)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_set_strict(CAL,cal,sizeof cal,&info)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_set_strict(PEND,pend,sizeof pend,&info)==LAUNCHCORE_PERSIST_OK);
    } else {
        assert(launchcore_persist_init()==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_set(KEY,&value,sizeof value)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_set(CAL,cal,sizeof cal)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_set(PEND,pend,sizeof pend)==LAUNCHCORE_PERSIST_OK);
    }
    counters();
}
static void fill_for_gc(bool strict) {
    uint32_t used=aligned(20)+aligned(32)+aligned(24+68)+aligned(24+40);uint64_t value=0;launchcore_persist_strict_info_t info;
    while(used+aligned(32)<=geometry.persistent_data_size/2u) {
        if(strict)assert(launchcore_persist_set_strict(77,&value,sizeof value,&info)==LAUNCHCORE_PERSIST_OK);
        else assert(launchcore_persist_set(77,&value,sizeof value)==LAUNCHCORE_PERSIST_OK);
        used+=aligned(32);
    }
    counters();
}
static void prepare_kind(unsigned width,unsigned kind) {
    if(kind==STRICT_PROVISION){small_reset(width);return;}
    seed_store(width,kind<=STRICT_GC);
    if(kind==STRICT_GC||kind==LEGACY_GC)fill_for_gc(kind==STRICT_GC);
}
static launchcore_persist_status_t operate(unsigned kind) {
    launchcore_persist_strict_info_t info;uint64_t value=2;
    switch(kind) {
    case STRICT_PROVISION: return launchcore_persist_provision_virgin_strict(KEY,&value,sizeof value,&info);
    case STRICT_APPEND: case STRICT_GC: return launchcore_persist_set_strict(KEY,&value,sizeof value,&info);
    default: return launchcore_persist_set(KEY,&value,sizeof value);
    }
}
static launchcore_persist_status_t repair(bool *changed) {
    launchcore_persist_strict_info_t info;return launchcore_persist_repair_interrupted_strict(&info,changed);
}
/* Committed data intact: old or new KEY (new when the write reported success),
 * calibration and the PENDING marker byte-exact. Provisioning may return to VIRGIN. */
static void require_intact(unsigned kind,launchcore_persist_status_t op) {
    launchcore_persist_strict_info_t info;unsigned w=writes,e=erases;
    assert(launchcore_persist_probe_strict(&info)==LAUNCHCORE_PERSIST_OK);assert(writes==w&&erases==e);
    uint64_t value=99;size_t n=sizeof value;launchcore_persist_status_t got=launchcore_persist_get_strict(KEY,&value,&n,&info);
    if(kind==STRICT_PROVISION) {
        if(info.health==LAUNCHCORE_PERSIST_HEALTH_VIRGIN){assert(got==LAUNCHCORE_PERSIST_NOT_FOUND&&op!=LAUNCHCORE_PERSIST_OK);return;}
        assert(got==LAUNCHCORE_PERSIST_OK&&value==2);return;
    }
    assert(info.health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY);
    assert(got==LAUNCHCORE_PERSIST_OK&&(value==1||value==2));if(op==LAUNCHCORE_PERSIST_OK)assert(value==2);
    uint8_t cal[68];n=sizeof cal;assert(launchcore_persist_get_strict(CAL,cal,&n,&info)==LAUNCHCORE_PERSIST_OK&&n==sizeof cal);
    for(unsigned j=0;j<sizeof cal;++j)assert(cal[j]==0x5A);
    uint8_t pend[40],expected[40];pend_value(expected);n=sizeof pend;
    assert(launchcore_persist_get_strict(PEND,pend,&n,&info)==LAUNCHCORE_PERSIST_OK&&n==sizeof pend&&!memcmp(pend,expected,sizeof pend));
}
static void require_idempotent(void) {
    bool changed=true;counters();assert(repair(&changed)==LAUNCHCORE_PERSIST_OK&&!changed);no_mutation(0,0,0);
}
static unsigned events_of(unsigned width,unsigned kind,launchcore_persist_status_t (*run)(unsigned)) {
    prepare_kind(width,kind);events=0;assert(run(kind)==LAUNCHCORE_PERSIST_OK);return events;
}

/* Every cut in every write kind, then every cut inside the repair of that
 * state, then every failed read inside that repair: the next repair always
 * restores a healthy store holding every committed record. */
static void interrupted(unsigned width,unsigned kind) {
    const unsigned total=events_of(width,kind,operate);
    unsigned corrupt=0,nested=0,reads=0;
    for(unsigned p=0;p<=total;++p) {
        prepare_kind(width,kind);capture();cut_after=p;
        launchcore_persist_status_t op=operate(kind);cut_after=UINT32_MAX;counters();
        launchcore_persist_strict_info_t info;bool was_corrupt=launchcore_persist_probe_strict(&info)!=LAUNCHCORE_PERSIST_OK;
        if(was_corrupt){++corrupt;assert(info.health==LAUNCHCORE_PERSIST_HEALTH_CORRUPT||info.health==LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS);}
        stage();bool changed=false;
        assert(repair(&changed)==LAUNCHCORE_PERSIST_OK);assert(changed==was_corrupt);assert(init_calls==0);
        require_intact(kind,op);require_idempotent();
        if(!was_corrupt)continue;
        unstage();events=0;read_calls=0;assert(repair(&changed)==LAUNCHCORE_PERSIST_OK);
        const unsigned repair_events=events,repair_reads=read_calls;
        for(unsigned q=0;q<repair_events;++q) {
            unstage();cut_after=q;(void)repair(&changed);cut_after=UINT32_MAX;counters();
            assert(repair(&changed)==LAUNCHCORE_PERSIST_OK);require_intact(kind,op);require_idempotent();++nested;
        }
        for(unsigned r=1;r<=repair_reads;++r) {
            unstage();fail_read=r;launchcore_persist_status_t status=repair(&changed);fail_read=UINT32_MAX;
            if(status==LAUNCHCORE_PERSIST_OK)require_intact(kind,op);
            else assert(status==LAUNCHCORE_PERSIST_STORAGE_ERROR||status==LAUNCHCORE_PERSIST_CORRUPT);
            counters();assert(repair(&changed)==LAUNCHCORE_PERSIST_OK);require_intact(kind,op);++reads;
        }
    }
    assert(corrupt>0);
    printf("repair granule%u %s: %u cuts (%u left corrupt) repaired, %u cuts inside repair, %u failed repair reads: PASS\n",
        width,kind_names[kind],total+1u,corrupt,nested,reads);
}

static void require_unchanged(launchcore_persist_health_t health,const char *what) {
    capture();uint8_t before[16384];memcpy(before,saved,geometry.persistent_data_size);
    counters();bool changed=true;launchcore_persist_strict_info_t info;
    launchcore_persist_status_t status=launchcore_persist_repair_interrupted_strict(&info,&changed);
    no_mutation(0,0,0);assert(!changed);assert(info.health==health);
    assert(status==((health==LAUNCHCORE_PERSIST_HEALTH_HEALTHY||health==LAUNCHCORE_PERSIST_HEALTH_VIRGIN)?LAUNCHCORE_PERSIST_OK:
        health==LAUNCHCORE_PERSIST_HEALTH_UNKNOWN?LAUNCHCORE_PERSIST_STORAGE_ERROR:LAUNCHCORE_PERSIST_CORRUPT));
    capture();assert(!memcmp(before,saved,geometry.persistent_data_size));
    printf("repair leaves %s unchanged: PASS\n",what);
}
static void fail_closed(unsigned width) {
    const uint32_t bank=geometry.persistent_data_size/2u;uint8_t copy[512];(void)bank;
    small_reset(width);require_unchanged(LAUNCHCORE_PERSIST_HEALTH_VIRGIN,"virgin");
    seed_store(width,true);require_unchanged(LAUNCHCORE_PERSIST_HEALTH_HEALTHY,"healthy");
    /* A corrupt committed record followed by later records: not an interrupted append. */
    seed_store(width,true);uint8_t bad=0;poke(last_record(KEY)+sizeof(record_header),&bad,1);
    require_unchanged(LAUNCHCORE_PERSIST_HEALTH_CORRUPT,"corrupt record mid-bank");
    /* A torn-looking record with programmed bytes beyond one maximal record. */
    seed_store(width,true);uint32_t end=last_record(PEND)+aligned(sizeof(record_header)+40u);
    uint32_t junk=0x12345678u;poke(end,&junk,sizeof junk);
    poke(geometry.persistent_data_base+geometry.persistent_data_size/2u-16u,&junk,sizeof junk);
    require_unchanged(LAUNCHCORE_PERSIST_HEALTH_CORRUPT,"corrupt tail followed by data");
    /* A committed bank whose header was damaged after its commit. */
    seed_store(width,true);uint32_t zero=0;poke(geometry.persistent_data_base,&zero,sizeof zero);
    require_unchanged(LAUNCHCORE_PERSIST_HEALTH_CORRUPT,"damaged header of the only committed bank");
    /* Equal and gapped generations. */
    seed_store(width,true);uint32_t half=geometry.persistent_data_size/2u;
    assert(backing->read(geometry.persistent_data_base,copy,half)==LAUNCHCORE_STORAGE_OK);
    assert(backing->write(geometry.persistent_data_base+half,copy,half)==LAUNCHCORE_STORAGE_OK);
    require_unchanged(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS,"equal generations");
    bank_generation(1,3);require_unchanged(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS,"generation gap");
    /* Exhausted generation and sequence. */
    seed_store(width,true);bank_generation(0,UINT32_MAX);require_unchanged(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS,"exhausted generation");
    seed_store(width,true);record_header rh;uint32_t at=last_record(CAL);
    assert(backing->read(at,&rh,sizeof rh)==LAUNCHCORE_STORAGE_OK);rh.sequence=UINT32_MAX;
    rh.header_crc=launchcore_crc32(&rh,offsetof(record_header,header_crc));poke(at,&rh,sizeof rh);
    require_unchanged(LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS,"exhausted sequence");
    /* Every read failing before any write: IO is never mistaken for a repairable state. */
    uint64_t value=2;launchcore_persist_strict_info_t info;
    for(unsigned p=0;;++p) {
        seed_store(width,true);cut_after=p;(void)launchcore_persist_set_strict(KEY,&value,sizeof value,&info);counters();
        if(launchcore_persist_probe_strict(&info)==LAUNCHCORE_PERSIST_CORRUPT)break;
        assert(p<64);
    }
    capture();read_calls=0;assert(repair(NULL)==LAUNCHCORE_PERSIST_OK);unsigned total_reads=read_calls;
    for(unsigned r=1;r<=total_reads;++r) {
        restore();unsigned mutations;fail_read=r;launchcore_persist_status_t status=repair(NULL);fail_read=UINT32_MAX;
        mutations=writes+erases;
        if(mutations==0u)assert(status==LAUNCHCORE_PERSIST_STORAGE_ERROR);
    }
    restore();fail_read=1;unsigned w=writes;assert(repair(NULL)==LAUNCHCORE_PERSIST_STORAGE_ERROR);assert(writes==w&&erases==0);
    printf("repair granule%u IO faults on %u reads never write before reading the state: PASS\n",width,total_reads);
    launchcore_storage_driver_t readonly=fake;readonly.erase=NULL;launchcore_storage_set_driver(&readonly);
    assert(launchcore_persist_repair_interrupted_strict(&info,NULL)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT);
    launchcore_storage_set_driver(&fake);assert(launchcore_persist_repair_interrupted_strict(NULL,NULL)==LAUNCHCORE_PERSIST_INVALID_ARGUMENT);
}
/* Bench FC, 2026-11-10: bank 0 a nearly full healthy journal of underglow
 * toggles, bank 1 still holding a LaunchCore metadata record ("LCMD") from an
 * older flash layout that put a metadata copy there. Strict health is CORRUPT
 * and every strict user refused; bank 1 was never a journal bank. */
static void bench_foreign_bank(unsigned width) {
    seed_store(width,true);launchcore_persist_strict_info_t info;uint8_t glow=0;
    const uint32_t half=geometry.persistent_data_size/2u;
    /* Toggle until the next toggle would compact: keep the store just before it. */
    for(;;) {
        capture();glow^=1u;assert(launchcore_persist_set_strict(UINT32_C(0x55474C57),&glow,1,&info)==LAUNCHCORE_PERSIST_OK);
        assert(launchcore_persist_probe_strict(&info)==LAUNCHCORE_PERSIST_OK);
        if(info.bank_generation!=1u){restore();glow^=1u;break;}
    }
    uint8_t metadata[96];memset(metadata,0x00,sizeof metadata);
    const uint32_t magic=UINT32_C(0x4C434D44);const uint16_t version=1,size=sizeof metadata;const uint32_t seq=7;
    memcpy(metadata,&magic,4);memcpy(metadata+4,&version,2);memcpy(metadata+6,&size,2);memcpy(metadata+8,&seq,4);
    assert(backing->write(geometry.persistent_data_base+half,metadata,sizeof metadata)==LAUNCHCORE_STORAGE_OK);
    counters();assert(launchcore_persist_probe_strict(&info)==LAUNCHCORE_PERSIST_CORRUPT);
    bool changed=false;assert(repair(&changed)==LAUNCHCORE_PERSIST_OK&&changed);
    uint8_t got=0xEE;size_t n=1;assert(launchcore_persist_get_strict(UINT32_C(0x55474C57),&got,&n,&info)==LAUNCHCORE_PERSIST_OK&&got==glow);
    require_intact(STRICT_APPEND,LAUNCHCORE_PERSIST_CORRUPT);require_idempotent();
    /* Thousands of toggles afterwards compact instead of exhausting the store. */
    for(unsigned i=0;i<3000;++i){glow^=1u;assert(launchcore_persist_set_strict(UINT32_C(0x55474C57),&glow,1,&info)==LAUNCHCORE_PERSIST_OK);}
    n=1;assert(launchcore_persist_get_strict(UINT32_C(0x55474C57),&got,&n,&info)==LAUNCHCORE_PERSIST_OK&&got==glow);
    require_intact(STRICT_APPEND,LAUNCHCORE_PERSIST_CORRUPT);
    printf("repair granule%u bench state (full bank 0, foreign metadata in bank 1) then 3000 toggles: PASS\n",width);
}

int main(void) {
    assert(launchcore_storage_init()==LAUNCHCORE_STORAGE_OK);backing=launchcore_storage_driver();
    for(unsigned width=8;width<=16;width+=8) {
        for(unsigned kind=0;kind<KINDS;++kind)interrupted(width,kind);
        fail_closed(width);
        bench_foreign_bank(width);
    }
    puts("strict interrupted-write repair: PASS");return 0;
}
