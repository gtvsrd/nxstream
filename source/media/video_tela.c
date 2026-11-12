// NXStream - desenho do vídeo na tela pela GPU (ver video_tela.h)
//
// Referência para ler os quadros do NVDEC direto na GPU: o renderizador deko3d
// do Moonlight-Switch (DKVideoRenderer.cpp), que faz o mesmo mapeamento.
#include "media/video_tela.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <deko3d.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_nvtegra.h>
#include <libavutil/pixfmt.h>

#include "core/log.h"
#include "ui/sobreposicao.h"

// Shaders compilados pelo uam no build (shaders/*.glsl -> .dksh embutidos)
#include "tela_vsh_dksh.h"
#include "video_fsh_dksh.h"
#include "sobre_fsh_dksh.h"

#define TELA_L 1280
#define TELA_A 720
#define NUM_IMAGENS_TELA 2
#define MAX_MAPEAMENTOS 16 // quadros diferentes do NVDEC já vistos (o FFmpeg reaproveita os mesmos)

#define TAM_CODIGO   (64 * 1024)
#define TAM_COMANDOS (64 * 1024)
#define TAM_DADOS    (128 * 1024)

// Dentro do bloco de dados
#define OFS_COMANDOS   0
#define OFS_DESC_IMG   TAM_COMANDOS                    // 3 descritores de imagem (luma, croma, texto)
#define OFS_DESC_AMOST (OFS_DESC_IMG + 0x100)          // 2 descritores de amostragem
#define OFS_UNIFORME   (OFS_DESC_IMG + 0x200)          // coeficientes YUV->RGB
#define TAM_UNIFORME   0x100

#define IMG_LUMA  0
#define IMG_CROMA 1
#define IMG_SOBRE 2
#define AMOST_LINEAR  0
#define AMOST_PERTO   1

typedef struct {
    float escala[4]; // y: multiplicador, deslocamento
    float coefs[4];  // R<-V, G<-U, G<-V, B<-U
} Cores;

typedef struct {
    void      *endereco;   // memória do quadro (mapa nvtegra)
    uint32_t   tamanho;
    uint32_t   ofs_croma;
    int        largura, altura;
    DkMemBlock bloco;
    DkImage    luma, croma;
    DkImageDescriptor desc_luma, desc_croma;
} Mapeamento;

static bool g_ativa;
static char g_erro[160];

static DkDevice g_disp;
static DkQueue g_fila;
static DkCmdBuf g_cmd;
static DkMemBlock g_bloco_codigo, g_bloco_dados, g_bloco_tela, g_bloco_sobre;
static DkShader g_sh_vertice, g_sh_video, g_sh_sobre;
static DkImage g_tela[NUM_IMAGENS_TELA];
static DkSwapchain g_troca;
static DkImage g_img_sobre;
static uint32_t *g_px_sobre;
static bool g_sobre_pronta; // a imagem do texto tem conteúdo válido

static Mapeamento g_mapas[MAX_MAPEAMENTOS];
static int g_num_mapas;
static int g_mapa_atual = -1;
static AVFrame *g_exibido;     // quadro na tela agora (fica preso até o próximo ser desenhado)
static Cores g_cores;
static int g_ajuste_preto = 0;        // 0, 1, 2: preto em 16, 24 ou 32 (teste da v0.7.3)
static const int NIVEL_PRETO[3] = {16, 24, 32};
static u64 g_ultima_medida;           // ticks da última medição dos níveis de luma

bool video_tela_ativa(void) { return g_ativa; }
const char *video_tela_erro(void) { return g_erro; }

static void ao_erro_deko3d(void *dados, const char *contexto, DkResult r, const char *msg) {
    (void)dados;
    LOG("video: deko3d: %s (%d) %s", contexto ? contexto : "", (int)r, msg ? msg : "");
}

// Carrega um shader .dksh embutido: cabeçalho + controle ficam na CPU, o código vai para a GPU
typedef struct {
    uint32_t magic, header_sz, control_sz, code_sz, programs_off, num_programs;
} CabecalhoDksh;

