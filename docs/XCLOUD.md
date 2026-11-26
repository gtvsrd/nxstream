# Roadmap: xCloud (jogos na nuvem)

Objetivo: jogar do **Xbox Cloud Gaming (xCloud)** no Switch, além do Remote Play do console próprio, reaproveitando ao máximo o que já funciona: login, WebRTC, decodificação, áudio, controle, interface e reconexão.

Este documento é o plano. As decisões em aberto estão no fim e serão fechadas com o usuário antes de cada fase.

## O que muda em relação ao Remote Play

O xCloud usa **a mesma família de API e o mesmo protocolo de streaming** do Remote Play (o NXStream já fala `/v5/sessions/home/...`; o xCloud é `/v5/sessions/cloud/...`). As diferenças estão em volta:

| Parte | Remote Play (hoje) | xCloud |
|---|---|---|
| Acesso | gsToken com oferta `xhome` | gsToken com oferta `xgpuweb` (Game Pass Ultimate); se não houver, `xgpuwebf2p` (só jogos gratuitos, como Fortnite) |
| Servidor | Região única devolvida no login | Lista de regiões no login (`offeringSettings.regions`, uma marcada como padrão); escolher a mais próxima (Brasil) |
| O que se escolhe | Um console (`/v6/servers/home`) | Um jogo: `/v2/titles` (jogos liberados para a conta), `/v2/titles/mru` (recentes) |
| Nomes e capas | Não há | Catálogo do Game Pass (`catalog.gamepass.com`): nomes, descrições e imagens dos jogos |
| Criar sessão | `serverId` = console | `titleId` = jogo |
| Espera | Console acordando (~9 s) | Fila por servidor (`WaitingForResources`), com tempo estimado em `/v1/waittime/{titleId}` |
| Rede | Mesma casa: só candidatos IPv4 privados (filtro atual) | Internet: candidatos públicos do servidor da Microsoft; o filtro precisa mudar |
| Qualidade | LAN quase sem perda | Perda de pacotes e variação de atraso pela internet: precisa de recuperação (NACK, buffer) |
| Reconexão | Sempre sessão nova (a antiga não aceita outra conexão) | A investigar: uma sessão nova pode **reiniciar o jogo**; verificar se dá para retomar a ativa (`/v5/sessions/cloud/active`) |

Referência aberta usada para confirmar endpoints: o Greenlight (MIT), cliente de xCloud e Remote Play para PC. Nenhum código dele é copiado; só a forma das chamadas, que é a mesma API pública que o NXStream já usa.

## Fases

Como no resto do projeto, cada fase termina com um `.nro` testado no Switch antes de seguir.

| Fase | Entrega | Critério de aceite no Switch |
|---|---|---|
| **X0** | **Acesso e lista em texto.** Pedir o gsToken `xgpuweb` (com recuo para `xgpuwebf2p`), ler as regiões, listar os jogos de `/v2/titles` com o nome vindo do catálogo. Diagnóstico no log: oferta obtida, regiões, quantidade de jogos | A lista dos jogos da conta aparece; o log mostra a região escolhida |
| **X1** | **Primeiro jogo na tela.** Sessão `cloud/play` com o `titleId`, acompanhar fila e estados, `/connect`, troca de SDP/ICE aceitando candidatos públicos, vídeo, som e controle | Um jogo abre e é jogável por alguns minutos |
| **X2** | **Qualidade pela internet.** Medir perda e variação de atraso; pedir retransmissão de pacotes perdidos (NACK), buffer curto de reordenação, pedido de quadro-chave mais esperto; estatísticas com perda e latência | 30 min de jogo sem travadas longas; estatísticas mostrando a perda |
| **X3** | **Interface de nuvem.** Tela inicial com "Meu Xbox" e "Nuvem"; grade de jogos com capas (download de imagens JPEG com cache no SD), recentes, busca simples, tela de fila com tempo estimado | Navegar e abrir jogos só com o controle, capas carregando sem travar |
| **X4** | **Reconexão e sessões.** Comportamento depois de sleep, HOME e queda de rede: retomar a sessão ativa quando possível, sem reiniciar o jogo; encerrar a sessão ao sair | Bloquear o Switch por alguns minutos e voltar ao mesmo ponto do jogo |
| **X5** | **Acabamento.** Escolha de região, idioma/mercado do catálogo, configurações de qualidade, documentação e telas | Uso no dia a dia |

