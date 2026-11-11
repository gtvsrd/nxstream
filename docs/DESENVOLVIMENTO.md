# Desenvolvimento

## Build

### Automático (GitHub Actions)

Todo push dispara o workflow [`build.yml`](../.github/workflows/build.yml), que compila dentro do container oficial `devkitpro/devkita64` e publica o `.nro`. As dependências do WebRTC (`scripts/compilar-deps.sh`) ficam em cache e só são recompiladas quando o script ou a camada `compat/` mudam.

Para baixar: aba **Actions** → execução mais recente → seção **Artifacts** → `nxstream-nro` (um `.zip` com o `nxstream.nro`). É preciso estar logado no GitHub.

### Local (opcional)

1. Instale o [devkitPro](https://devkitpro.org/wiki/Getting_Started) com o grupo `switch-dev`.
2. Uma vez, compile as dependências do WebRTC (mbedTLS 3 e libdatachannel, alguns minutos): `sh scripts/compilar-deps.sh`. Elas ficam em `deps/prefix/` (fora do Git).
3. Na pasta do projeto: `make`
4. Para limpar: `make clean`

## Testar no Switch

1. Copie `nxstream.nro` para `/switch/nxstream/` no cartão SD.
2. Abra em **title mode**: segure **R** ao iniciar qualquer jogo instalado e escolha o NXStream no menu do homebrew. Pelo álbum (applet mode) o app tem pouca memória e o streaming não vai funcionar.
3. Anote o resultado e, se algo der errado, mande o arquivo de log (ver abaixo).

## Logs

- **No cartão SD:** `/switch/nxstream/nxstream.log`, com horário de cada evento. O arquivo cresce a cada uso; pode ser apagado a qualquer momento.
- **Ao vivo no PC (nxlink):** no menu do homebrew, aperte **Y** para entrar no modo de rede. No PC, com as ferramentas do devkitPro instaladas, rode:
  ```sh
  nxlink -s nxstream.nro
  ```
  O `.nro` é enviado ao Switch pela rede e as mensagens do app aparecem no terminal. Evita tirar o cartão SD a cada teste.

**Nunca são gravados no log:** tokens, senhas, código de login e qualquer dado da conta.

## Testes no PC

As partes sem rede (interpretação das respostas da Microsoft e armazenamento do token) têm testes que rodam em qualquer PC com `gcc`, e também automaticamente no GitHub Actions:

```sh
make -C tests
```

## Checklists de teste por fase

### Fase 0: diagnóstico

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir em title mode | Linha "Modo: TITLE MODE (ok)" | ✅ ok |
| Rede | Linha com o IP do Switch | ✅ ok |
| Apertar POWER, esperar ~10 s, acordar | App continua aberto e mostra "Switch acordou do sleep" | ✅ v0.0.2: app continua aberto e recebe os avisos do sistema |
| Apertar Y logo após acordar | Tempo até a rede voltar como "conectado" | ✅ Conectado de imediato |
| Colocar e tirar da dock | Linha "Modo mudou" | |

**v0.0.1:** o app não recebeu nenhum aviso ao acordar (o libnx suspende em silêncio por padrão). A rede volta conectada imediatamente.
**v0.0.2:** ativa os avisos do sistema (`AppletFocusHandlingMode_SuspendHomeSleepNotify`) e adiciona detector de congelamento. ✅ O app detecta a volta e a rede volta instantaneamente ao acordar. ⚠️ Correção posterior (log da v0.2.2): no **bloqueio/sleep**, a detecção veio do detector de tempo, não de um aviso do sistema. Nenhum aviso chega nesse caso.

### Fase 1: login e lista de consoles (v0.1.0)

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Primeira abertura | Tela com endereço `microsoft.com/link` e um código | ✅ |
| Digitar o código no celular/PC e logar | App detecta sozinho em poucos segundos e mostra "Conectando ao Xbox Live..." | ✅ |
| Lista de consoles | Gamertag (ou "conectada") e seu Xbox com tipo e estado (Ligado / Em repouso) | ✅ Console encontrado; A mostra o aviso da fase 2 |
| Fechar e abrir o app de novo | Vai direto para a lista, **sem pedir login** | ✅ 2 a 3 s até a lista |
| Abrir de novo no dia seguinte | Continua sem pedir login | |
| Y na lista | Lista atualiza | |
| X na lista e confirmar | Volta para a tela de login | |
| Desligar o Wi-Fi do Switch e abrir o app | Mensagem de erro com "A = tentar de novo", **sem fechar o app** | |

Sleep/resume com o app na lista: retoma em ~0,5 s.

Se algo falhar, mande o arquivo `/switch/nxstream/nxstream.log` (ele não contém tokens).

### Fase 2a: sessão de streaming (v0.2.3)

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| A em um console **ligado** | Estados passam por "Preparando..." até "Sessao pronta!" | ✅ Pronta em ~5 s |
| A em um console **em repouso** | Mesmo resultado; o console acorda só no modo de streaming | ✅ Pronta em ~11 s |
| Tempo até ficar pronta | Mostrado na tela ("preparada em X s") | ✅ ~11 s a partir do repouso |
| Deixar a sessão aberta 3+ minutos | Verificações com "viva" e nenhuma falha (v0.2.2: ✅ ~6 min) | v0.2.0: keepalive HTTP 400 a cada envio (~30–40 s). Servidor: `KeepAlive : ServerStreamingStarted` (keepalive não aceito antes do cliente conectar). v0.2.1 troca por consulta de estado. Sessão continuou existindo por 4+ min |
| Dormir o Switch 30 s com a sessão aberta e acordar | Linha "Voltou de X s suspenso: sessao continua viva" (ou "caiu") | ✅ v0.2.3: detectado e mostrado na tela; sessão viva após 37 s e 98 s |
| Dormir o Switch 5+ minutos e acordar | Idem: descobre por quanto tempo a sessão sobrevive sem keepalive | ✅ Viva após 190 s (3 min 10 s). Limite máximo ainda não medido |
| HOME com a sessão aberta | Detectado como suspensão | ✅ Detectado por tempo (sem aviso do sistema) |
| B na sessão | Volta para a lista; a sessão é encerrada | ✅ `DELETE` HTTP 202 |

**v0.2.1 tem um bug:** chamadas HTTP lentas eram confundidas com suspensão. A v0.2.2 passou a depender só dos avisos do sistema, mas ao **bloquear** o Switch nenhum aviso foi detectado. A v0.2.3 usa os dois sinais: aviso do sistema e tempo parado entre quadros, descontando o tempo das chamadas de rede. O log registra qual sinal detectou cada suspensão.

Os dois testes de sleep não têm resposta "certa": servem para medir quanto tempo o servidor mantém a sessão sem keepalive. Isso define a estratégia de reconexão da fase 4.

### Fase 2b, parte 1: diagnóstico WebRTC (v0.3.0)

Na lista de consoles, aperte **−** para abrir o diagnóstico. Ele não usa o Xbox.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o diagnóstico | App não trava nem fecha | ✅ |
| Certificado e oferta SDP | "ok em X s" com certificado, vídeo, áudio e dados = sim | ✅ 0,01 s, tudo "sim" |
| Candidatos de rede | Pelo menos um com o IP do Switch (o mesmo da fase 0), "coleta completa" | ✅ Coleta completa em 0,05 s |
| Resultado | "tudo funcionando no Switch!" | ✅ |
| A (repetir) algumas vezes | Repete sem travar | ✅ Todas as vezes |

Log da v0.3.0 (35 diagnósticos seguidos, todos ok) mostrou, corrigido na v0.4.1:
- canal e trilhas do diagnóstico não eram apagados (105 objetos sobrando ao fechar, e o log terminava antes de "encerrado");
- libjuice usava `rand()` para as credenciais ICE (agora usa o gerador seguro do Switch, via `compat/patches/aplicar.py`);
- linhas de threads diferentes se misturavam no log; o log agora tem trava, linhas de até 8 KB e recomeça ao passar de 1 MB.
| B e depois + (fechar o app) | Fecha normalmente, sem travar | |

O log registra a oferta SDP completa e cada candidato, para comparar com o que o Xbox espera na parte 2.

### Fase 2b, parte 2: conexão com o Xbox (v0.4.3)

Na lista, escolha o console com **A**. Depois de "Sessao pronta", a tela mostra as etapas da conexão.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| 1. Oferta local | ok, com 1 ou mais candidatos locais | v0.4.1: ✅ oferta e candidato gerados; ❌ envio ao servidor falhou (ver abaixo) |
| 2. Resposta do console | ok | |
| 3. Candidatos de rede | ok, com candidatos do console aceitos | |
| 4. Conexão WebRTC | ok, "CONECTADO" | |
| 5. Handshake dos canais | ok; os quatro canais "aberto" | |
| Pacotes de vídeo | Contador subindo (o console está enviando imagem) | |
| Keepalive após 30 s | ok (HTTP 200): confirma a hipótese da fase 2a | |
| Deixar conectado 3+ minutos | Continua conectado, keepalives ok | |
| B | Desconecta e volta para a lista | |

**v0.4.1:** nas 3 tentativas, o `POST /sdp` falhou com "Couldn't connect to server" no mesmo instante, enquanto o mesmo servidor tinha respondido a `/play` e `/state` logo antes. Causa provável (hipótese): a libjuice pede 1 MB + 1 MB de buffer para o socket UDP e esgota o poço de memória de sockets da libnx (~2,25 MB por padrão), sem sobrar para o socket TCP do curl. **v0.4.2:** poço de sockets dobrado (`sb_efficiency` 8), 6 sessões BSD, buffers da libjuice reduzidos no Switch (512 KB + 128 KB) e o log das falhas do curl passa a trazer a causa do sistema.

**v0.4.2:** ✅ hipótese dos buffers confirmada: `/sdp` e `/ice` passaram (HTTP 202), o console respondeu com o SDP dele, anunciou o IP local (192.168.0.4:9002), e nas 3 tentativas **a conexão ICE e o handshake DTLS com o Xbox foram concluídos**. ❌ No passo seguinte, ao abrir o SCTP: erro `Could not bind usrsctp socket, errno=106` (EAFNOSUPPORT) numa tentativa, e o app fechou sozinho nas outras duas. Causa: a `struct sockaddr_conn` da usrsctp ficava no formato Linux enquanto a usrsctp e a libnx usam o formato BSD (`sa_len`), então AF_CONN era lido como "tamanho 123, família 0". **v0.4.3:** patch na `usrsctp.h` (formato BSD no Switch) e `HAVE_SCONN_LEN` em `compat.cmake`.

Se alguma etapa falhar, mande o `nxstream.log`: ele contém a oferta, a resposta SDP do console, todos os candidatos e as mensagens dos canais.

As próximas fases recebem seus checklists aqui quando começarem.

## Convenções

- **Branches:** `main` sempre compila. Trabalho de cada fase em `fase-N` e merge quando o teste no Switch passar.
- **Commits:** em português, no imperativo e curtos (ex.: "Adiciona login por código").
- **Código:** C, comentários em português, nomes de funções e variáveis em português ou inglês de forma consistente dentro de cada módulo.
- **Configuração da API:** endpoints, versões e tempos ficam em `source/api/config.h`, nunca espalhados pelo código.
