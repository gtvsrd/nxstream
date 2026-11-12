// NXStream - conexão WebRTC com o console (fase 2b, parte 2)
//
// Monta a oferta com as mídias e canais que o console espera, aplica a
// resposta e os candidatos trocados pela API de sessão, e faz o handshake
// dos canais de dados. Especificação: docs/PROTOCOLO.md, seção 3.
//
// Os callbacks da libdatachannel rodam em threads dela: a tela lê uma cópia
// do estado com conexao_ler().
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../api/xapi.h"
#include "../media/h264_info.h"

typedef enum { CANAL_CHAT, CANAL_CONTROL, CANAL_INPUT, CANAL_MESSAGE, NUM_CANAIS } Canal;

typedef struct {
    double segundos;                 // desde conexao_criar()

    bool   oferta_pronta;
    bool   coleta_completa;
    XCandidatos locais;
    char   ufrag[64];

    int    estado;                   // rtcState da libdatachannel
    bool   conectado;
    double t_conectado;
    bool   falhou;                   // não conseguiu conectar (failed/closed)
    bool   caiu;                     // estava conectado e deixou de estar

    bool   canal_aberto[NUM_CANAIS];
    unsigned long msgs_recebidas[NUM_CANAIS];

    bool   handshake_ok;
    double t_handshake;

    unsigned long pacotes_audio;

    // Vídeo (fase 2c.1): quadros H.264 montados pela libdatachannel
    unsigned long quadros, quadros_chave, bytes_video;
    double t_primeiro_quadro, t_primeiro_chave, t_ultimo_quadro;
    unsigned long maior_quadro;          // bytes
    bool   sps_valido;
    H264Sps sps;                         // resolução, perfil e nível do último SPS
    unsigned long pedidos_quadro_chave;

    char   erro[160];
} ConexaoEstado;

bool  conexao_criar(void);
char *conexao_oferta(void);                        // cópia do SDP local; liberar com free()
bool  conexao_aplicar_resposta(const char *sdp);
int   conexao_adicionar_remotos(const XCandidatos *remotos); // quantos foram aceitos
void  conexao_ler(ConexaoEstado *copia);
void  conexao_encerrar(void);

// Pede ao console um quadro-chave (envia PLI). Use com moderação.
void  conexao_pedir_quadro_chave(void);
// Envia um pacote binário no canal "input" (controle e metadados dos quadros; fase 3)
void  conexao_enviar_input(const uint8_t *dados, size_t tamanho);

const char *conexao_nome_canal(Canal c);
const char *conexao_nome_estado(int estado);