### Situação

| Fase | Versão | Estado |
|---|---|---|
| X0 | v0.11.0 / v0.11.1 | Funcionou no Switch (25/11); v0.11.1 deixa a carga mais rápida |
| X1 | v0.12.0 | Funcionou no Switch (25/11): Gears Tactics abriu e conectou na 1ª |
| X2 | v0.12.1 / v0.12.2 | Funcionou (25/11): 11 min em 720p, 0 erros de decodificação |
| X3 | v0.13.0 | Em teste no Switch |

### Detalhes por fase

**X0 – Acesso.** O login atual já gera o token do Xbox Live que vale para as duas ofertas; muda só a chamada ao gssv (`xgpuweb.gssv-play-prod.xboxlive.com/v2/login/user`). Os dois tokens (casa e nuvem) passam a viver juntos, com renovação própria (4 h). O nome dos jogos vem do catálogo (`/v3/products` com o mercado BR e idioma pt-BR).

**X0 – Como ficou (v0.11.0).**
- `auth_nuvem()` (`auth.c`): mesmo user token e XSTS do console, login no gssv com `xgpuweb`; se recusar, `xgpuwebf2p`. O gsToken da nuvem fica separado do console (`g_nuvem` em `main.c`) e é renovado depois de ~4 h ou ao receber 401.
- `xnuvem_listar()` (`api/xnuvem.c`): `/v2/titles` (repete se vier 204, como o Greenlight), `/v2/titles/mru`, só os jogos com direito (`hasEntitlement` ou `isFreeInStore`), nomes e editoras do catálogo em lotes de 100 (mercado BR, pt-BR). Recentes primeiro, depois por nome.
- Tela: **R** na lista de consoles abre "Jogos na nuvem" (`docs/telas/10_nuvem.png`): 4 jogos por vez, direcional para cima/baixo (segurar repete) e para os lados (pula 4), **A** mostra os IDs do jogo, **Y** recarrega, **B** volta.
- Log: `auth: nuvem: oferta ..., regiao ... (de: ...)`, `xnuvem: /v2/titles: N jogo(s)`, `com direito`, `catalogo: N lote(s)`, `lista pronta em X s` e os 5 primeiros jogos.

| Teste (v0.11.0) | Resultado esperado | Resultado obtido |
|---|---|---|
| R na lista de consoles | "Acessando o Xbox Cloud Gaming...", depois "Carregando os jogos da nuvem..." e a lista | |
| Cabeçalho da lista | "Game Pass Ultimate" e a região (Brasil) | |
| Nomes | Jogos com nome e editora, recentes no topo com bolinha verde | |
| Navegação | Segurar o direcional rola rápido; para os lados pula 4 | |
| B e R de novo | Volta aos consoles; a lista reabre na hora (fica na memória) | |

**Resultado da v0.11.0 (log de 25/11):** funcionou na 1ª vez. Oferta `xgpuweb` (Ultimate), região padrão **BRAZILSOUTH** entre 19 (Austrália, EUA, Europa, Japão, Coreia, Índia, Chile, México...). `/v2/titles` trouxe 2869 jogos, 622 liberados para a conta, todos com nome, 24 recentes (Gears Tactics, FF VII Remake, Minecraft Dungeons II, Balatro...). Ponto fraco: **20 s** para a lista aparecer: 1,3 MB da lista em 5,9 s e **6,7 MB** do catálogo em 13,9 s (7 lotes de ~1 MB; cada jogo traz descrição, capturas de tela, idiomas...).

**v0.11.1:**
- Respostas comprimidas (gzip) em todas as requisições: o catálogo cai ~4x (medido: 21 KB → 5,6 KB para 2 jogos). O log mostra o tamanho e quanto veio pela rede.
- Nomes guardados no SD (`/switch/NXStream/nuvem-nomes.json`, ~50 KB): da 2ª vez em diante, o catálogo só é consultado para jogos novos.

