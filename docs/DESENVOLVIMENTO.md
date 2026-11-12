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

## Crash reports

Quando o app fecha com erro, o Atmosphère salva um relatório em `/atmosphere/crash_reports/` no cartão SD. Para traduzir os endereços em funções e linhas:

1. Baixe o artifact `nxstream-elf-para-crash-reports` do **mesmo** build do `.nro` que travou.
2. Com o devkitPro instalado: `sh scripts/traduzir-crash.sh nxstream.elf <relatorio>.log`

O script confere se o `Module Id` do relatório bate com o `.elf` antes de traduzir.

## Logs

- **No cartão SD:** `/switch/nxstream/nxstream.log`, com horário de cada evento. O arquivo cresce a cada uso; pode ser apagado a qualquer momento.
- **Ao vivo no PC (nxlink):** no menu do homebrew, aperte **Y** para entrar no modo de rede. No PC, com as ferramentas do devkitPro instaladas, rode:
  ```sh
  nxlink -s nxstream.nro
  ```
  O `.nro` é enviado ao Switch pela rede e as mensagens do app aparecem no terminal. Evita tirar o cartão SD a cada teste.

**Nunca são gravados no log:** tokens, senhas, código de login e qualquer dado da conta.

## Testes no PC

### Teste de limpeza da libdatachannel

`sh tests/limpeza/rodar.sh` (precisa de git, cmake, g++ e libssl-dev): compila a libdatachannel no mesmo commit do Switch, com os patches de `compat/patches/aplicar.py`, e verifica que `rtcCleanup()` termina depois de uma conexão igual à do app. Pegou o travamento da v0.5.1 antes do Switch. Roda no GitHub Actions quando os patches mudam.

### Interpretação de respostas

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

### Fase 2b, parte 2: conexão com o Xbox (v0.4.5)

Na lista, escolha o console com **A**. Depois de "Sessao pronta", a tela mostra as etapas da conexão.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| 1. Oferta local | ok, com 1 ou mais candidatos locais | ✅ v0.4.4 |
| 2. Resposta do console | ok | ✅ v0.4.3 |
| 3. Candidatos de rede | ok, com candidatos do console aceitos | ✅ v0.4.4: 11 do console, 1 da rede local usado |
| 4. Conexão WebRTC | ok, "CONECTADO" | ✅ v0.4.3, ~2,6 s |
| 5. Handshake dos canais | ok; os quatro canais "aberto" | ✅ v0.4.3 (o canal `chat` abre e o console fecha logo em seguida) |
| Pacotes de vídeo | Contador subindo (o console está enviando imagem) | ✅ v0.4.4 |
| Keepalive após 30 s | ok (HTTP 200): confirma a hipótese da fase 2a | ✅ v0.4.4: HTTP 200 `{"aliveSeconds":null,"reason":"None"}`; hipótese confirmada |
| Deixar conectado 3+ minutos | Continua conectado, keepalives ok | ✅ v0.4.4: 3 min 16 s, 6 keepalives ok |
| B | Desconecta e volta para a lista | ✅ v0.4.5: sem avisos no log |
| Fechar o app com + | Log termina com `encerrando (4/4...)` e `NXStream encerrado` | ✅ v0.4.5: sem `ERRO FATAL` |
| Bloquear o Switch conectado | A conexão cai (reconexão é a fase 4) | v0.4.4: caiu após 282 s de sleep, mas **a sessão sobreviveu** (keepalive HTTP 200 ao acordar): na fase 4 basta refazer só a conexão WebRTC |

**v0.4.1:** nas 3 tentativas, o `POST /sdp` falhou com "Couldn't connect to server" no mesmo instante, enquanto o mesmo servidor tinha respondido a `/play` e `/state` logo antes. Causa provável (hipótese): a libjuice pede 1 MB + 1 MB de buffer para o socket UDP e esgota o poço de memória de sockets da libnx (~2,25 MB por padrão), sem sobrar para o socket TCP do curl. **v0.4.2:** poço de sockets dobrado (`sb_efficiency` 8), 6 sessões BSD, buffers da libjuice reduzidos no Switch (512 KB + 128 KB) e o log das falhas do curl passa a trazer a causa do sistema.

