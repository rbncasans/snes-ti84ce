/* Puente entre la CPU y run_sst.py (SingleStepTests). */
#include <stdint.h>
#include <string.h>
#include "cpu/65c816.h"
static uint8_t mem[1<<24];
static cpu_t cpu;
uint8_t bus_read(uint32_t a){return mem[a];}
void bus_write(uint32_t a,uint8_t v){mem[a]=v;}
void poke(uint32_t a,uint8_t v){mem[a]=v;}
uint8_t peek(uint32_t a){return mem[a];}
void set_state(int pc,int s,int p,int a,int x,int y,int dbr,int d,int pbr,int e){
 memset(&cpu,0,sizeof cpu);cpu.pc=pc;cpu.s=s;cpu.p=p;cpu.a=a;cpu.x=x;cpu.y=y;cpu.db=dbr;cpu.d=d;cpu.pb=pbr;cpu.e=e;
 if(e){cpu.s=0x100|(cpu.s&0xff);cpu.p|=0x30;}
 if(cpu.p&0x10){cpu.x&=0xff;cpu.y&=0xff;}}
void get_state(int*o){o[0]=cpu.pc;o[1]=cpu.s;o[2]=cpu.p;o[3]=cpu.a;o[4]=cpu.x;o[5]=cpu.y;o[6]=cpu.db;o[7]=cpu.d;o[8]=cpu.pb;o[9]=cpu.e;}
int step(void){return (int)cpu_step(&cpu);}