| Teste (v0.11.1) | Resultado esperado | Resultado obtido |
|---|---|---|
| 1ª abertura da nuvem | Lista em bem menos de 20 s; log com `pela rede` menor que o tamanho | |
| 2ª abertura (fechar o app e abrir de novo) | `todos os nomes ja estavam no cache`, lista em poucos segundos | Sim: 5,7 s |

**Resultado da v0.11.1 (log de 25/11):** o cache funcionou: da 2ª vez a lista ficou pronta em **5,7 s** (antes 20 s). O gzip funcionou no catálogo (6,7 MB → 1,2 MB pela rede), mas a 1ª carga levou 21,8 s mesmo assim: quem demora é o servidor, ~3 s por lote. Já `/v2/titles` (1,3 MB, 5,3 s) vem sem compressão: o servidor da nuvem não usa gzip. Fica para a X3 mostrar na hora a última lista salva e atualizar por trás.

**X1 – Como ficou (v0.12.0).**
- Na lista "Jogos na nuvem", **A = Jogar** (X mostra os detalhes). O jogo abre pelo mesmo fluxo da sessão do console (`abrir_sessao` e `passo_conectar` em `main.c`), com `g_na_nuvem` ligado: o gsToken usado é o da nuvem (`g_nuvem`) e o `g_console` vira só o rótulo das telas (nome do jogo, "Xbox Cloud Gaming").
- `xapi_sessao_iniciar(..., titleId)`: `POST /v5/sessions/cloud/play` com `titleId` e `serverId` vazio. Estados iguais aos do console, mais a fila (`WaitingForResources`): a estimativa vem de `/v1/waittime/{titleId}` uma vez e aparece na tela ("Espera estimada: N min"). Limite de preparo de 15 min (console: 2 min).
- `ReadyToConnect`: o mesmo token de transferência do console (o Greenlight usa o mesmo `lpt` nos dois).
- Candidatos: `xapi_filtrar_ipv4` aceita endereços públicos (servidores do Azure) e continua recusando IPv6. Sem STUN, como o Greenlight: a checagem sai do Switch para o endereço público do servidor e atravessa o roteador.
- Renovação: `obter_streaming_novo` renova o token da nuvem quando a sessão é da nuvem; cada token guarda quando foi obtido (`AuthStreaming.obtido_em`).
- Fim da sessão: B, Desconectar ou fechar o app encerram a sessão (o servidor é liberado). Se o servidor fechar (tempo sem jogar, saída do jogo), aparece "A sessão na nuvem foi encerrada" com A = Jogar de novo. **Uma queda de rede ainda abre uma sessão nova, que recomeça o jogo**: retomar a mesma é a fase X4.
- Sem NACK nem buffer de reordenação ainda (fase X2): perda de pacotes na internet aparece como travadas curtas até o próximo quadro-chave.

| Teste (v0.12.0) | Resultado esperado | Resultado obtido |
|---|---|---|
| A num jogo da lista | "Preparando o jogo na nuvem" (ou "Na fila para jogar"), depois o jogo | |
| Jogar uns minutos | Imagem e som; controle respondendo; estatísticas (+ e − juntos) | |
| Menu › Desconectar | Volta para a lista de jogos, no mesmo jogo | |
| Log | `jogo na nuvem escolhido`, `sessao criada`, estados, `candidato(s) recebido(s), N usado(s) (IPv4)`, `conectado ao console` | Sim |

**Resultado da v0.12.0 (log de 25/11, Gears Tactics):** sessão pronta em 14,5 s (sem fila; `ReadyToConnect` → token → `Provisioning` → `Provisioned`), conexão em 1,6 s com 1 candidato IPv4 público do servidor, sem STUN. Som perfeito (0 pacotes perdidos em 4 min). O vídeo veio em **1920x1080** (H.264 Main 4.2) e teve **3751 erros de decodificação** em 4 min: nenhum com a tela parada (0 Mbps) e até 25 por segundo a 10 Mbps. Causa: o depacketizer da libdatachannel entrega o quadro assim que chega o pacote com o marcador de fim, sem esperar pacotes atrasados nem pedir os perdidos. Pela internet, pacotes fora de ordem ou perdidos viram quadros quebrados, e cada erro estragava os quadros seguintes até o próximo quadro-chave (pedido 1 vez por segundo).

