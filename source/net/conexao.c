#include "conexao.h"
#include "rtc_comum.h"
#include "../core/log.h"
#include "cJSON.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtc/rtc.h>
#include <switch.h>

// ---------------------------------------------------------------------------
// Estado compartilhado com as threads da libdatachannel
// ---------------------------------------------------------------------------
static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;
static ConexaoEstado g_estado;
static char *g_oferta = NULL;
static u64 g_inicio;

static int g_pc = -1;
static int g_canais[NUM_CANAIS] = {-1, -1, -1, -1};
static int g_trilha_video = -1, g_trilha_audio = -1;

// Identificadores passados como "ponteiro de usuário" nos callbacks
#define ID_TRILHA_VIDEO 100
#define ID_TRILHA_AUDIO 101

static const struct { const char *nome, *protocolo; } CANAIS[NUM_CANAIS] = {
    [CANAL_CHAT]    = {"chat", "chatV1"},
    [CANAL_CONTROL] = {"control", "controlV1"},
    [CANAL_INPUT]   = {"input", "1.0"},
    [CANAL_MESSAGE] = {"message", "messageV1"},
};

// Mídias da oferta (docs/PROTOCOLO.md 3.1). O mid 0 é o áudio, 1 o vídeo; a
// libdatachannel dá ao canal de dados o próximo número livre (2).
static const char *MIDIA_AUDIO =
    "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=mid:0\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=fmtp:111 minptime=10;useinbandfec=1;stereo=1\r\n"
    "a=rtcp-fb:111 transport-cc\r\n";

static const char *MIDIA_VIDEO =
    "m=video 9 UDP/TLS/RTP/SAVPF 102 104\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=mid:1\r\n"
    "a=recvonly\r\n"
    "a=rtpmap:102 H264/90000\r\n"
    "a=rtcp-fb:102 goog-remb\r\n"
    "a=rtcp-fb:102 transport-cc\r\n"
    "a=rtcp-fb:102 ccm fir\r\n"
    "a=rtcp-fb:102 nack\r\n"
    "a=rtcp-fb:102 nack pli\r\n"
    "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f\r\n"
    "a=rtpmap:104 H264/90000\r\n"
    "a=rtcp-fb:104 goog-remb\r\n"
    "a=rtcp-fb:104 transport-cc\r\n"
    "a=rtcp-fb:104 ccm fir\r\n"
    "a=rtcp-fb:104 nack\r\n"
    "a=rtcp-fb:104 nack pli\r\n"
    "a=fmtp:104 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n";

static double agora_s(void) {
    return armTicksToNs(armGetSystemTick() - g_inicio) / 1e9;
}

const char *conexao_nome_canal(Canal c) {
    return (c >= 0 && c < NUM_CANAIS) ? CANAIS[c].nome : "?";
}

const char *conexao_nome_estado(int e) {
    switch (e) {
        case RTC_NEW:          return "nova";
        case RTC_CONNECTING:   return "conectando";
        case RTC_CONNECTED:    return "CONECTADO";
        case RTC_DISCONNECTED: return "desconectado";
        case RTC_FAILED:       return "falhou";
        case RTC_CLOSED:       return "fechada";
        default:               return "?";
    }
}

static void definir_erro(const char *texto, int codigo) {
    pthread_mutex_lock(&g_trava);
    snprintf(g_estado.erro, sizeof g_estado.erro, "%s (codigo %d)", texto, codigo);
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: %s (codigo %d)", texto, codigo);
}

// ---------------------------------------------------------------------------
// Envio nos canais (texto vai como binário UTF-8, como o cliente oficial)
// ---------------------------------------------------------------------------
static void enviar(Canal c, const void *dados, int tamanho) {
    int dc = g_canais[c];
    if (dc < 0)
        return;
    int rc = rtcSendMessage(dc, (const char *)dados, tamanho);
    if (rc < 0)
        LOG("conexao: falha ao enviar no canal %s (%d)", CANAIS[c].nome, rc);
}

static void enviar_texto(Canal c, const char *texto) {
    LOG("conexao: -> %s: %.200s", CANAIS[c].nome, texto);
    enviar(c, texto, (int)strlen(texto));
}