static bool carregar_shader(DkShader *sh, const uint8_t *dados, size_t tamanho, uint32_t *ofs_codigo) {
    CabecalhoDksh cab;
    if (tamanho < sizeof cab) return false;
    memcpy(&cab, dados, sizeof cab);
    if (cab.control_sz + cab.code_sz > tamanho) return false;
    uint32_t ofs = (*ofs_codigo + DK_SHADER_CODE_ALIGNMENT - 1) & ~(uint32_t)(DK_SHADER_CODE_ALIGNMENT - 1);
    if (ofs + cab.code_sz > TAM_CODIGO - DK_SHADER_CODE_UNUSABLE_SIZE) return false;
    memcpy((uint8_t *)dkMemBlockGetCpuAddr(g_bloco_codigo) + ofs, dados + cab.control_sz, cab.code_sz);
    // O controle só é lido aqui; copiado para memória alinhada como no exemplo do deko3d
    void *controle = aligned_alloc(16, (cab.control_sz + 15) & ~15u);
    if (!controle) return false;
    memcpy(controle, dados, cab.control_sz);
    DkShaderMaker m;
    dkShaderMakerDefaults(&m, g_bloco_codigo, ofs);
    m.control = controle;
    m.programId = 0;
    dkShaderInitialize(sh, &m);
    free(controle);
    *ofs_codigo = ofs + cab.code_sz;
    return true;
}

static DkMemBlock criar_bloco(uint32_t tamanho, uint32_t flags, void *memoria) {
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, g_disp, (tamanho + DK_MEMBLOCK_ALIGNMENT - 1) & ~(uint32_t)(DK_MEMBLOCK_ALIGNMENT - 1));
    m.flags = flags;
    m.storage = memoria;
    return dkMemBlockCreate(&m);
}

static void escrever_descritor_img(int id, const DkImageDescriptor *d) {
    memcpy((uint8_t *)dkMemBlockGetCpuAddr(g_bloco_dados) + OFS_DESC_IMG + id * sizeof(DkImageDescriptor), d,
           sizeof *d);
}

// Coeficientes YUV->RGB conforme o quadro (padrão: BT.709 faixa limitada)
static void definir_cores(const AVFrame *q) {
    // Como no Moonlight-Switch: o NVDEC às vezes marca faixa cheia sem ser; para o
    // vídeo do Xbox (faixa limitada) isso deixaria os pretos acinzentados.
    bool cheia = q->color_range == AVCOL_RANGE_JPEG && q->format != AV_PIX_FMT_NVTEGRA;
    bool bt601 = q->colorspace == AVCOL_SPC_SMPTE170M || q->colorspace == AVCOL_SPC_BT470BG;
    float rv, gu, gv, bu;
    if (bt601) {
        rv = cheia ? 1.4020f : 1.5960f; gu = cheia ? 0.3441f : 0.3917f;
        gv = cheia ? 0.7141f : 0.8129f; bu = cheia ? 1.7720f : 2.0172f;
    } else {
        rv = cheia ? 1.5748f : 1.7927f; gu = cheia ? 0.1873f : 0.2132f;
        gv = cheia ? 0.4681f : 0.5329f; bu = cheia ? 1.8556f : 2.1124f;
    }
    // Preto e branco: na faixa limitada o preto é 16 e o branco 235. O ajuste do menu
    // sobe o preto (24 ou 32) mantendo o branco em 235.
    float preto = cheia ? 0.0f : (float)NIVEL_PRETO[g_ajuste_preto];
    float branco = cheia ? 255.0f : 235.0f;
    float escala = 255.0f / (branco - preto);
    // Croma: os coeficientes acima já incluem a faixa (224 níveis); só o luma muda
    Cores c = {{escala, preto / 255.0f, 0, 0}, {rv, gu, gv, bu}};
    if (memcmp(&c, &g_cores, sizeof c) != 0) {
        g_cores = c;
        LOG("video: cores %s, faixa %s, preto em %.0f (range=%d espaco=%d primarias=%d transf=%d)",
            bt601 ? "BT.601" : "BT.709", cheia ? "cheia" : "limitada", preto, q->color_range, q->colorspace,
            q->color_primaries, q->color_trc);
    }
}

