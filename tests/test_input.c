// Testes do pacote do canal "input" (source/input/pacote_input.c), no PC
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "input/pacote_input.h"

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static unsigned long u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned long)p[3] << 24; }

int main(void) {
    uint8_t b[INPUT_TAMANHO_MAX];

    // Só o controle: 14 de cabeçalho + 1 (quantidade) + 23
    EstadoControle c = {XB_A | XB_MENU | XB_DIREITA, 1000, -32767, 32767, 0, 65535, 0};
    size_t n = pacote_input_montar(b, 7, 1234.5, NULL, 0, &c);
    assert(n == 38);
    assert(u16(b) == 2);              // tipo Gamepad
    assert(u32(b + 2) == 7);          // sequência
    double t;
    memcpy(&t, b + 6, 8);
    assert(t == 1234.5);              // tempo (f64 little-endian no PC)
    assert(b[14] == 1 && b[15] == 0); // 1 controle, índice 0
    assert(u16(b + 16) == (XB_A | XB_MENU | XB_DIREITA));
    assert((int16_t)u16(b + 18) == 1000);
    assert((int16_t)u16(b + 20) == -32767);
    assert((int16_t)u16(b + 22) == 32767);
    assert(u16(b + 26) == 65535 && u16(b + 28) == 0);
    assert(u32(b + 30) == 1);                                        // LE
    assert(b[34] == 0 && b[35] == 0 && b[36] == 0 && b[37] == 1);    // BE

    // Metadado + controle: 14 + 1 + 28 + 1 + 23
    MetadadoQuadro m = {0xA1B2C3D4u, 10, 11, 12, 13};
    n = pacote_input_montar(b, 8, 99.0, &m, 1, &c);
    assert(n == 67);
    assert(u16(b) == 3);
    assert(b[14] == 1 && u32(b + 15) == 0xA1B2C3D4u && u32(b + 19) == 10 && u32(b + 31) == 13);
    assert(u32(b + 35) == 99 && u32(b + 39) == 99);
    assert(b[43] == 1 && b[44] == 0);

    // Só metadados (menu aberto não manda controle? manda neutro; aqui o caso sem controle)
    n = pacote_input_montar(b, 9, 1.0, &m, 1, NULL);
    assert(n == 14 + 1 + 28 && u16(b) == 1);

    printf("test_input: ok\n");
    return 0;
}