static void gerar_uuid(char saida[37]) {
    uint8_t b[16];
    randomGet(b, sizeof b);
    b[6] = (b[6] & 0x0F) | 0x40; // versão 4
    b[8] = (b[8] & 0x3F) | 0x80; // variante RFC 4122
    snprintf(saida, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12],
             b[13], b[14], b[15]);
}

// {"type":"Message","content":"<JSON>","id":"<uuid>","target":"<caminho>","cv":""}
static void enviar_mensagem(const char *alvo, cJSON *conteudo) {
    char *texto_conteudo = cJSON_PrintUnformatted(conteudo);
    cJSON_Delete(conteudo);
    char id[37];
    gerar_uuid(id);
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "Message");
    cJSON_AddStringToObject(msg, "content", texto_conteudo ? texto_conteudo : "{}");
    cJSON_AddStringToObject(msg, "id", id);
    cJSON_AddStringToObject(msg, "target", alvo);
    cJSON_AddStringToObject(msg, "cv", "");
    char *texto = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    free(texto_conteudo);
    if (texto)
        enviar_texto(CANAL_MESSAGE, texto);
    free(texto);
}

static void enviar_handshake(void) {
    char id[37];
    gerar_uuid(id);
    char texto[160];
    snprintf(texto, sizeof texto,
             "{\"type\":\"Handshake\",\"version\":\"messageV1\",\"id\":\"%s\",\"cv\":\"0\"}", id);
    enviar_texto(CANAL_MESSAGE, texto);
}

// Depois do HandshakeAck (docs/PROTOCOLO.md 3.4)
static void apos_handshake(void) {
    // 1. control: autorização e gamepad (o cliente oficial envia "adicionado" e "removido")
    enviar_texto(CANAL_CONTROL,
                 "{\"message\":\"authorizationRequest\",\"accessKey\":\"4BDB3609-C1F1-4195-9B37-FEFF45DA8B8E\"}");
    enviar_texto(CANAL_CONTROL, "{\"message\":\"gamepadChanged\",\"gamepadIndex\":0,\"wasAdded\":true}");
    enviar_texto(CANAL_CONTROL, "{\"message\":\"gamepadChanged\",\"gamepadIndex\":0,\"wasAdded\":false}");

    // 2. input: metadados do cliente (15 bytes, little-endian)
    uint8_t pacote[15];
    uint16_t tipo = 8;          // ClientMetadata
    uint32_t sequencia = 0;
    double tempo_ms = armTicksToNs(armGetSystemTick()) / 1e6;
    memcpy(pacote + 0, &tipo, 2);
    memcpy(pacote + 2, &sequencia, 4);
    memcpy(pacote + 6, &tempo_ms, 8);
    pacote[14] = 1;             // máximo de toques
    LOG("conexao: -> input: metadados do cliente (15 bytes)");
    enviar(CANAL_INPUT, pacote, sizeof pacote);

    // 3. message: configurações
    cJSON *c = cJSON_CreateObject();
    cJSON *versao = cJSON_AddArrayToObject(c, "version");
    cJSON_AddItemToArray(versao, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(versao, cJSON_CreateNumber(2));
    cJSON_AddItemToArray(versao, cJSON_CreateNumber(0));
    cJSON_AddArrayToObject(c, "systemUis");
    enviar_mensagem("/streaming/systemUi/configuration", c);

    char id_instalacao[37];
    gerar_uuid(id_instalacao);
    c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "clientAppInstallId", id_instalacao);
    enviar_mensagem("/streaming/properties/clientappinstallidchanged", c);

    c = cJSON_CreateObject();
    cJSON_AddNumberToObject(c, "orientation", 0);
    enviar_mensagem("/streaming/characteristics/orientationchanged", c);

    c = cJSON_CreateObject();
    cJSON_AddFalseToObject(c, "touchInputEnabled");
    enviar_mensagem("/streaming/characteristics/touchinputenabledchanged", c);

    enviar_mensagem("/streaming/characteristics/clientdevicecapabilities", cJSON_CreateObject());

    c = cJSON_CreateObject(); // 720p (docs/PLANO.md)
    cJSON_AddNumberToObject(c, "horizontal", 1280);
    cJSON_AddNumberToObject(c, "vertical", 720);
    cJSON_AddNumberToObject(c, "preferredWidth", 1280);
    cJSON_AddNumberToObject(c, "preferredHeight", 720);
    cJSON_AddNumberToObject(c, "safeAreaLeft", 0);
    cJSON_AddNumberToObject(c, "safeAreaTop", 0);
    cJSON_AddNumberToObject(c, "safeAreaRight", 1280);
    cJSON_AddNumberToObject(c, "safeAreaBottom", 720);
    cJSON_AddTrueToObject(c, "supportsCustomResolution");
    enviar_mensagem("/streaming/characteristics/dimensionschanged", c);
}