**v0.4.2:** ✅ hipótese dos buffers confirmada: `/sdp` e `/ice` passaram (HTTP 202), o console respondeu com o SDP dele, anunciou o IP local (192.168.0.4:9002), e nas 3 tentativas **a conexão ICE e o handshake DTLS com o Xbox foram concluídos**. ❌ No passo seguinte, ao abrir o SCTP: erro `Could not bind usrsctp socket, errno=106` (EAFNOSUPPORT) numa tentativa, e o app fechou sozinho nas outras duas. Causa (confirmada pelo crash report do Atmosphère, traduzido com o `.elf` do mesmo build): a `struct sockaddr_conn` da usrsctp ficava no formato Linux enquanto a usrsctp (`HAVE_SA_LEN`) e a libnx usam o formato BSD, então AF_CONN (123) era lido como "tamanho 123, família 0". Em `usrsctp_register_address` → `sctp_add_addr_to_vrf`, o `memcpy(..., addr->sa_len)` copiava 123 bytes para um espaço de ~28: corrupção de memória, e o app caía na reserva de memória seguinte (`_malloc_r`, criando o `SctpTransport`). Quando não caía, o `bind` falhava com errno 106. **v0.4.3:** patch na `usrsctp.h` (formato BSD no Switch) e `HAVE_SCONN_LEN` em `compat.cmake`.

**v0.4.3:** ✅ **primeira conexão completa com o Xbox**: CONECTADO em ~2,6 s, SCTP aberto, os 4 canais abertos, `HandshakeAck` do console, primeiro pacote de vídeo e mensagens `titleinfo` / `showlayoutv2`. A conexão subiu antes mesmo de os candidatos do console chegarem. ❌ Logo depois, ao adicionar os candidatos do console, o app fechou sem tela de erro: a libnx fez DNS reverso dos IPs públicos derivados do Teredo (ignorando `NI_NUMERICHOST`) e o candidato IPv6 encerrou o app (provável exceção C++ não tratada, que não gera crash report). **v0.4.4:** só candidatos IPv4 de rede local são usados (o filtro foi testado com os 11 candidatos reais: sobra só `192.168.0.4:9002`), `getnameinfo()` corrigido na camada compat e exceções C++ não tratadas passam a ser registradas no log.

**v0.4.4:** ✅ todas as etapas, sem erros. Achados no log, corrigidos na v0.4.5: ao fechar o app, `rtcCleanup()` lançava `std::system_error` (pego pelo novo registro de exceções; era o motivo de os logs anteriores terminarem sem "encerrado"); ao desconectar, ~1000 avisos "dropping incoming message" porque as trilhas eram apagadas antes de fechar a conexão.

Se alguma etapa falhar, mande o `nxstream.log`: ele contém a oferta, a resposta SDP do console, todos os candidatos e as mensagens dos canais.

### Fase 2c.1: quadros de vídeo (v0.5.2)

Conecte ao console como na fase 2b. A tela de conectado agora mostra a seção **Video**.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Resolução | Aparece em poucos segundos (provavelmente 1280x720) | v0.5.0: 2560x1440 (o pedido de 720p em `dimensionschanged` foi ignorado). ✅ v0.5.1: **1280x720**, H.264 Main 3.2, ~4,5 Mbps, informando "android" no `X-MS-Device-Info` |
| Quadros por segundo | Estável, perto de 60/s (ou 30/s), mesmo com a tela do Xbox parada | ✅ v0.5.0: 59–61/s durante 2 min, 17–29 Mbps |
| Quadros-chave | Pelo menos 1, logo após conectar (sozinho ou depois de "pedido") | ✅ v0.5.0: primeiro em 0,2 s após conectar, sem pedido; depois a cada 30 s |
| Mexer no Xbox pelo controle dele (menu, um jogo) | Mbps sobe quando a imagem muda muito | ✅ v0.5.2: 4,1–5,5 Mbps; quadros-chave maiores (até ~125 KB) quando a imagem muda |
| Deixar 3+ minutos | Quadros por segundo continuam estáveis | ✅ v0.5.2: 9 min 45 s a 59–61 quadros/s, quadros-chave a cada 30 s, todos os keepalives ok |
| Fechar com + na tela conectada | Fecha sem travar e sem erro de sistema | ✅ v0.5.2: log termina com os 4 passos e `NXStream encerrado` |

