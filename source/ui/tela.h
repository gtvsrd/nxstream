// NXStream - interface gráfica simples (v0.10.0)
//
// Telas inspiradas no app do Xbox: fundo escuro, verde de destaque, texto com a
// fonte do próprio Switch. Desenhadas pela CPU numa imagem 1280x720 que vai para
// a tela pelo framebuffer do libnx (a mesma janela do console de texto e da GPU,
// então só um dos três fica ativo por vez).
//
// Uso: escolher a tela com ui_status/ui_codigo/ui_mensagem/ui_lista (pode chamar
// a cada quadro, só copia os textos) e chamar ui_desenhar() uma vez por quadro,
// que redesenha tudo (com a animação) e espera o vsync.
//
// Rodapé: botões no formato "A=Conectar|Y=Atualizar|+=Fechar". Botões
// reconhecidos: A, B, X, Y, +, -, L, R (L e R no formato dos botões de ombro).
// Textos em UTF-8 (acentos funcionam).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_LARGURA 1280
#define UI_ALTURA  720

typedef enum { UI_ERRO, UI_AVISO, UI_INFO } UiTipo;

typedef struct {
    char nome[64];
    char detalhe[96];
    int  estado; // bolinha à direita: 0 = cinza, 1 = verde, 2 = amarela
} UiItem;

#define UI_MAX_ITENS 8

// Liga a interface (framebuffer). false = não deu (o app segue no console de texto).
bool ui_iniciar(void);
void ui_encerrar(void);
bool ui_ativa(void);

// Símbolo pulsando no centro, com até três linhas de texto abaixo.
// titulo: grande; status: em verde; detalhe: menor, em cinza (aceita \n).
void ui_status(const char *titulo, const char *status, const char *detalhe, const char *rodape);
// Login por código: endereço e código em destaque.
void ui_codigo(const char *aviso, const char *endereco, const char *codigo, const char *status,
               const char *rodape);
// Erro, aviso ou pergunta, com ícone.
void ui_mensagem(UiTipo tipo, const char *titulo, const char *detalhe, const char *rodape);
// Lista (consoles): `selecionado` em destaque.
void ui_lista(const char *titulo, const char *subtitulo, const UiItem *itens, int total, int selecionado,
              const char *mensagem, const char *rodape);

// Gamertag mostrada no cabeçalho de todas as telas ("" ou NULL = nenhuma).
void ui_definir_conta(const char *gamertag);

// Foto do perfil no cabeçalho (v0.13.1): RGBA UI_FOTO x UI_FOTO, ou NULL para
// voltar à inicial da gamertag. A tela guarda só o ponteiro.
#define UI_FOTO 34
void ui_definir_foto(const uint32_t *px);

// Abas "Consoles" e "Nuvem" no cabeçalho (v0.13.0), com os botões L e R.
// 0 = Consoles, 1 = Nuvem, -1 = sem abas. Só aparecem na lista e na nuvem.
void ui_definir_abas(int ativa);

// --- Jogos na nuvem (v0.13.0): faixa de recentes + grade de capas -----------
// Quem chama faz o layout (posição de cada capa, já com a rolagem); a tela só
// desenha. Capas: RGBA UI_CAPA_L x UI_CAPA_A (ui_decodificar_capa); NULL =
// ainda não chegou (aparece o nome no lugar).
#define UI_CAPA_L 140
#define UI_CAPA_A 210
#define UI_MAX_CARTOES 48
typedef struct {
    int x, y;
    char nome[64];
    const uint32_t *capa;
    bool selecionado;
} UiCartao;
typedef struct {
    int y;
    char texto[96];
} UiRotulo;
typedef struct {
    UiCartao cartoes[UI_MAX_CARTOES];
    int n_cartoes;
    UiRotulo rotulos[4];
    int n_rotulos;
    int clip_topo, clip_base;   // faixa vertical onde capas e rótulos aparecem
    char nome[96], info[160];   // jogo selecionado, embaixo da grade
    char aviso[256];            // no meio da área (ex.: busca sem resultado)
} UiNuvem;
void ui_nuvem(const UiNuvem *n, const char *rodape);
// JPEG/PNG -> RGBA w x h (preenche e corta o centro). Liberar com free(). NULL = falhou.
uint32_t *ui_decodificar_capa(const uint8_t *dados, size_t tam, int w, int h);

// Menu por cima do vídeo: desenha um cartão no centro de uma imagem RGBA
// 1280x720 (a camada de sobreposição). Itens em UTF-8; rodapé como nas telas.
void ui_menu_video(uint32_t *pixels, int passo, const char *titulo, const char *subtitulo,
                   const char *const *itens, int total, int selecionado, const char *rodape);

// Estatísticas por cima do vídeo (v0.10.6): cartão no canto de cima à esquerda.
#define UI_HIST 60
typedef struct {
    char  console[64];
    int   conectado_s;
    int   largura, altura;
    float fps, fps_decod, mbps;
    float decod_ms, decod_ms_max;
    bool  hardware;
    unsigned long pulados, repetidos;  // tela: quadros que não apareceram / atualizações repetidas
    bool  audio_ativo;
    int   audio_fila_ms;
    unsigned long audio_perdidos, audio_faltas;
    // Rede (v0.12.2, montador.c): pacotes de vídeo fora de ordem, recuperados por
    // NACK e quadros perdidos de vez
    unsigned long rede_pacotes, rede_fora_de_ordem, rede_recuperados, rede_quadros_perdidos;
    long  keepalive_status;            // -1 = ainda não enviado
    int   keepalive_ha_s;
    float hist_mbps[UI_HIST];          // últimos segundos, o mais antigo primeiro
    int   hist_total;
} UiEstatisticas;
void ui_estatisticas_video(uint32_t *pixels, int passo, const UiEstatisticas *e);

// Redesenha a tela atual e a mostra (espera o vsync).
void ui_desenhar(void);

// --- Para o teste no PC (tests/ui): desenha sem framebuffer -----------------
// fonte: arquivo TrueType na memória; t: segundos (animação)
void ui_definir_fonte(const unsigned char *ttf);
void ui_renderizar(uint32_t *pixels, int passo, double t);