// ---------------------------------------------------------------------------
// Callbacks da libdatachannel
// ---------------------------------------------------------------------------
static void ao_descricao(int pc, const char *sdp, const char *tipo, void *ptr) {
    (void)pc; (void)ptr;
    char ufrag[64] = "";
    const char *u = strstr(sdp, "a=ice-ufrag:");
    if (u) {
        u += strlen("a=ice-ufrag:");
        size_t n = strcspn(u, "\r\n");
        if (n >= sizeof ufrag)
            n = sizeof ufrag - 1;
        memcpy(ufrag, u, n);
        ufrag[n] = '\0';
    }
    pthread_mutex_lock(&g_trava);
    free(g_oferta);
    g_oferta = strdup(sdp);
    snprintf(g_estado.ufrag, sizeof g_estado.ufrag, "%s", ufrag);
    g_estado.oferta_pronta = true;
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: descricao local (%s) pronta, %d bytes:\n%s", tipo, (int)strlen(sdp), sdp);
}

static void ao_candidato(int pc, const char *cand, const char *mid, void *ptr) {
    (void)pc; (void)ptr;
    LOG("conexao: candidato local [%s]: %s", mid ? mid : "-", cand);
    pthread_mutex_lock(&g_trava);
    XCandidatos *l = &g_estado.locais;
    if (l->total < XAPI_MAX_CAND) {
        snprintf(l->cand[l->total], sizeof l->cand[0], "%s", cand);
        snprintf(l->mid[l->total], sizeof l->mid[0], "%s", mid ? mid : "0");
        l->total++;
    }
    pthread_mutex_unlock(&g_trava);
}

static void ao_estado_coleta(int pc, rtcGatheringState estado, void *ptr) {
    (void)pc; (void)ptr;
    if (estado != RTC_GATHERING_COMPLETE)
        return;
    pthread_mutex_lock(&g_trava);
    g_estado.coleta_completa = true;
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: coleta de candidatos completa");
}

static void ao_estado(int pc, rtcState estado, void *ptr) {
    (void)pc; (void)ptr;
    pthread_mutex_lock(&g_trava);
    g_estado.estado = estado;
    if (estado == RTC_CONNECTED && !g_estado.conectado) {
        g_estado.conectado = true;
        g_estado.t_conectado = agora_s();
    }
    if (estado == RTC_FAILED || estado == RTC_CLOSED)
        g_estado.falhou = true;
    if (g_estado.conectado && estado != RTC_CONNECTED)
        g_estado.caiu = true;
    double t = agora_s();
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: estado = %s (%.2f s)", conexao_nome_estado(estado), t);
}

static void ao_canal_aberto(int id, void *ptr) {
    (void)id;
    Canal c = (Canal)(intptr_t)ptr;
    if (c < 0 || c >= NUM_CANAIS)
        return;
    pthread_mutex_lock(&g_trava);
    g_estado.canal_aberto[c] = true;
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: canal %s aberto", CANAIS[c].nome);
    if (c == CANAL_MESSAGE)
        enviar_handshake();
}

