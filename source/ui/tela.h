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
// reconhecidos: A, B, X, Y, +, -. Textos em UTF-8 (acentos funcionam).
#pragma once
#include <stdbool.h>
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

// Redesenha a tela atual e a mostra (espera o vsync).
void ui_desenhar(void);

// --- Para o teste no PC (tests/ui): desenha sem framebuffer -----------------
// fonte: arquivo TrueType na memória; t: segundos (animação)
void ui_definir_fonte(const unsigned char *ttf);
void ui_renderizar(uint32_t *pixels, int passo, double t);
