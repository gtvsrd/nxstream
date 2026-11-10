// Testes das partes sem rede: interpretação de respostas e armazenamento.
// Rodam no PC e no CI:  make -C tests
#include "auth/auth.h"
#include "api/xapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_falhas = 0, g_total = 0;

#define CHECAR(cond) do { \
    g_total++; \
    if (!(cond)) { g_falhas++; printf("  FALHOU (linha %d): %s\n", __LINE__, #cond); } \
} while (0)

static void teste_codigo(void) {
    puts("codigo de login");
    AuthCodigo c;
    CHECAR(auth_parse_codigo(
        "{\"user_code\":\"ABCD1234\",\"device_code\":\"DAQABAAEAAAB\",\"verification_uri\":"
        "\"https://www.microsoft.com/link\",\"expires_in\":900,\"interval\":5,\"message\":\"x\"}", &c));
    CHECAR(!strcmp(c.codigo_usuario, "ABCD1234"));
    CHECAR(!strcmp(c.codigo_dispositivo, "DAQABAAEAAAB"));
    CHECAR(!strcmp(c.endereco, "https://www.microsoft.com/link"));
    CHECAR(c.expira_em_s == 900 && c.intervalo_s == 5);

    CHECAR(!auth_parse_codigo("{\"error\":\"invalid_client\"}", &c));
    CHECAR(!auth_parse_codigo("isto nao e json", &c));
}

static void teste_token(void) {
    puts("token da conta");
    AuthConta conta = {0};
    CHECAR(auth_parse_token(200, "{\"access_token\":\"AAA\",\"refresh_token\":\"RRR\",\"token_type\":\"bearer\"}", &conta) == AUTH_OK);
    CHECAR(conta.access_token && !strcmp(conta.access_token, "AAA"));
    CHECAR(conta.refresh_token && !strcmp(conta.refresh_token, "RRR"));

    // Renovação substitui os dois tokens
    CHECAR(auth_parse_token(200, "{\"access_token\":\"A2\",\"refresh_token\":\"R2\"}", &conta) == AUTH_OK);
    CHECAR(!strcmp(conta.access_token, "A2") && !strcmp(conta.refresh_token, "R2"));

    CHECAR(auth_parse_token(400, "{\"error\":\"authorization_pending\"}", &conta) == AUTH_PENDENTE);
    CHECAR(auth_parse_token(400, "{\"error\":\"slow_down\"}", &conta) == AUTH_LENTO);
    CHECAR(auth_parse_token(400, "{\"error\":\"authorization_declined\"}", &conta) == AUTH_RECUSADO);
    CHECAR(auth_parse_token(400, "{\"error\":\"expired_token\"}", &conta) == AUTH_EXPIRADO);
    CHECAR(auth_parse_token(400, "{\"error\":\"invalid_grant\"}", &conta) == AUTH_INVALIDO);
    CHECAR(auth_parse_token(500, "<html>erro</html>", &conta) == AUTH_ERRO);
    CHECAR(auth_parse_token(200, "{\"access_token\":\"so_um\"}", &conta) == AUTH_ERRO);
    // Respostas de erro não apagam os tokens que já existiam
    CHECAR(!strcmp(conta.access_token, "A2"));
    auth_conta_liberar(&conta);
    CHECAR(conta.access_token == NULL);
}

static void teste_xbl(void) {
    puts("tokens do Xbox Live");
    char gtg[64];
    char *t = auth_parse_xbl_token(
        "{\"IssueInstant\":\"2026-11-10T00:00:00Z\",\"Token\":\"eyJ0eXAi\","
        "\"DisplayClaims\":{\"xui\":[{\"uhs\":\"123\",\"gtg\":\"Gustavo123\"}]}}", gtg, sizeof gtg);
    CHECAR(t && !strcmp(t, "eyJ0eXAi"));
    CHECAR(!strcmp(gtg, "Gustavo123"));
    free(t);

    t = auth_parse_xbl_token("{\"Token\":\"T\",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"1\"}]}}", gtg, sizeof gtg);
    CHECAR(t && gtg[0] == '\0');
    free(t);

    CHECAR(auth_parse_xbl_token("{\"XErr\":2148916233}", gtg, sizeof gtg) == NULL);
    CHECAR(auth_descrever_xerr(2148916233LL) != NULL);
    CHECAR(auth_descrever_xerr(1) == NULL);
}

static void teste_streaming(void) {
    puts("token de streaming");
    AuthStreaming s = {0};
    CHECAR(auth_parse_streaming(
        "{\"offeringSettings\":{\"allowRegionSelection\":false,\"regions\":["
        "{\"name\":\"WestUS\",\"baseUri\":\"https://wus.core.gssv-play-prodxhome.xboxlive.com\",\"isDefault\":false},"
        "{\"name\":\"UKSouth\",\"baseUri\":\"https://uks.core.gssv-play-prodxhome.xboxlive.com/\",\"isDefault\":true}]},"
        "\"market\":\"BR\",\"gsToken\":\"GST\",\"tokenType\":\"bearer\",\"durationInSeconds\":14400}", &s));
    CHECAR(s.gs_token && !strcmp(s.gs_token, "GST"));
    CHECAR(!strcmp(s.base_uri, "https://uks.core.gssv-play-prodxhome.xboxlive.com")); // padrão, sem '/' final
    CHECAR(s.duracao_s == 14400);
    auth_streaming_liberar(&s);

    // Sem região padrão: usa a primeira
    AuthStreaming s2 = {0};
    CHECAR(auth_parse_streaming(
        "{\"offeringSettings\":{\"regions\":[{\"baseUri\":\"https://a.example\"},{\"baseUri\":\"https://b.example\"}]},"
        "\"gsToken\":\"G\"}", &s2));
    CHECAR(!strcmp(s2.base_uri, "https://a.example"));
    auth_streaming_liberar(&s2);

    AuthStreaming s3 = {0};
    CHECAR(!auth_parse_streaming("{\"gsToken\":\"G\"}", &s3));
}

static void teste_consoles(void) {
    puts("lista de consoles");
    XConsoles c;
    CHECAR(xapi_parse_consoles(
        "{\"totalItems\":2,\"results\":["
        "{\"deviceName\":\"Xbox da Sala\",\"serverId\":\"F4001ABCDEF\",\"powerState\":\"ConnectedStandby\","
        "\"consoleType\":\"XboxSeriesX\",\"playPath\":\"\",\"outOfHomeWarning\":false},"
        "{\"deviceName\":\"Quarto\",\"serverId\":\"F4002\",\"powerState\":\"On\",\"consoleType\":\"XboxOneS\"}],"
        "\"continuationToken\":null}", &c));
    CHECAR(c.total == 2);
    CHECAR(!strcmp(c.itens[0].nome, "Xbox da Sala"));
    CHECAR(!strcmp(c.itens[0].server_id, "F4001ABCDEF"));
    CHECAR(!strcmp(xapi_estado_amigavel(c.itens[0].estado), "Em repouso"));
    CHECAR(!strcmp(xapi_tipo_amigavel(c.itens[0].tipo), "Xbox Series X"));
    CHECAR(!strcmp(xapi_estado_amigavel(c.itens[1].estado), "Ligado"));

    CHECAR(xapi_parse_consoles("{\"totalItems\":0,\"results\":[]}", &c) && c.total == 0);
    CHECAR(!xapi_parse_consoles("{\"erro\":1}", &c));
}

static void teste_armazenamento(void) {
    puts("armazenamento do token");
    const char *arq = "teste_token.json";
    remove(arq);
    CHECAR(auth_carregar_refresh(arq) == NULL);
    CHECAR(auth_salvar_refresh(arq, "M.C5_BAY.0.U.-Cabc!*$def"));
    char *t = auth_carregar_refresh(arq);
    CHECAR(t && !strcmp(t, "M.C5_BAY.0.U.-Cabc!*$def"));
    free(t);
    CHECAR(auth_salvar_refresh(arq, "NOVO")); // sobrescreve
    t = auth_carregar_refresh(arq);
    CHECAR(t && !strcmp(t, "NOVO"));
    free(t);
    CHECAR(auth_apagar(arq));
    CHECAR(auth_carregar_refresh(arq) == NULL);
}

int main(void) {
    teste_codigo();
    teste_token();
    teste_xbl();
    teste_streaming();
    teste_consoles();
    teste_armazenamento();
    printf("\n%d de %d verificacoes passaram\n", g_total - g_falhas, g_total);
    return g_falhas ? 1 : 0;
}