**v0.12.1 (adianta a parte principal da X2):**
- `net/montador.c`: montador de quadros próprio. Guarda os pacotes por número de sequência, só entrega um quadro completo e na ordem, pede os que faltam com **RTCP NACK** (o servidor anuncia `nack` no SDP; reenvio no mesmo fluxo, sem RTX) até 3 vezes, e desiste depois de 160 ms: descarta o quadro, pede um quadro-chave e não entrega nada quebrado ao decodificador. 35 verificações em `tests/test_montador.c` (fora de ordem, perda com e sem recuperação, virada da sequência, STAP-A/FU-A).
- libdatachannel: ajuste de uma linha (`compat/patches/aplicar.py`) para enviar RTCP numa trilha só de recepção quando há handler.
- **720p na nuvem:** o `play` informa `osName: android` (o mesmo truque do Better xCloud); com `windows` o xCloud manda 1080p.
- Log a cada 10 s: `rede N buracos, N recuperados, N fora de ordem, N quadros perdidos`; e um resumo ao desconectar.

| Teste (v0.12.1) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir um jogo da nuvem | `video 1280x720` no log | Sim (Minecraft Dungeons II) |
| Jogar uns minutos com movimento | Erros de decodificação perto de 0; `recuperados` subindo quando houver buracos | **0 erros** em 11 min (antes 3751 em 4 min) |
| Console de casa | Igual a antes (o montador também vale para o console) | Sim: 0 erros, 0 buracos |

**Resultado da v0.12.1 (log de 25/11):** 11 min de Minecraft Dungeons II a 60 quadros/s, 4–6 Mbps, **0 erros de decodificação e 0 quadros perdidos**. A causa principal era a ordem: **27% dos pacotes de vídeo** (89.915 de 331.982) chegaram fora de ordem, todos arrumados pelo montador. Perdas de verdade foram poucas: 66 NACKs, 67 pacotes reenviados pelo servidor e recuperados. No console de casa, 0 fora de ordem e 0 erros, como antes. Ao desligar o Xbox, o motivo veio no aviso: `KickForServerShutdown`.

**v0.12.2:** linha **Rede** no cartão de estatísticas (% fora de ordem, pacotes reenviados, quadros perdidos) e a tela "O Xbox foi desligado" quando o motivo é `KickForServerShutdown`. Com isso a X2 está completa; falta só a latência, que o app ainda não mede.

**X1 – Sessão.** O caminho de conexão é o mesmo do Remote Play; o que muda:
- corpo do `play`: `titleId` preenchido, `serverId` vazio;
- estado `WaitingForResources`: mostrar posição/tempo da fila em vez de "Preparando o console";
- filtro de candidatos: no xCloud, aceitar os endereços públicos do servidor (IPv4; IPv6 se a rede tiver);
- NAT: o Switch costuma estar atrás de NAT (no caso do usuário, CGNAT do provedor). O servidor da Microsoft tem IP público, então em princípio o Switch alcança o servidor sem servidor STUN; se falhar, adicionar STUN na libjuice.

**X2 – Qualidade.** Hoje a recepção de vídeo pede quadro-chave quando algo se perde, o que na internet causaria travadas frequentes. Plano: medir primeiro (perda e atraso por segundo no log e nas estatísticas), depois implementar NACK do lado de quem recebe (a libdatachannel não traz pronto) e um buffer de reordenação de algumas dezenas de milissegundos. Em 720p (decisão abaixo) a taxa deve ficar perto da do Remote Play (~5–8 Mbps), e o decodificador já é folgado (2 ms por quadro).

**X3 – Interface.** Capas exigem baixar e decodificar JPEG (habilitar JPEG no stb_image, hoje só PNG), reduzir para o tamanho do cartão e guardar no SD para as próximas aberturas. A grade carrega as imagens em segundo plano para a navegação não travar.

