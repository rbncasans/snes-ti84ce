/* Sustituto mínimo de keypadc.h para probar en el PC. Constantes iguales
 * que en el CE C Toolchain. */
#ifndef KEYPADC_STUB_H
#define KEYPADC_STUB_H
#include <stdint.h>
extern uint8_t kb_Data[8];
static inline void kb_Scan(void) {}
/* Grupo 1 */
#define kb_Graph   (1 << 0)
#define kb_Trace   (1 << 1)
#define kb_Zoom    (1 << 2)
#define kb_Window  (1 << 3)
#define kb_Yequ    (1 << 4)
#define kb_2nd     (1 << 5)
#define kb_Mode    (1 << 6)
#define kb_Del     (1 << 7)
/* Grupo 2 */
#define kb_Alpha   (1 << 7)
/* Grupo 3 */
#define kb_GraphVar (1 << 7)
/* Grupo 6 */
#define kb_Enter   (1 << 0)
#define kb_Add     (1 << 1)
#define kb_Clear   (1 << 6)
/* Grupo 7 */
#define kb_Down    (1 << 0)
#define kb_Left    (1 << 1)
#define kb_Right   (1 << 2)
#define kb_Up      (1 << 3)
#endif