static void ao_canal_fechado(int id, void *ptr) {
    (void)id;
    Canal c = (Canal)(intptr_t)ptr;
    if (c < 0 || c >= NUM_CANAIS)
        return;
    pthread_mutex_lock(&g_trava);
    g_estado.canal_aberto[c] = false;
    pthread_mutex_unlock(&g_trava);
    LOG("conexao: canal %s fechado", CANAIS[c].nome);
}

static void tratar_message(const char *dados, int tamanho) {
    cJSON *raiz = cJSON_ParseWithLength(dados, (size_t)tamanho);
    const cJSON *tipo = cJSON_GetObjectItemCaseSensitive(raiz, "type");
    const cJSON *alvo = cJSON_GetObjectItemCaseSensitive(raiz, "target");
    const char *t = cJSON_IsString(tipo) ? tipo->valuestring : "?";
    LOG("conexao: <- message: %s %s", t, cJSON_IsString(alvo) ? alvo->valuestring : "");
    if (!strcmp(t, "HandshakeAck")) {
        pthread_mutex_lock(&g_trava);
        bool primeira = !g_estado.handshake_ok;
        g_estado.handshake_ok = true;
        g_estado.t_handshake = agora_s();
        pthread_mutex_unlock(&g_trava);
        if (primeira)
            apos_handshake();
    }
    cJSON_Delete(raiz);
}