// Mede os níveis de luma do quadro (mínimo, percentis e máximo) lendo a memória
// do NVDEC pela CPU. A ordem dos bytes (layout de vídeo) não importa para isso.
static void medir_niveis(const AVFrame *q) {
    AVNVTegraMap *mapa = av_nvtegra_frame_get_fbuf_map(q);
    if (!mapa) return;
    const uint8_t *luma = (const uint8_t *)av_nvtegra_map_get_addr(mapa);
    uint32_t tam = (uint32_t)(q->data[1] - q->data[0]); // o plano de luma vem antes do croma
    if (!luma || tam == 0) return;
    static uint32_t hist[256];
    memset(hist, 0, sizeof hist);
    uint32_t n = 0;
    for (uint32_t i = 0; i < tam; i += 61) { // ~23 mil amostras espalhadas
        hist[luma[i]]++;
        n++;
    }
    int minimo = 0, maximo = 255, p1 = 0, p5 = 0, p50 = 0;
    while (minimo < 255 && !hist[minimo]) minimo++;
    while (maximo > 0 && !hist[maximo]) maximo--;
    uint32_t soma = 0;
    bool a1 = false, a5 = false, a50 = false;
    for (int v = 0; v < 256; v++) {
        soma += hist[v];
        if (!a1 && soma >= n / 100) { p1 = v; a1 = true; }
        if (!a5 && soma >= n / 20) { p5 = v; a5 = true; }
        if (!a50 && soma >= n / 2) { p50 = v; a50 = true; }
    }
    LOG("video: luma min %d, 1%% %d, 5%% %d, mediana %d, max %d (preto ajustado em %d)", minimo, p1, p5, p50,
        maximo, NIVEL_PRETO[g_ajuste_preto]);
}

void video_tela_ajuste_preto(int nivel) {
    if (nivel < 0 || nivel > 2) nivel = 0;
    g_ajuste_preto = nivel;
    memset(&g_cores, 0, sizeof g_cores); // recalcula e registra no próximo quadro
    g_ultima_medida = 0;                 // e mede de novo já
}

int video_tela_nivel_preto(void) { return NIVEL_PRETO[g_ajuste_preto]; }

// Encontra (ou cria) o bloco da GPU para a memória deste quadro do NVDEC
static int mapear(const AVFrame *q) {
    AVNVTegraMap *mapa = av_nvtegra_frame_get_fbuf_map(q);
    if (!mapa) return -1;
    void *end = av_nvtegra_map_get_addr(mapa);
    uint32_t tam = av_nvtegra_map_get_size(mapa);
    uint32_t ofs_croma = (uint32_t)(q->data[1] - q->data[0]);
    for (int i = 0; i < g_num_mapas; i++) {
        Mapeamento *m = &g_mapas[i];
        if (m->endereco == end && m->tamanho == tam && m->ofs_croma == ofs_croma && m->largura == q->width &&
            m->altura == q->height)
            return i;
    }
    if (g_num_mapas == MAX_MAPEAMENTOS) {
        LOG("video: mais de %d quadros diferentes do NVDEC; recomeçando os mapeamentos", MAX_MAPEAMENTOS);
        for (int i = 0; i < g_num_mapas; i++) dkMemBlockDestroy(g_mapas[i].bloco);
        g_num_mapas = 0;
        g_mapa_atual = -1;
    }
    Mapeamento *m = &g_mapas[g_num_mapas];
    memset(m, 0, sizeof *m);
    m->endereco = end;
    m->tamanho = tam;
    m->ofs_croma = ofs_croma;
    m->largura = q->width;
    m->altura = q->height;
    m->bloco = criar_bloco(tam, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, end);
    if (!m->bloco) {
        LOG("video: nao foi possivel mapear o quadro na GPU (%u bytes)", tam);
        return -1;
    }
    DkImageLayoutMaker lm;
    DkImageLayout l_luma, l_croma;
    dkImageLayoutMakerDefaults(&lm, g_disp);
    lm.flags = DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo;
    lm.format = DkImageFormat_R8_Unorm;
    lm.dimensions[0] = (uint32_t)q->width;
    lm.dimensions[1] = (uint32_t)q->height;
    dkImageLayoutInitialize(&l_luma, &lm);
    lm.format = DkImageFormat_RG8_Unorm;
    lm.dimensions[0] = (uint32_t)q->width / 2;
    lm.dimensions[1] = (uint32_t)q->height / 2;
    dkImageLayoutInitialize(&l_croma, &lm);
    dkImageInitialize(&m->luma, &l_luma, m->bloco, 0);
    dkImageInitialize(&m->croma, &l_croma, m->bloco, ofs_croma);
    DkImageView v;
    dkImageViewDefaults(&v, &m->luma);
    dkImageDescriptorInitialize(&m->desc_luma, &v, false, false);
    dkImageViewDefaults(&v, &m->croma);
    dkImageDescriptorInitialize(&m->desc_croma, &v, false, false);
    if (g_num_mapas == 0)
        LOG("video: primeiro quadro mapeado na GPU (%dx%d, %u bytes, croma em +%u)", q->width, q->height, tam,
            ofs_croma);
    return g_num_mapas++;
}

