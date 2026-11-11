#include "h264_info.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Leitor de bits com exp-Golomb (H.264, seção 9.1)
// ---------------------------------------------------------------------------
typedef struct {
    const uint8_t *dados;
    size_t tamanho_bits, pos;
    bool erro;
} Bits;

static unsigned bit(Bits *b) {
    if (b->pos >= b->tamanho_bits) {
        b->erro = true;
        return 0;
    }
    unsigned v = (b->dados[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}

static unsigned bits(Bits *b, int n) {
    unsigned v = 0;
    while (n-- > 0)
        v = (v << 1) | bit(b);
    return v;
}

static unsigned ue(Bits *b) {
    int zeros = 0;
    while (!b->erro && bit(b) == 0)
        if (++zeros > 31) {
            b->erro = true;
            return 0;
        }
    return ((1u << zeros) - 1) + bits(b, zeros);
}

static int se(Bits *b) {
    unsigned k = ue(b);
    return (k & 1) ? (int)((k + 1) / 2) : -(int)(k / 2);
}

static void pular_scaling_list(Bits *b, int tamanho) {
    int ultimo = 8, proximo = 8;
    for (int j = 0; j < tamanho && !b->erro; j++) {
        if (proximo != 0)
            proximo = (ultimo + se(b) + 256) % 256;
        ultimo = proximo == 0 ? ultimo : proximo;
    }
}

// ---------------------------------------------------------------------------
// SPS (H.264, seção 7.3.2.1.1)
// ---------------------------------------------------------------------------
bool h264_ler_sps(const uint8_t *nal, size_t tamanho, H264Sps *sps) {
    if (tamanho < 4 || (nal[0] & 0x1F) != 7)
        return false;

    // Remove os bytes de proteção (00 00 03 -> 00 00); o SPS é curto
    uint8_t rbsp[256];
    size_t n = 0;
    int zeros = 0;
    for (size_t i = 1; i < tamanho && n < sizeof rbsp; i++) {
        if (zeros >= 2 && nal[i] == 0x03) {
            zeros = 0;
            continue;
        }
        zeros = nal[i] == 0 ? zeros + 1 : 0;
        rbsp[n++] = nal[i];
    }

    Bits b = {rbsp, n * 8, 0, false};
    int perfil = (int)bits(&b, 8);
    bits(&b, 8); // constraint flags + reservado
    int nivel = (int)bits(&b, 8);
    ue(&b);      // seq_parameter_set_id

    unsigned chroma = 1; // 4:2:0 quando não informado
    bool cor_separada = false;
    if (perfil == 100 || perfil == 110 || perfil == 122 || perfil == 244 || perfil == 44 ||
        perfil == 83 || perfil == 86 || perfil == 118 || perfil == 128 || perfil == 138 ||
        perfil == 139 || perfil == 134 || perfil == 135) {
        chroma = ue(&b);
        if (chroma == 3)
            cor_separada = bit(&b);
        ue(&b);      // bit_depth_luma_minus8
        ue(&b);      // bit_depth_chroma_minus8
        bit(&b);     // qpprime_y_zero_transform_bypass_flag
        if (bit(&b)) { // seq_scaling_matrix_present_flag
            int listas = chroma != 3 ? 8 : 12;
            for (int i = 0; i < listas && !b.erro; i++)
                if (bit(&b))
                    pular_scaling_list(&b, i < 6 ? 16 : 64);
        }
    }

    ue(&b); // log2_max_frame_num_minus4
    unsigned poc = ue(&b);
    if (poc == 0) {
        ue(&b); // log2_max_pic_order_cnt_lsb_minus4
    } else if (poc == 1) {
        bit(&b);
        se(&b);
        se(&b);
        unsigned ciclo = ue(&b);
        for (unsigned i = 0; i < ciclo && !b.erro; i++)
            se(&b);
    }
    ue(&b);  // max_num_ref_frames
    bit(&b); // gaps_in_frame_num_value_allowed_flag
    unsigned largura_mbs = ue(&b) + 1;
    unsigned altura_mapa = ue(&b) + 1;
    unsigned so_quadros = bit(&b); // frame_mbs_only_flag
    if (!so_quadros)
        bit(&b); // mb_adaptive_frame_field_flag
    bit(&b);     // direct_8x8_inference_flag

    unsigned corte_esq = 0, corte_dir = 0, corte_cima = 0, corte_baixo = 0;
    if (bit(&b)) { // frame_cropping_flag
        corte_esq = ue(&b);
        corte_dir = ue(&b);
        corte_cima = ue(&b);
        corte_baixo = ue(&b);
    }
    if (b.erro)
        return false;

    // Unidades do recorte (seção 7.4.2.1.1)
    unsigned unid_x = 1, unid_y = 2 - so_quadros;
    if (chroma != 0 && !cor_separada) {
        unid_x = (chroma == 1 || chroma == 2) ? 2 : 1;
        unid_y *= (chroma == 1) ? 2 : 1;
    }
    long largura = (long)largura_mbs * 16 - (long)unid_x * (corte_esq + corte_dir);
    long altura = (long)altura_mapa * 16 * (2 - so_quadros) - (long)unid_y * (corte_cima + corte_baixo);
    if (largura <= 0 || altura <= 0 || largura > 8192 || altura > 8192)
        return false;

    sps->largura = (int)largura;
    sps->altura = (int)altura;
    sps->perfil = perfil;
    sps->nivel = nivel;
    return true;
}

// ---------------------------------------------------------------------------
// Quadro: percorre as NALs separadas por 00 00 01 (ou 00 00 00 01)
// ---------------------------------------------------------------------------
static size_t proximo_inicio(const uint8_t *d, size_t tam, size_t de) {
    for (size_t i = de; i + 3 <= tam; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
            return i;
    return tam;
}

void h264_analisar_quadro(const uint8_t *d, size_t tam, H264Quadro *q) {
    memset(q, 0, sizeof *q);
    size_t i = proximo_inicio(d, tam, 0);
    while (i < tam) {
        size_t inicio_nal = i + 3;
        size_t fim = proximo_inicio(d, tam, inicio_nal);
        size_t tam_nal = fim - inicio_nal;
        // Zero do start code longo seguinte não pertence a esta NAL
        while (tam_nal > 0 && d[inicio_nal + tam_nal - 1] == 0 && fim < tam)
            tam_nal--;
        if (tam_nal > 0) {
            q->num_nals++;
            int tipo = d[inicio_nal] & 0x1F;
            if (tipo == 5)
                q->tem_idr = true;
            else if (tipo == 8)
                q->tem_pps = true;
            else if (tipo == 7) {
                q->tem_sps = true;
                if (!q->sps_valido)
                    q->sps_valido = h264_ler_sps(d + inicio_nal, tam_nal, &q->sps);
            }
        }
        i = fim;
    }
}

const char *h264_nome_perfil(int perfil) {
    switch (perfil) {
        case 66:  return "Baseline";
        case 77:  return "Main";
        case 88:  return "Extended";
        case 100: return "High";
        default:  return "?";
    }
}
