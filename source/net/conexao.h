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
#include "../api/xapi.h"

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

    unsigned long pacotes_video, bytes_video;
    unsigned long pacotes_audio;
    double t_primeiro_video;

    char   erro[160];
} ConexaoEstado;

bool  conexao_criar(void);
char *conexao_oferta(void);                        // cópia do SDP local; liberar com free()
bool  conexao_aplicar_resposta(const char *sdp);
int   conexao_adicionar_remotos(const XCandidatos *remotos); // quantos foram aceitos
void  conexao_ler(ConexaoEstado *copia);
void  conexao_encerrar(void);

const char *conexao_nome_canal(Canal c);
const char *conexao_nome_estado(int estado);