**X3 – Como ficou (v0.13.0).** Decisões do usuário (25/11): faixa de recentes + grade, busca pelo teclado do Switch, abas no topo com L/R.
- **Abas** "Consoles" e "Nuvem" no cabeçalho, com os botões L e R; L ou R troca. O app abre na última aba usada (`conta.json`, `"aba"`), como decidido no roadmap.
- **Tela da nuvem** (`docs/telas/10_nuvem.png`): faixa "Jogados recentemente" (os 24 de `/v2/titles/mru`) e grade "Todos os jogos" em 7 colunas, por nome. Capas de 140x210, borda verde no selecionado, rolagem animada; nome, editora e "jogado recentemente" embaixo. Direcional com repetição ao segurar; da faixa para baixo vai à grade na mesma coluna.
- **Capas** (`ui/capas.c`): o catálogo traz a URL do pôster (`Image_Poster`, ou `Image_Tile`); o servidor de imagens da loja entrega já em 140x210 (`?w=140&h=210&format=jpg`, ~15 KB). Duas threads baixam as visíveis primeiro (e a próxima linha), guardam em `/switch/NXStream/capas/<productId>.jpg` e decodificam (stb_image com JPEG). 64 capas na memória. Sem capa ainda, o cartão mostra o nome.
- **Busca:** Y abre o teclado do Switch; a grade mostra só os jogos com o texto, sem diferenciar maiúsculas nem acentos ("pokemon" acha "Pokémon"). B limpa.
- **Outros botões:** A joga, X detalhes, − recarrega a lista, + fecha.
- O cache de nomes passou para a versão 2 (com a capa): na 1ª abertura da v0.13.0 o catálogo é consultado de novo (~20 s, uma vez).
- Fila: continua a tela de conexão com "Na fila para jogar" e a espera estimada (X1).

| Teste (v0.13.0) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir a aba Nuvem (L/R) | Faixa de recentes e grade; capas aparecendo em poucos segundos | |
| Fechar e abrir o app | Abre direto na aba Nuvem; capas já vêm do SD | |
| Rolar a grade segurando o direcional | Rolagem suave, sem travar; capas chegando | |
| Y e buscar "minecraft" | Só os Minecraft; B volta a todos | |
| A num jogo | Abre o jogo como antes; ao sair, volta no mesmo jogo | |

**X4 – Reconexão.** O ponto mais incerto. Investigar se uma sessão de nuvem continua viva depois da queda e se aceita nova conexão (no Remote Play não aceita), ou se `/v5/sessions/cloud/active` permite retomar o mesmo jogo. Se não der para retomar, avisar o usuário antes de abrir sessão nova, porque o jogo pode recomeçar do último salvamento.

## Riscos

| Risco | Impacto | Mitigação |
|---|---|---|
| Conta sem Game Pass Ultimate | Só jogos gratuitos (oferta `xgpuwebf2p`) | Detectar a oferta e avisar na tela; testar com um jogo gratuito |
| Perda de pacotes pela internet (Wi-Fi do Switch) | Travadas e artefatos | Fase X2 inteira dedicada; estatísticas para diagnosticar |
| Reconexão reinicia o jogo | Perda de progresso | Investigar na X4; avisar antes de abrir sessão nova |
| Mudança de API pela Microsoft | Para de funcionar | Endpoints isolados em `config.h`, respostas inesperadas no log (como no Remote Play) |
| Termos de uso da Microsoft | Bloqueio do acesso não oficial | Mesmo risco do Remote Play e do Greenlight; uso só com a conta do próprio usuário |

## Decisões tomadas (25/11)

| Decisão | Escolha |
|---|---|
| Conta de teste | Tem **Game Pass Ultimate**: testes com o catálogo completo (oferta `xgpuweb`); o recuo para `xgpuwebf2p` fica para contas sem assinatura |
| Tela inicial | Abre no **último modo usado** (Meu Xbox ou Nuvem), com troca por uma aba |
| Resolução | **720p sempre**, também na TV: menos dados e mais estabilidade pela internet |
| Ordem | **Qualidade do streaming (X2) antes da interface com capas (X3)** |
