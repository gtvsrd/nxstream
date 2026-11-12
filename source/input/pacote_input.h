// NXStream - pacotes do canal "input" (fase 3)
//
// Formato (little-endian), igual ao do cliente de referência
// (xbox-xcloud-player, src/channel/input/packet.ts):
//   cabeçalho 14 bytes: u16 tipos, u32 sequência, f64 tempo em ms
//   [tipos & 1  Metadata] u8 n + n x 28 bytes (7 x u32: chave do quadro = timestamp
//                         RTP, chegada, enviado, decodificado, exibido, agora, agora)
//   [tipos & 2  Gamepad]  u8 n + n x 23 bytes (u8 índice, u16 botões, 4 x s16 eixos,
//                         2 x u16 gatilhos, u32 1 (LE), u32 1 (BE))
// Sem dependência do Switch: testado no PC (tests/).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Botões no formato do Xbox
enum {
    XB_NEXUS = 1 << 1, XB_MENU = 1 << 2, XB_VIEW = 1 << 3,
    XB_A = 1 << 4, XB_B = 1 << 5, XB_X = 1 << 6, XB_Y = 1 << 7,
    XB_CIMA = 1 << 8, XB_BAIXO = 1 << 9, XB_ESQUERDA = 1 << 10, XB_DIREITA = 1 << 11,
    XB_LB = 1 << 12, XB_RB = 1 << 13, XB_LS = 1 << 14, XB_RS = 1 << 15,
};

typedef struct {
    uint16_t botoes;
    int16_t  lx, ly, rx, ry;   // -32767..32767; y positivo = para cima
    uint16_t lt, rt;           // 0..65535
} EstadoControle;

typedef struct {
    uint32_t chave;            // timestamp RTP do quadro exibido
    uint32_t chegada_ms, enviado_ms, decodificado_ms, exibido_ms;
} MetadadoQuadro;

#define INPUT_MAX_METADADOS 8
#define INPUT_TAMANHO_MAX (14 + 1 + INPUT_MAX_METADADOS * 28 + 1 + 23)

// Monta um pacote com até INPUT_MAX_METADADOS metadados e (se `controle` != NULL)
// o estado do controle 0. Retorna o tamanho escrito em `saida`.
size_t pacote_input_montar(uint8_t *saida, uint32_t sequencia, double agora_ms,
                           const MetadadoQuadro *metadados, int num_metadados,
                           const EstadoControle *controle);
