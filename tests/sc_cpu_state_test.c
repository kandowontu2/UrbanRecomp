#include "snes/interp816.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

Interp816 *ScCpuOracleInit(void *,Interp816ReadHandler,Interp816WriteHandler);
void ScCpuOracleFree(Interp816 *);
void ScCpuOracleReset(Interp816 *);
void ScCpuOracleSetFlags(Interp816 *,uint8_t);
uint8_t ScCpuOracleGetFlags(Interp816 *);
void ScCpuOracleSaveLoad(Interp816 *,SaveLoadInfo *);
void ScCpuOracleBrkHook(Interp816 *,bool);
typedef struct {unsigned reads,address[4],seed;} Bus;
static uint8_t read_bus(void *context,uint32_t address) {
    Bus *b=context;assert(b->reads<4);b->address[b->reads++]=address;
    return (uint8_t)(address*73+b->seed);
}
static void write_bus(void *context,uint32_t address,uint8_t value) {
    (void)context;(void)address;(void)value;assert(0);
}
static void equal(Interp816 *a,Interp816 *b) {
    Interp816 copy=*a;copy.mem=b->mem;assert(!memcmp(&copy,b,sizeof copy));
}
typedef struct {SaveLoadInfo info;uint8_t bytes[sizeof(Interp816)];size_t size;bool load;} Record;
static void record_bus(SaveLoadInfo *info,void *data,size_t size) {
    Record *r=(Record *)info;r->size=size;assert(size<=sizeof r->bytes);
    if(r->load) memcpy(data,r->bytes,size);else memcpy(r->bytes,data,size);
}
int main(int argc,char **argv) {
    (void)argv;
    if(argc>1) {
        Bus bus={0};Interp816 *cpu=interp816_init(&bus,read_bus,write_bus);
        cpu->k=0x7e;cpu->pc=0x100;interp816_runOpcode(cpu);assert(0);
    }
    unsigned states=0;
    for(unsigned seed=0;seed<256;++seed) {
        Bus a={.seed=seed},b={.seed=seed};
        Interp816 *actual=interp816_init(&a,read_bus,write_bus);
        Interp816 *oracle=ScCpuOracleInit(&b,read_bus,write_bus);assert(actual && oracle);equal(actual,oracle);
        actual->a=oracle->a=0xffff;actual->sp=oracle->sp=0xbeef;
        interp816_set_brk_hook_enabled(actual,false);ScCpuOracleBrkHook(oracle,false);
        interp816_reset(actual);ScCpuOracleReset(oracle);equal(actual,oracle);
        assert(!memcmp(&a,&b,sizeof a) && a.reads==2 && a.address[0]==0xfffc && a.address[1]==0xfffd);
        for(unsigned e=0;e<2;++e)for(unsigned flags=0;flags<256;++flags) {
            actual->e=oracle->e=e;actual->x=oracle->x=0xabcd;actual->y=oracle->y=0xef98;
            actual->sp=oracle->sp=0xfe37;
            interp816_setFlags(actual,(uint8_t)flags);ScCpuOracleSetFlags(oracle,(uint8_t)flags);
            equal(actual,oracle);assert(interp816_getFlags(actual)==ScCpuOracleGetFlags(oracle));++states;
        }
        Record ea={.info={record_bus}},eb={.info={record_bus}};
        interp816_saveload(actual,&ea.info);ScCpuOracleSaveLoad(oracle,&eb.info);
        assert(ea.size==offsetof(Interp816,cyclesUsed)-offsetof(Interp816,a) &&
            ea.size==eb.size && !memcmp(ea.bytes,eb.bytes,ea.size));
        actual->a=oracle->a=0;actual->sp=oracle->sp=0;
        actual->cyclesUsed=oracle->cyclesUsed=123;ea.load=eb.load=true;
        interp816_saveload(actual,&ea.info);ScCpuOracleSaveLoad(oracle,&eb.info);
        equal(actual,oracle);assert(actual->cyclesUsed==123);
        interp816_free(actual);ScCpuOracleFree(oracle);
    }
    printf("CPU state: 256 exact lifecycle/reset bus cases and %u exact flags/emulation/register cases; no decoder in native state module\n",states);
}