O log registra uma linha de vídeo a cada 10 s, a resolução e os primeiros quadros-chave.

**v0.5.0:** fechar o app com **+** direto da tela conectada deu erro de sistema (o log terminou limpo, então o erro veio depois de fechar o log). Causa **confirmada pelo crash report** (traduzido com o `.elf` do mesmo build): a thread principal estava em `socketExit()` (`main.c`) enquanto uma thread da libjuice ainda estava em `recvfrom()` (falha ao acessar a tabela de sockets já liberada: `_socketGetFd`) e uma thread da libdatachannel ainda destruía a conexão ICE (`agent_destroy` → `conn_destroy`). Ou seja: sem `rtcCleanup()`, as threads da biblioteca ainda fechavam a conexão usando sockets quando o app fechou a rede. O `rtcCleanup()` tinha sido removido na v0.4.5 porque lançava `std::system_error`: o motivo é o `std::thread::detach()` da thread de limpeza, que falha na libnx. **v0.5.1:** patch na libdatachannel (`join()` no lugar de `detach()` no Switch) e `rtcCleanup()` de volta antes de fechar a rede; avisos "dropping incoming message" só contados.

**v0.5.1:** ❌ o app travou em "3/4: liberando a libdatachannel" e precisou ser fechado por fora. O `join()` causa um impasse: a thread de limpeza é criada dentro de um destrutor que segura a trava (ou roda numa thread do pool) de que a própria limpeza precisa. **Reproduzido no PC** com `tests/limpeza` (compila a libdatachannel com os mesmos patches): com `join()`, `rtcCleanup()` trava. **v0.5.2:** a thread de limpeza é mantida viva (objeto `std::thread` no heap, sem `detach()` nem `join()`); no mesmo teste, `rtcCleanup()` volta em 0,01 s em 3 de 3 rodadas. O teste roda no GitHub Actions sempre que os patches mudam.

### Fase 2c.2: decodificação (v0.6.0)

Conecte ao console. A tela de conectado ganha a seção **Decodificacao**.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Decodificador | "h264 + nvtegra" em verde (chip de vídeo do Switch) | ✅ v0.6.0: `h264 + nvtegra (FFmpeg 7.1)` |
| Decodificados por segundo | Igual aos recebidos (~60/s) | ✅ v0.6.0: 59–61/s, sempre igual aos recebidos |
| Tempo de decodificação | Médio bem abaixo de 16,7 ms (o limite para 60/s) | ✅ v0.6.0: médio 1,1–2,3 ms, máximo 6,3 ms |
| Erros e descartados | 0 erros; descartados só os poucos quadros antes do 1º quadro-chave | ⚠️ v0.6.0: 1 descartado (o quadro que chegou antes do 1º quadro-chave) e **1 erro**, no 1º quadro-chave (`AVERROR_UNKNOWN`); a partir do quadro seguinte, nenhum erro em 11 min |
| Imagem mudando muito (menu, jogo) | Continua ~60/s, sem erros | ✅ v0.6.0: 4,2–5,4 Mbps, quadros-chave de até ~127 KB, 60/s |
| Deixar 3+ minutos | Estável | ✅ v0.6.0: 10 min 51 s, 38 994 quadros decodificados, todos os keepalives ok |
| Fechar com + | Fecha sem travar (o log mostra "decod: encerrado") | ✅ v0.6.0: `decod: encerrado`, os 4 passos e `NXStream encerrado` |

**v0.6.0:** a 10 min 10 s de conexão, o console enviou três vezes `TransactionStart /streaming/sessionLifetimeManagement/serverInitiatedDisconnect` (junto com `titleinfo` e `showlayoutv2`), mas o vídeo e os keepalives continuaram até o app ser fechado 41 s depois. O app ainda não responde a essas transações. Nada foi feito no console naquele momento, então a causa provável é **inatividade**: o app ainda não tem controle conectado ao stream (só avisa e retira o gamepad no início). A fase 3 (controle) deve evitar isso; responder a essas transações fica anotado para a fase 4 (estabilidade).

### Fase 2c.3: imagem na tela (v0.7.0)