bool video_tela_iniciar(void) {
    if (g_ativa) return true;
    g_erro[0] = 0;
    memset(&g_cores, 0, sizeof g_cores);
    g_ultima_medida = 0;
    g_num_mapas = 0;
    g_mapa_atual = -1;
    g_sobre_pronta = false;

    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = ao_erro_deko3d;
    g_disp = dkDeviceCreate(&dm);
    if (!g_disp) {
        snprintf(g_erro, sizeof g_erro, "Nao foi possivel iniciar a GPU (deko3d).");
        return false;
    }

    g_bloco_codigo = criar_bloco(TAM_CODIGO, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, NULL);
    g_bloco_dados = criar_bloco(TAM_DADOS, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, NULL);
    g_bloco_sobre = criar_bloco(SOBRE_LARGURA * SOBRE_ALTURA * 4, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached, NULL);

    // Imagens da tela (troca dupla, sincronizada com o vsync)
    DkImageLayoutMaker lm;
    DkImageLayout l_tela, l_sobre;
    dkImageLayoutMakerDefaults(&lm, g_disp);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = TELA_L;
    lm.dimensions[1] = TELA_A;
    dkImageLayoutInitialize(&l_tela, &lm);
    uint32_t alinh = dkImageLayoutGetAlignment(&l_tela);
    uint32_t tam_img = ((uint32_t)dkImageLayoutGetSize(&l_tela) + alinh - 1) & ~(alinh - 1);
    g_bloco_tela = criar_bloco(tam_img * NUM_IMAGENS_TELA, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, NULL);

    if (!g_bloco_codigo || !g_bloco_dados || !g_bloco_sobre || !g_bloco_tela) {
        snprintf(g_erro, sizeof g_erro, "Sem memoria para a GPU.");
        goto falhar;
    }
    DkImage const *imagens[NUM_IMAGENS_TELA];
    for (int i = 0; i < NUM_IMAGENS_TELA; i++) {
        dkImageInitialize(&g_tela[i], &l_tela, g_bloco_tela, i * tam_img);
        imagens[i] = &g_tela[i];
    }

    // Imagem do texto: linear, escrita pela CPU
    dkImageLayoutMakerDefaults(&lm, g_disp);
    lm.flags = DkImageFlags_PitchLinear;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = SOBRE_LARGURA;
    lm.dimensions[1] = SOBRE_ALTURA;
    lm.pitchStride = SOBRE_LARGURA * 4;
    dkImageLayoutInitialize(&l_sobre, &lm);
    dkImageInitialize(&g_img_sobre, &l_sobre, g_bloco_sobre, 0);
    g_px_sobre = (uint32_t *)dkMemBlockGetCpuAddr(g_bloco_sobre);

    // Shaders
    uint32_t ofs = 0;
    if (!carregar_shader(&g_sh_vertice, tela_vsh_dksh, tela_vsh_dksh_size, &ofs) ||
        !carregar_shader(&g_sh_video, video_fsh_dksh, video_fsh_dksh_size, &ofs) ||
        !carregar_shader(&g_sh_sobre, sobre_fsh_dksh, sobre_fsh_dksh_size, &ofs)) {
        snprintf(g_erro, sizeof g_erro, "Shaders invalidos no build.");
        goto falhar;
    }

    // Descritores fixos: texto e as duas formas de amostragem
    DkImageView v;
    DkImageDescriptor d;
    dkImageViewDefaults(&v, &g_img_sobre);
    dkImageDescriptorInitialize(&d, &v, false, false);
    escrever_descritor_img(IMG_SOBRE, &d);
    DkSampler s;
    DkSamplerDescriptor ds[2];
    dkSamplerDefaults(&s);
    s.minFilter = s.magFilter = DkFilter_Linear;
    s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(&ds[AMOST_LINEAR], &s);
    s.minFilter = s.magFilter = DkFilter_Nearest;
    dkSamplerDescriptorInitialize(&ds[AMOST_PERTO], &s);
    memcpy((uint8_t *)dkMemBlockGetCpuAddr(g_bloco_dados) + OFS_DESC_AMOST, ds, sizeof ds);

    // Fila, comandos e a troca de imagens na janela do app
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, g_disp);
    qm.flags = DkQueueFlags_Graphics;
    g_fila = dkQueueCreate(&qm);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, g_disp);
    g_cmd = dkCmdBufCreate(&cm);
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, g_disp, nwindowGetDefault(), imagens, NUM_IMAGENS_TELA);
    g_troca = dkSwapchainCreate(&sm);
    if (!g_fila || !g_cmd || !g_troca) {
        snprintf(g_erro, sizeof g_erro, "Nao foi possivel assumir a tela.");
        goto falhar;
    }

    g_ativa = true;
    LOG("video: GPU pronta (deko3d, %dx%d, %d imagens, shaders %u bytes)", TELA_L, TELA_A, NUM_IMAGENS_TELA, ofs);
    return true;

