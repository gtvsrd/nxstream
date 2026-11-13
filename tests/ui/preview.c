// Gera imagens das telas da interface no PC (para conferir o visual sem o Switch).
// Uso: make -C tests/ui FONTE=/caminho/fonte.ttf  -> tests/ui/saida/*.ppm
#include "ui/tela.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t px[UI_LARGURA * UI_ALTURA];

static void salvar(const char *nome) {
    char caminho[256];
    snprintf(caminho, sizeof caminho, "saida/%s.ppm", nome);
    FILE *f = fopen(caminho, "wb");
    fprintf(f, "P6\n%d %d\n255\n", UI_LARGURA, UI_ALTURA);
    for (int i = 0; i < UI_LARGURA * UI_ALTURA; i++) {
        unsigned char c[3] = {px[i] & 0xFF, (px[i] >> 8) & 0xFF, (px[i] >> 16) & 0xFF};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "", "rb");
    if (!f) { fprintf(stderr, "fonte?\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc(n); fread(ttf, 1, n, f); fclose(f);
    ui_definir_fonte(ttf);

    ui_definir_conta("Gamertag123");
    ui_status("Conectando a XBOX", "Preparando o console",
              "Xbox Series X · em repouso · 6 s\nSe o console estava em repouso, ele acorda sem ligar a TV.",
              "B=Cancelar");
    ui_renderizar(px, UI_LARGURA, 0.3); salvar("1_status");

    ui_status("Reconectando a XBOX", "Nova tentativa em 2 s", "Tentativa 2 de 5", "B=Voltar para a lista");
    ui_renderizar(px, UI_LARGURA, 1.1); salvar("2_reconectando");

    ui_codigo(NULL, "microsoft.com/link", "ABCD1234", "Aguardando login · o código expira em 14:32",
              "A=Abrir a página no Switch|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0.5); salvar("3_codigo");

    ui_mensagem(UI_ERRO, "Não foi possível abrir a sessão.",
                "xapi: criar sessao respondeu HTTP 503: {\"code\":\"ServiceUnavailable\"} Confira se o console "
                "está ligado ou em repouso e conectado à internet.",
                "A=Tentar de novo|B=Voltar para a lista|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("4_erro");

    ui_mensagem(UI_AVISO, "Sair da conta?", "Na próxima vez será preciso fazer login de novo.",
                "A=Sair da conta|B=Cancelar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("5_aviso");

    UiItem itens[2] = {{"XBOX", "Xbox Series X · Em repouso", 2}, {"Sala", "Xbox One S · Desligado", 0}};
    ui_lista("Seus consoles", "Conectado como Gamertag123", itens, 2, 0, NULL,
             "A=Conectar|Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("6_lista");

    ui_lista("Seus consoles", "Conectado como Gamertag123", NULL, 0, 0,
             "Nenhum console encontrado nesta conta.\nNo Xbox, ative em: Configurações > Dispositivos e conexões > "
             "Recursos remotos > Habilitar recursos remotos.",
             "Y=Atualizar|X=Sair da conta|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("7_lista_vazia");
    // Menu por cima do vídeo: fundo simulando um quadro do jogo com o véu escuro
    for (int i = 0; i < UI_LARGURA * UI_ALTURA; i++) {
        int x = i % UI_LARGURA, y = i / UI_LARGURA;
        px[i] = 0xFF000000u | ((uint32_t)(40 + y / 12) << 16) | ((uint32_t)(30 + x / 20) << 8) | (uint32_t)(20 + x / 40);
    }
    const char *opcoes[] = {"Voltar ao jogo", "Botão Xbox (guia)", "Mostrar estatísticas", "Desconectar",
                           "Fechar o NXStream"};
    ui_menu_video(px, UI_LARGURA, "NXStream", "XBOX · conectado há 12 min", opcoes, 5, 0, "A=Escolher|B=Voltar");
    salvar("8_menu_video");
    return 0;
}
