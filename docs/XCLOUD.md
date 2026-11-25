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

### Detalhes por fase

**X0 – Acesso.** O login atual já gera o token do Xbox Live que vale para as duas ofertas; muda só a chamada ao gssv (`xgpuweb.gssv-play-prod.xboxlive.com/v2/login/user`). Os dois tokens (casa e nuvem) passam a viver juntos, com renovação própria (4 h). O nome dos jogos vem do catálogo (`/v3/products` com o mercado BR e idioma pt-BR).

**X1 – Sessão.** O caminho de conexão é o mesmo do Remote Play; o que muda:
- corpo do `play`: `titleId` preenchido, `serverId` vazio;
- estado `WaitingForResources`: mostrar posição/tempo da fila em vez de "Preparando o console";
- filtro de candidatos: no xCloud, aceitar os endereços públicos do servidor (IPv4; IPv6 se a rede tiver);
- NAT: o Switch costuma estar atrás de NAT (no caso do usuário, CGNAT do provedor). O servidor da Microsoft tem IP público, então em princípio o Switch alcança o servidor sem servidor STUN; se falhar, adicionar STUN na libjuice.

**X2 – Qualidade.** Hoje a recepção de vídeo pede quadro-chave quando algo se perde, o que na internet causaria travadas frequentes. Plano: medir primeiro (perda e atraso por segundo no log e nas estatísticas), depois implementar NACK do lado de quem recebe (a libdatachannel não traz pronto) e um buffer de reordenação de algumas dezenas de milissegundos. Em 720p (decisão abaixo) a taxa deve ficar perto da do Remote Play (~5–8 Mbps), e o decodificador já é folgado (2 ms por quadro).

**X3 – Interface.** Capas exigem baixar e decodificar JPEG (habilitar JPEG no stb_image, hoje só PNG), reduzir para o tamanho do cartão e guardar no SD para as próximas aberturas. A grade carrega as imagens em segundo plano para a navegação não travar.

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