falhar:
    LOG("video: %s", g_erro);
    g_ativa = true; // para o encerrar limpar o que já foi criado
    video_tela_encerrar();
    return false;
}

long long video_tela_desenhar(AVFrame *quadro) {
    if (!g_ativa) {
        av_frame_free(&quadro);
        return -1;
    }
    int novo_mapa = -1;
    if (quadro) {
        if (quadro->format != AV_PIX_FMT_NVTEGRA) {
            av_frame_free(&quadro); // decodificado pela CPU: não desenhado nesta fase
        } else {
            novo_mapa = mapear(quadro);
            if (novo_mapa < 0) {
                av_frame_free(&quadro);
            } else {
                definir_cores(quadro);
                if (!g_ultima_medida || armTicksToNs(armGetSystemTick() - g_ultima_medida) > 10000000000ULL) {
                    g_ultima_medida = armGetSystemTick();
                    medir_niveis(quadro);
                }
            }
        }
    }

    // Camada de texto: só renderiza quando o conteúdo muda
    bool com_sobre = sobre_visivel();
    bool sobre_atualizada = false;
    if (com_sobre && (sobre_mudou() || !g_sobre_pronta)) {
        sobre_renderizar(g_px_sobre, SOBRE_LARGURA);
        dkMemBlockFlushCpuCache(g_bloco_sobre, 0, SOBRE_LARGURA * SOBRE_ALTURA * 4);
        g_sobre_pronta = true;
        sobre_atualizada = true;
    }

    int vaga = dkQueueAcquireImage(g_fila, g_troca); // espera uma imagem livre (vsync)

    dkCmdBufClear(g_cmd);
    dkCmdBufAddMemory(g_cmd, g_bloco_dados, OFS_COMANDOS, TAM_COMANDOS);
    DkGpuAddr base = dkMemBlockGetGpuAddr(g_bloco_dados);
    int mapa = novo_mapa >= 0 ? novo_mapa : g_mapa_atual;
    if (mapa >= 0) {
        // Descritores do quadro atual pelo próprio fluxo de comandos (mantém a ordem)
        dkCmdBufPushData(g_cmd, base + OFS_DESC_IMG + IMG_LUMA * sizeof(DkImageDescriptor), &g_mapas[mapa].desc_luma,
                         sizeof(DkImageDescriptor));
        dkCmdBufPushData(g_cmd, base + OFS_DESC_IMG + IMG_CROMA * sizeof(DkImageDescriptor),
                         &g_mapas[mapa].desc_croma, sizeof(DkImageDescriptor));
    }
    dkCmdBufBarrier(g_cmd, DkBarrier_None,
                    DkInvalidateFlags_Descriptors | (sobre_atualizada ? DkInvalidateFlags_Image : 0));

    DkImageView alvo;
    dkImageViewDefaults(&alvo, &g_tela[vaga]);
    dkCmdBufBindRenderTarget(g_cmd, &alvo, NULL);
    DkViewport vp = {0, 0, TELA_L, TELA_A, 0.0f, 1.0f};
    DkScissor sc = {0, 0, TELA_L, TELA_A};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    dkCmdBufSetScissors(g_cmd, 0, &sc, 1);
    dkCmdBufBindImageDescriptorSet(g_cmd, base + OFS_DESC_IMG, 3);
    dkCmdBufBindSamplerDescriptorSet(g_cmd, base + OFS_DESC_AMOST, 2);

    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    DkColorState cs;
    dkColorStateDefaults(&cs);
    DkColorWriteState cws;
    dkColorWriteStateDefaults(&cws);
    dkCmdBufBindRasterizerState(g_cmd, &rs);
    dkCmdBufBindColorState(g_cmd, &cs);
    dkCmdBufBindColorWriteState(g_cmd, &cws);

    if (mapa >= 0) {
        DkShader const *sh[] = {&g_sh_vertice, &g_sh_video};
        dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, sh, 2);
        DkResHandle tex[] = {dkMakeTextureHandle(IMG_LUMA, AMOST_LINEAR), dkMakeTextureHandle(IMG_CROMA, AMOST_LINEAR)};
        dkCmdBufBindTextures(g_cmd, DkStage_Fragment, 0, tex, 2);
        dkCmdBufBindUniformBuffer(g_cmd, DkStage_Fragment, 0, base + OFS_UNIFORME, TAM_UNIFORME);
        dkCmdBufPushConstants(g_cmd, base + OFS_UNIFORME, TAM_UNIFORME, 0, sizeof g_cores, &g_cores);
        dkCmdBufDraw(g_cmd, DkPrimitive_TriangleStrip, 4, 1, 0, 0);
    } else {
        dkCmdBufClearColorFloat(g_cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (com_sobre && g_sobre_pronta) {
        DkBlendState bs;
        dkBlendStateDefaults(&bs); // alfa: SrcAlpha / InvSrcAlpha
        cs.blendEnableMask = 1;
        dkCmdBufBindColorState(g_cmd, &cs);
        dkCmdBufBindBlendStates(g_cmd, 0, &bs, 1);
        DkShader const *sh[] = {&g_sh_vertice, &g_sh_sobre};
        dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, sh, 2);
        // Texto em pixels exatos (1280x720 na tela de 1280x720)
        dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 2, dkMakeTextureHandle(IMG_SOBRE, AMOST_PERTO));
        dkCmdBufDraw(g_cmd, DkPrimitive_TriangleStrip, 4, 1, 0, 0);
    }

    dkQueueSubmitCommands(g_fila, dkCmdBufFinishList(g_cmd));
    dkQueuePresentImage(g_fila, g_troca, vaga);
    // Simples e seguro: espera a GPU terminar antes de soltar o quadro anterior e
    // de reaproveitar a memória de comandos (um retângulo por quadro: leva pouco).
    dkQueueWaitIdle(g_fila);

    if (novo_mapa >= 0) {
        av_frame_free(&g_exibido); // o anterior já não está na tela
        g_exibido = quadro;
        g_mapa_atual = novo_mapa;
        return quadro->pts == AV_NOPTS_VALUE ? -1 : (long long)quadro->pts;
    }
    return -1;
}

