// NXStream - pacotes do canal "input" (ver pacote_input.h)
#include "input/pacote_input.h"

#include <string.h>

static uint8_t *u8(uint8_t *p, uint8_t v) { *p = v; return p + 1; }
static uint8_t *u16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t *u32le(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
    return p + 4;
}
static uint8_t *u32be(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * (3 - i)));
    return p + 4;
}
static uint8_t *f64le(uint8_t *p, double v) {
    uint64_t b;
    memcpy(&b, &v, sizeof b);
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(b >> (8 * i));
    return p + 8;
}

size_t pacote_input_montar(uint8_t *saida, uint32_t sequencia, double agora_ms,
                           const MetadadoQuadro *metadados, int num_metadados,
                           const EstadoControle *controle) {
    if (num_metadados < 0) num_metadados = 0;
    if (num_metadados > INPUT_MAX_METADADOS) num_metadados = INPUT_MAX_METADADOS;
    uint16_t tipos = (uint16_t)((num_metadados ? 1 : 0) | (controle ? 2 : 0));
    uint8_t *p = saida;
    p = u16le(p, tipos);
    p = u32le(p, sequencia);
    p = f64le(p, agora_ms);
    uint32_t agora32 = (uint32_t)agora_ms;
    if (num_metadados) {
        p = u8(p, (uint8_t)num_metadados);
        for (int i = 0; i < num_metadados; i++) {
            const MetadadoQuadro *m = &metadados[i];
            p = u32le(p, m->chave);
            p = u32le(p, m->chegada_ms);
            p = u32le(p, m->enviado_ms);
            p = u32le(p, m->decodificado_ms);
            p = u32le(p, m->exibido_ms);
            p = u32le(p, agora32);
            p = u32le(p, agora32);
        }
    }
    if (controle) {
        p = u8(p, 1);  // um controle
        p = u8(p, 0);  // índice 0
        p = u16le(p, controle->botoes);
        p = u16le(p, (uint16_t)controle->lx);
        p = u16le(p, (uint16_t)controle->ly);
        p = u16le(p, (uint16_t)controle->rx);
        p = u16le(p, (uint16_t)controle->ry);
        p = u16le(p, controle->lt);
        p = u16le(p, controle->rt);
        p = u32le(p, 1); // PhysicalPhysicality
        p = u32be(p, 1); // VirtualPhysicality (o cliente de referência grava em big-endian)
    }
    return (size_t)(p - saida);
}
