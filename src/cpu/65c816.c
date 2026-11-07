#include "65c816.h"

void cpu_reset(cpu_t *cpu)
{
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->s = 0x01FF;
    cpu->d = 0;
    cpu->db = 0;
    cpu->pb = 0;
    cpu->pc = 0;      /* TODO: leer vector de reset en $00:FFFC */
    cpu->p = 0x34;    /* M=1, X=1, I=1 */
    cpu->e = 1;
}
