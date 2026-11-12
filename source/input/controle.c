// NXStream - controle do Switch -> controle do Xbox (ver controle.h)
#include "input/controle.h"

#include <string.h>

static int16_t eixo(s32 v) {
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    return (int16_t)v;
}

void controle_ler(PadState *pad, EstadoControle *s) {
    memset(s, 0, sizeof *s);
    u64 b = padGetButtons(pad);
    if (b & HidNpadButton_B) s->botoes |= XB_A;
    if (b & HidNpadButton_A) s->botoes |= XB_B;
    if (b & HidNpadButton_Y) s->botoes |= XB_X;
    if (b & HidNpadButton_X) s->botoes |= XB_Y;
    if (b & HidNpadButton_L) s->botoes |= XB_LB;
    if (b & HidNpadButton_R) s->botoes |= XB_RB;
    if (b & HidNpadButton_StickL) s->botoes |= XB_LS;
    if (b & HidNpadButton_StickR) s->botoes |= XB_RS;
    if (b & HidNpadButton_Plus) s->botoes |= XB_MENU;
    if (b & HidNpadButton_Minus) s->botoes |= XB_VIEW;
    if (b & HidNpadButton_Up) s->botoes |= XB_CIMA;
    if (b & HidNpadButton_Down) s->botoes |= XB_BAIXO;
    if (b & HidNpadButton_Left) s->botoes |= XB_ESQUERDA;
    if (b & HidNpadButton_Right) s->botoes |= XB_DIREITA;
    if (b & HidNpadButton_ZL) s->lt = 65535;
    if (b & HidNpadButton_ZR) s->rt = 65535;
    HidAnalogStickState l = padGetStickPos(pad, 0), r = padGetStickPos(pad, 1);
    // No Switch e no pacote do Xbox, y positivo é para cima
    s->lx = eixo(l.x);
    s->ly = eixo(l.y);
    s->rx = eixo(r.x);
    s->ry = eixo(r.y);
}