void video_tela_encerrar(void) {
    if (!g_ativa) return;
    if (g_fila) dkQueueWaitIdle(g_fila);
    if (g_troca) dkSwapchainDestroy(g_troca);
    if (g_fila) dkQueueDestroy(g_fila);
    if (g_cmd) dkCmdBufDestroy(g_cmd);
    for (int i = 0; i < g_num_mapas; i++) dkMemBlockDestroy(g_mapas[i].bloco);
    av_frame_free(&g_exibido); // depois de soltar os blocos que apontam para ele
    if (g_bloco_tela) dkMemBlockDestroy(g_bloco_tela);
    if (g_bloco_sobre) dkMemBlockDestroy(g_bloco_sobre);
    if (g_bloco_dados) dkMemBlockDestroy(g_bloco_dados);
    if (g_bloco_codigo) dkMemBlockDestroy(g_bloco_codigo);
    if (g_disp) dkDeviceDestroy(g_disp);
    g_troca = NULL;
    g_fila = NULL;
    g_cmd = NULL;
    g_bloco_tela = g_bloco_sobre = g_bloco_dados = g_bloco_codigo = NULL;
    g_disp = NULL;
    g_num_mapas = 0;
    g_mapa_atual = -1;
    g_px_sobre = NULL;
    g_ativa = false;
    LOG("video: GPU encerrada");
}
