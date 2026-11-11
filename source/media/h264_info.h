// NXStream - leitura de informações de quadros H.264 (Annex B, com 00 00 01)
//
// Sem decodificar o vídeo: identifica os tipos de NAL de cada quadro e lê do
// SPS a resolução, o perfil e o nível. Sem dependências: testável no PC.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int largura, altura;      // já com o recorte (cropping) aplicado
    int perfil;               // profile_idc: 66 Baseline, 77 Main, 100 High
    int nivel;                // level_idc: 31 = 3.1, 42 = 4.2...
} H264Sps;

typedef struct {
    bool tem_idr;             // quadro-chave (NAL 5)
    bool tem_sps, tem_pps;    // NAL 7 e 8
    int  num_nals;
    bool sps_valido;          // sps preenchido (só se tem_sps e a leitura deu certo)
    H264Sps sps;
} H264Quadro;

// Analisa um quadro completo (uma ou mais NALs com start code)
void h264_analisar_quadro(const uint8_t *dados, size_t tamanho, H264Quadro *info);

// Lê um SPS (a partir do byte de cabeçalho da NAL, 0x67...). false se inválido.
bool h264_ler_sps(const uint8_t *nal, size_t tamanho, H264Sps *sps);

const char *h264_nome_perfil(int perfil);