Conecte ao console. Assim que o primeiro quadro é decodificado, a tela de texto dá lugar ao vídeo do Xbox em tela cheia (ainda sem controle e sem som). Nos primeiros segundos aparece o aviso "Segure + e - por 1 s para abrir o menu".

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Imagem | A tela do Xbox aparece em tela cheia, sem estar de cabeça para baixo ou espelhada | ✅ v0.7.1. (v0.7.0: imagem invertida na vertical; no deko3d padrão o topo da tela é y = +1, como nos exemplos oficiais, e o shader assumia y = -1) |
| Cores | Naturais: preto bem preto, sem tons esverdeados ou rosados | |
| Movimento | Mexendo no Xbox pelo controle dele, a imagem acompanha suave, sem travadas regulares | ✅ v0.7.0: suave; 60 quadros/s recebidos e decodificados, 0 erros |
| Menu (+ e - por 1 s) | Abre o menu; cima/baixo escolhe, A confirma, B volta | ✅ v0.7.1 |
| Estatísticas (pelo menu) | O texto da tela de conexão aparece por cima do vídeo, legível; some ao escolher de novo | ✅ v0.7.1: ligadas e desligadas pelo menu |
| Desconectar (menu ou B) | Volta para a lista de consoles, em texto normal | ✅ v0.7.0 (B, duas vezes): `video: GPU encerrada` antes de fechar a conexão; reconectou em seguida sem problema |
| Fechar o app (menu) | Fecha sem travar; o log mostra "video: GPU encerrada" antes de "decod: encerrado" | |
| Deixar 3+ minutos | Imagem estável | ✅ v0.7.1: 2 min 44 s, 9 877 quadros, 0 erros; ❌ travada a cada 30 s (keepalive, ver abaixo) |
| Modo dock (se possível) | A imagem aparece na TV (em 720p ampliado nesta etapa) | |

O log registra quando a GPU assume a tela (`video: GPU pronta`, `main: video na tela`), o primeiro quadro mapeado na GPU e o padrão de cores detectado. Se a imagem não aparecer, a tela volta para o texto com a linha "Tela: ..." e o motivo fica no log.

Controle do Switch ainda não age no Xbox nesta etapa (é a fase 3).

**v0.7.1:** a imagem travava a cada 30 s: o keepalive era uma chamada HTTP na thread principal, que parava de desenhar até a resposta chegar. **v0.7.2:** o keepalive roda numa thread própria (o módulo HTTP cria uma conexão por requisição, então é seguro); encerrar a sessão espera um keepalive em andamento terminar.

| Teste (v0.7.2) | Resultado esperado | Resultado obtido |
|---|---|---|
| Deixar 2+ minutos com estatísticas ligadas | Nenhuma travada a cada 30 s; "Keepalive: ok" continua atualizando | ✅ v0.7.2: 8 min 45 s sem travadas, 20 keepalives ok, 0 erros |

**v0.7.2:** no OLED o preto fica claro demais. O app já converte a faixa limitada (16 = preto), então o preto do vídeo do Xbox provavelmente chega acima de 16. **v0.7.3** (diagnóstico): o log mede os níveis de luma do quadro a cada 10 s (`video: luma min ..., 1% ..., mediana ...`) e o menu ganha **Nivel de preto: 16 / 24 / 32** para comparar na tela.

| Teste (v0.7.3) | Resultado esperado | Resultado obtido |
|---|---|---|
| Tela escura do Xbox (fundo preto) por ~20 s | O log mostra os níveis reais do preto | |
| Menu > Nivel de preto (16, 24, 32) | Dizer qual deixa o preto realmente preto sem apagar os detalhes escuros | |

As próximas fases recebem seus checklists aqui quando começarem.

## Convenções

- **Branches:** `main` sempre compila. Trabalho de cada fase em `fase-N` e merge quando o teste no Switch passar.
- **Commits:** em português, no imperativo e curtos (ex.: "Adiciona login por código").
- **Código:** C, comentários em português, nomes de funções e variáveis em português ou inglês de forma consistente dentro de cada módulo.
- **Configuração da API:** endpoints, versões e tempos ficam em `source/api/config.h`, nunca espalhados pelo código.