static void ao_mensagem(int id, const char *dados, int tamanho, void *ptr) {
    (void)id;
    intptr_t origem = (intptr_t)ptr;
    // tamanho < 0: texto terminado em '\0'; >= 0: binário
    int n = tamanho < 0 ? (int)strlen(dados) : tamanho;

    if (origem == ID_TRILHA_VIDEO || origem == ID_TRILHA_AUDIO) {
        pthread_mutex_lock(&g_trava);
        if (origem == ID_TRILHA_VIDEO) {
            if (g_estado.pacotes_video == 0)
                g_estado.t_primeiro_video = agora_s();
            g_estado.pacotes_video++;
            g_estado.bytes_video += (unsigned long)n;
        } else {
            g_estado.pacotes_audio++;
        }
        unsigned long pv = g_estado.pacotes_video;
        pthread_mutex_unlock(&g_trava);
        if (origem == ID_TRILHA_VIDEO && pv == 1)
            LOG("conexao: primeiro pacote de video recebido (%d bytes)", n);
        return;
    }

    Canal c = (Canal)origem;
    if (c < 0 || c >= NUM_CANAIS)
        return;
    pthread_mutex_lock(&g_trava);
    unsigned long contagem = ++g_estado.msgs_recebidas[c];
    pthread_mutex_unlock(&g_trava);

    if (c == CANAL_MESSAGE) {
        tratar_message(dados, n);
    } else if (contagem <= 10) {
        // Primeiras mensagens dos outros canais vão para o log (estudo do protocolo)
        if (c == CANAL_CONTROL)
            LOG("conexao: <- control: %.*s", n > 200 ? 200 : n, dados);
        else {
            char hex[3 * 24 + 1] = "";
            for (int i = 0; i < n && i < 24; i++)
                snprintf(hex + 3 * i, 4, "%02x ", (unsigned char)dados[i]);
            LOG("conexao: <- %s: %d bytes: %s", CANAIS[c].nome, n, hex);
        }
    }
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
bool conexao_criar(void) {
    conexao_encerrar();
    pthread_mutex_lock(&g_trava);
    memset(&g_estado, 0, sizeof g_estado);
    pthread_mutex_unlock(&g_trava);
    g_inicio = armGetSystemTick();
    rtc_comum_iniciar();

    rtcConfiguration cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.disableAutoNegotiation = true; // a oferta só sai depois de trilhas e canais

    g_pc = rtcCreatePeerConnection(&cfg);
    if (g_pc < 0) {
        definir_erro("Falha ao criar a conexao WebRTC", g_pc);
        return false;
    }
    rtcSetLocalDescriptionCallback(g_pc, ao_descricao);
    rtcSetLocalCandidateCallback(g_pc, ao_candidato);
    rtcSetGatheringStateChangeCallback(g_pc, ao_estado_coleta);
    rtcSetStateChangeCallback(g_pc, ao_estado);

    g_trilha_audio = rtcAddTrack(g_pc, MIDIA_AUDIO);
    g_trilha_video = rtcAddTrack(g_pc, MIDIA_VIDEO);
    if (g_trilha_audio < 0 || g_trilha_video < 0) {
        definir_erro("Falha ao adicionar as trilhas de audio/video",
                     g_trilha_audio < 0 ? g_trilha_audio : g_trilha_video);
        return false;
    }
    rtcSetUserPointer(g_trilha_video, (void *)(intptr_t)ID_TRILHA_VIDEO);
    rtcSetUserPointer(g_trilha_audio, (void *)(intptr_t)ID_TRILHA_AUDIO);
    rtcSetMessageCallback(g_trilha_video, ao_mensagem);
    rtcSetMessageCallback(g_trilha_audio, ao_mensagem);

    for (int c = 0; c < NUM_CANAIS; c++) {
        rtcDataChannelInit init;
        memset(&init, 0, sizeof init); // ordenado e confiável
        init.protocol = CANAIS[c].protocolo;
        g_canais[c] = rtcCreateDataChannelEx(g_pc, CANAIS[c].nome, &init);
        if (g_canais[c] < 0) {
            definir_erro("Falha ao criar canal de dados", g_canais[c]);
            return false;
        }
        rtcSetUserPointer(g_canais[c], (void *)(intptr_t)c);
        rtcSetOpenCallback(g_canais[c], ao_canal_aberto);
        rtcSetClosedCallback(g_canais[c], ao_canal_fechado);
        rtcSetMessageCallback(g_canais[c], ao_mensagem);
    }

    int rc = rtcSetLocalDescription(g_pc, "offer");
    if (rc < 0) {
        definir_erro("Falha ao gerar a oferta SDP", rc);
        return false;
    }
    LOG("conexao: criada (oferta sendo gerada)");
    return true;
}

char *conexao_oferta(void) {
    pthread_mutex_lock(&g_trava);
    char *copia = g_oferta ? strdup(g_oferta) : NULL;
    pthread_mutex_unlock(&g_trava);
    return copia;
}

bool conexao_aplicar_resposta(const char *sdp) {
    int rc = rtcSetRemoteDescription(g_pc, sdp, "answer");
    if (rc < 0) {
        definir_erro("A resposta do console nao foi aceita", rc);
        return false;
    }
    LOG("conexao: resposta do console aplicada");
    return true;
}

int conexao_adicionar_remotos(const XCandidatos *r) {
    int aceitos = 0;
    for (int i = 0; i < r->total; i++) {
        int rc = rtcAddRemoteCandidate(g_pc, r->cand[i], r->mid[i]);
        if (rc >= 0)
            aceitos++;
        else
            LOG("conexao: candidato remoto recusado (%d): %s", rc, r->cand[i]);
    }
    LOG("conexao: %d de %d candidatos do console aceitos", aceitos, r->total);
    return aceitos;
}

void conexao_ler(ConexaoEstado *copia) {
    pthread_mutex_lock(&g_trava);
    *copia = g_estado;
    pthread_mutex_unlock(&g_trava);
    copia->segundos = agora_s();
}

void conexao_encerrar(void) {
    // Canais e trilhas primeiro: cada um é um identificador próprio na biblioteca
    for (int c = 0; c < NUM_CANAIS; c++) {
        int dc = g_canais[c];
        g_canais[c] = -1; // enviar() para de usar antes de apagar
        if (dc >= 0)
            rtcDeleteDataChannel(dc);
    }
    if (g_trilha_video >= 0)
        rtcDeleteTrack(g_trilha_video);
    if (g_trilha_audio >= 0)
        rtcDeleteTrack(g_trilha_audio);
    g_trilha_video = g_trilha_audio = -1;
    if (g_pc >= 0) {
        rtcDeletePeerConnection(g_pc);
        LOG("conexao: encerrada");
        g_pc = -1;
    }
    pthread_mutex_lock(&g_trava);
    free(g_oferta);
    g_oferta = NULL;
    pthread_mutex_unlock(&g_trava);
}
