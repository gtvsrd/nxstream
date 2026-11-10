# Desenvolvimento

## Build

### Automático (GitHub Actions)

Todo push dispara o workflow [`build.yml`](../.github/workflows/build.yml), que compila dentro do container oficial `devkitpro/devkita64` e publica o `.nro`.

Para baixar: aba **Actions** → execução mais recente → seção **Artifacts** → `nxstream-nro` (um `.zip` com o `nxstream.nro`). É preciso estar logado no GitHub.

### Local (opcional)

1. Instale o [devkitPro](https://devkitpro.org/wiki/Getting_Started) com o grupo `switch-dev`.
2. Na pasta do projeto: `make`
3. Para limpar: `make clean`

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
**v0.0.2:** ativa os avisos do sistema (`AppletFocusHandlingMode_SuspendHomeSleepNotify`) e adiciona detector de congelamento. ✅ Todos os avisos chegam no sleep e no HOME, e a rede volta instantaneamente ao acordar.

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

### Fase 2a: sessão de streaming (v0.2.0)

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| A em um console **ligado** | Estados passam por "Preparando..." até "Sessao pronta!" | |
| A em um console **em repouso** | Mesmo resultado; o console acorda só no modo de streaming | |
| Tempo até ficar pronta | Mostrado na tela ("preparada em X s") | |
| Deixar a sessão aberta 3+ minutos | Keepalives com "ok" e nenhuma falha | |
| Dormir o Switch 30 s com a sessão aberta e acordar | Linha "Voltou de X s suspenso: sessao continua viva" (ou "caiu") | |
| Dormir o Switch 5+ minutos e acordar | Idem: descobre por quanto tempo a sessão sobrevive sem keepalive | |
| B na sessão | Volta para a lista; a sessão é encerrada | |

Os dois testes de sleep não têm resposta "certa": servem para medir quanto tempo o servidor mantém a sessão sem keepalive. Isso define a estratégia de reconexão da fase 4.

As próximas fases recebem seus checklists aqui quando começarem.

## Convenções

- **Branches:** `main` sempre compila. Trabalho de cada fase em `fase-N` e merge quando o teste no Switch passar.
- **Commits:** em português, no imperativo e curtos (ex.: "Adiciona login por código").
- **Código:** C, comentários em português, nomes de funções e variáveis em português ou inglês de forma consistente dentro de cada módulo.
- **Configuração da API:** endpoints, versões e tempos ficam em `source/api/config.h`, nunca espalhados pelo código.
