# HANDOFF — Estado do projeto e próximos passos

> Documento para o próximo agente (ou pessoa) continuar o trabalho. Leia junto com
> [AGENTS.md](AGENTS.md), que define as regras de git flow, commits e código.
> Atualizado em 2026-11-19, em `develop` (commit de merge mais recente no GitHub: `origin/develop`).

---

## 1. Contexto em uma página

**project_sinos** — nó LoRaWAN que mede a altura do Rio dos Sinos (RS) com um sensor
ultrassônico. Hardware do nó:

| Item                | Detalhe                                                                 |
|---------------------|-------------------------------------------------------------------------|
| MCU                 | STM32U585CIU6 (Cortex-M33), projeto STM32CubeMX + CMake, sem RTOS        |
| Rádio               | Seeed Wio-E5 (STM32WLE5), firmware AT **4.0.11**, DevEUI `2C:F7:F1:20:81:50:5D:50` |
| UART MCU ↔ Wio-E5   | USART1, PA9 (TX) / PA10 (RX), 9600 8N1, pull-up nos dois pinos, RX por interrupção |
| Reset do Wio-E5     | PA0 (`reset_wioe5`), open-drain, inicia em **nível baixo** (MCU segura o módulo em reset até o driver liberar) |
| Relógio de baixo consumo | LSE 32,768 kHz (PC14/PC15), RTC com *wakeup timer* interno         |
| Debug               | ST-LINK V2 (FW V2J48S7), SWD, **access port 0**                          |
| Região LoRaWAN      | AU915, sub-banda 2 (canais 8–15 + 65), OTAA, Classe A                    |

**O que está pronto e testado:**

- Driver do Wio-E5 (`firmware/node_device/node/Lib/wioe5/`): especificado, testado no
  host e validado na placa até o join (sem rede).
- Ciclo da aplicação (`node/App/`): setup → join → envio periódico → sleep do módulo →
  Stop 2 do MCU, com retentativas e reset automático do módulo.
- 157 testes no host (Unity), todos passando; teste de mutação no driver (24 defeitos
  injetados, todos detectados).
- Firmware Debug e Release compilam sem warnings (Release: ~28,7 KB flash, ~4,3 KB RAM).
- Debug pelo VS Code (extensão STM32Cube) configurado; trace da UART em builds Debug.
- CI no GitHub Actions verde (testes + build Debug/Release); Release com `.bin` em tags
  `v*` escrito, aguardando a primeira release (6.7.1).

**O que NÃO foi verificado na placa** (depende de um join com sucesso): envio de
uplink, downlink, sleep/wake do Wio-E5, entrada/saída do Stop 2, consumo de corrente.

---

## 2. Mapa do repositório

```
project_sinos/
├── AGENTS.md                          regras do repo (LEIA)
├── HANDOFF.md                         este arquivo
├── firmware/node_device/
│   ├── docs/wioe5/driver_spec.md      especificação do driver + integração (§1–§18)
│   ├── docs/wioe5/test_plan.md        plano de testes, situação dos testes
│   ├── node/                          projeto CubeMX (abrir esta pasta no VS Code)
│   │   ├── node.ioc                   configuração CubeMX (alterar só pelo CubeMX)
│   │   ├── Core/Src/main.c            só chama app_node_init/app_node_process (USER CODE)
│   │   ├── App/app_lorawan.c/h        ciclo e política de retentativa (sem HAL, testado)
│   │   ├── App/app_node.c/h           cola STM32: instâncias, callbacks HAL, payload, Stop 2
│   │   ├── Lib/wioe5/inc|src          driver (sem HAL)
│   │   ├── Lib/wioe5/port             porta STM32 (único arquivo do driver com HAL)
│   │   ├── .vscode/                   launch.json (debug ST-LINK), settings.json e
│   │   │                              c_cpp_properties.json (build/clangd pela extensão ST)
│   │   ├── .settings/                 versões travadas das ferramentas da extensão ST
│   │   │                              (bundles-lock): todos compilam com as mesmas versões
│   │   └── .clangd                    aponta o autocompletar para build/Debug
│   └── tests/                         testes no host (Unity, fake port, modem simulado)
│       └── fixtures/                  capturas reais de bancada
└── firmware/gateway/                  vazio
```

Documentos de referência: especificação AT do Wio-E5 (PDF da Seeed, "LoRa-E5 AT Command
Specification V1.0"), resumida na §5 da `driver_spec.md`.

---

## 3. Ambiente desta máquina (Windows 11)

| Ferramenta           | Onde / como                                                                |
|----------------------|----------------------------------------------------------------------------|
| gcc do host          | WinLibs (`winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`), no PATH |
| CMake 4.4, Ninja 1.13| winget, no PATH                                                            |
| arm-none-eabi-gcc 14.3 | `%LOCALAPPDATA%\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin` (não está no PATH do shell; a extensão ST injeta) |
| GDB                  | `%LOCALAPPDATA%\stm32cube\bundles\gnu-gdb-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gdb.exe` |
| ST-LINK GDB server   | `%LOCALAPPDATA%\stm32cube\bundles\stlink-gdbserver\7.14.0+st.2\bin\ST-LINK_gdbserver.exe` |
| CubeProgrammer CLI   | `%LOCALAPPDATA%\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe` |
| STM32CubeIDE 1.17    | `C:\ST\STM32CubeIDE_1.17.0` (tem outra cópia do gcc ARM nos plugins)       |
| GitHub CLI (`gh`)    | **não instalado**; push via `git push` funciona (credenciais já configuradas) |

### Comandos

```bash
# testes do host (em firmware/node_device/tests) — PATH do shell precisa do gcc
cmake --preset host && cmake --build --preset host && ctest --preset host

# firmware (em firmware/node_device/node) — pôr o gcc ARM no PATH antes
cmake --preset Debug && cmake --build --preset Debug
cmake --preset Release && cmake --build --preset Release
```

Debug na placa pela linha de comando: ver AGENTS.md §4.1. Resumo:
`ST-LINK_gdbserver -e -p 61234 -d -s -k -m 0 -cp <programmer\bin>` e
`arm-none-eabi-gdb build/Debug/node.elf` → `target extended-remote :61234`.
**Encerre o servidor depois** — a sonda só aceita um cliente (o VS Code falha se ele ficar aberto).

---

## 4. Armadilhas já encontradas (não repita)

1. **Arquivo travado pelo CubeMX.** Com o STM32CubeMX aberto, `cmake/stm32cubemx/CMakeLists.txt`
   fica bloqueado e `git switch`/`stash`/`merge` falham no meio ("unable to unlink").
   Peça ao usuário para fechar o CubeMX antes de trocar de branch depois de uma regeneração.
2. **CubeMX liga o RTC wakeup timer no init** (contador 0, RTCCLK/16 → interrupção a cada
   ~0,5 ms). O `USER CODE RTC_Init 2` em `main.c` desliga. Se regenerar, confirme que o
   trecho continua lá.
3. **ST-LINK no access port 0** (`-m 0`). Com `-m 1` dá "No device found on target".
4. **Breakpoints na recepção da UART** (`handle_line`, `drain_rx`, callbacks) fazem perder
   bytes a 9600 baud (1 byte/ms) → `uart_errors` sobe. Não é bug do driver.
5. **`interrupt` assíncrono do gdb em modo batch não funciona** com esse servidor; para ler o
   estado, pare em breakpoints de pontos naturais (ex.: `wioe5_start_join`) ou conecte de novo
   (o gdb para o alvo ao conectar).
6. **`uint8_t[]` aparecem como "lixo" no Live Watch** (ex.: `dev_eui`). São bytes binários;
   use `-exec p/x s_wio.info.dev_eui` ou `(char*)` para textos.
7. **Heredocs grandes no Bash do Windows** às vezes falham ("unexpected EOF"); use a
   ferramenta de escrita de arquivo.
8. **`.gitignore` e subpastas:** uma regra com barra no meio (`.vscode/*`) só vale para a raiz
   do repositório. Para valer em `node/.vscode`, use `**/.vscode/*`. A extensão ST também
   cria `node/.gitignore` com `!.settings`, que libera a pasta `.settings/` só naquele projeto.
9. **A `main` só aceita PR** (ruleset do GitHub, AGENTS.md §2.7): merge local + push na `main`
   é recusado. O `develop` não exige checks porque os merges de feature são locais e o
   commit de merge ainda não teria CI. Se renomear um job do `ci.yml`, a ruleset da `main`
   precisa ser atualizada, senão nenhum PR consegue ser mergeado.
10. Arquivos gerados pelo CubeMX são CRLF (`.gitattributes` mantém `*.ioc` CRLF); os nossos
   são LF no repo. Avisos "LF will be replaced by CRLF" são normais.

---

## 5. Decisões já tomadas (não reabrir sem o usuário)

- Driver não-bloqueante (`wioe5_process()` no laço), sem malloc/printf/float, porta abstrata.
- OTAA, Classe A, AU915 sub-banda 2, uplink **não confirmado**, payload ≤ 11 bytes.
- Chaves (AppEUI/AppKey) **não** ficam no firmware nem no repo: gravadas uma vez no módulo
  pelo terminal (`provision = NULL`).
- Política de retentativa na aplicação (`app_lorawan`), não no driver.
- Git flow com `main` protegida: releases e hotfixes entram por **pull request** (merge commit),
  features entram no `develop` por merge local `--no-ff` (AGENTS.md §2.5 e §2.7).
- MCU em **Stop 2** entre medições (RAM mantida; setup/join só no cold boot).
- MCU segura o Wio-E5 em reset desde o boot; o `setup` libera (PA0 open-drain).
- Testes do host escritos antes da implementação; mudanças de contrato atualizam spec + testes juntos.
- TTN avaliado e viável para testes, mas **adiado** pelo usuário (spec §16).

---

## 6. Próximos passos (em ordem de prioridade)

A **pipeline de CI/CD (6.7.1)** está funcionando no GitHub; o Release será validado na
primeira tag. Ainda sem sensor e sem cadastro no TTN (o usuário está pesquisando), 6.1 e 6.2
seguem bloqueados.

Sugestão: **6.1 e 6.2 em paralelo** (6.1 depende de hardware/cadastro do usuário; 6.2 é
firmware). **6.5 (watchdog)** antes de qualquer teste longo em campo.

### 6.1 Fechar o enlace LoRaWAN  ⟵ destrava a validação na placa
**Precisa do usuário:** gateway AU915 (sub-banda 2) + servidor de rede (TTN para testes;
ChirpStack próprio é opção para produção).

- [ ] Cadastrar o dispositivo (DevEUI acima) no servidor; gravar no módulo pelo terminal
      `AT+ID=AppEui,"…"` e `AT+KEY=APPKEY,"…"` (spec §16.3). Conferir `AT+LW=VER` com o cadastro.
- [ ] Rodar o checklist HIL do `test_plan.md` §3: HIL-02 (join), HIL-03 (uplinks),
      HIL-04 (downlink), HIL-05 (sleep/wake), HIL-08 (24 h), HIL-09 (Stop 2), HIL-10 (PA9 estável).
- [ ] **Medir corrente na build Release** (a Debug mantém o debugger ativo em Stop).
- [ ] Capturar com `wioe5_trace` e salvar em `tests/fixtures/` os itens pendentes da spec
      §15.1: join com sucesso (item 1), prefixo `+MSGHEX:`/`+MSG:` com e sem downlink (2),
      sequência de wake (5), estado do join após reset (7), `AT+LW=VER` (8).
- [ ] Implementar `tests/test_replay.c` (nível N3 do plano) que reproduz as fixtures no
      simulador. Formato das fixtures: ver `tests/fixtures/bench_setup_join_fail.log`.
- [ ] Se alguma resposta real divergir do parser, ajustar parser + testes + spec §17 juntos.

**Pronto quando:** uplinks chegam ao servidor, downlink chega ao `on_downlink`, Stop 2 e
wake funcionam na placa e a corrente média está medida e registrada na spec.

### 6.2 Driver do sensor ultrassônico  ⟵ objetivo do nó
**Precisa do usuário:** modelo do sensor e como está ligado (UART, pulso/eco ou analógico;
pinos; alimentação controlável?).

Processo do AGENTS.md: especificação em `docs/<sensor>/` aprovada pelo usuário → testes no
host → implementação. A spec deve decidir:
- [ ] Interface e periférico (UART/timer input capture/ADC), mudanças no `.ioc` (feitas pelo usuário no CubeMX).
- [ ] Compensação de temperatura (velocidade do som ~0,17 %/°C). O sensor do Wio-E5
      (`AT+TEMP`, ±5 °C) **não** basta; prever sensor dedicado.
- [ ] Filtragem: N leituras por ciclo, mediana, rejeição de ecos falsos (chuva, insetos, ondas).
- [ ] Conversão distância → nível: `nível = altura_instalação − distância`, com calibração.
- [ ] Energia: alimentar o sensor só durante a medida (GPIO / load switch).
- [ ] Mesma arquitetura do driver Wio-E5: lógica sem HAL + porta STM32 + testes com dublês.
- [ ] Integrar trocando a função de payload em `App/app_node.c` (`heartbeat_payload` →
      payload de nível; ver 6.3).

### 6.3 Formato do payload e decodificador
- [ ] Definir formato versionado ≤ 11 bytes. Proposta inicial: `[tipo/versão][nível mm u16]
      [bateria mV u16][temperatura dC i16][flags u8]`. Tipo `0x00` já é o heartbeat atual
      (`[0x00][contador u16]`).
- [ ] Documentar em `docs/` e escrever o decodificador do servidor (TTN *payload formatter* em JS).
- [ ] Teste do codificador no host.

### 6.4 Energia e autonomia
- [ ] Medição de corrente por fase (Stop 2, medida, TX/RX) — depende de 6.1.
- [ ] Tensão da bateria (ADC do STM32 ou `AT+VDD`) no payload.
- [ ] Questão Q9 (spec §15): reduzir SYSCLK (hoje 160 MHz) nas fases ativas.
- [ ] Dimensionar bateria/solar com os números medidos.

### 6.5 Robustez de campo
- [ ] **IWDG**: o IWDG continua contando em Stop 2. Ou o timeout cobre o maior sono
      (até ~18 h — inviável), ou os sonos são limitados ao timeout (acordar para dar refresh),
      ou se congela o IWDG em Stop pelos *option bytes* (`IWDG_STOP`) — decidir com o usuário.
- [ ] Rejoin periódico e tratamento de contador de quadros após reset do MCU.
- [ ] Contadores de reset/erros (`app_lorawan_stats`) enviados periodicamente para diagnóstico.

### 6.6 Funcionalidades da aplicação
- [ ] Downlinks de configuração (período de envio, pedido de status) — usar `on_downlink`,
      hoje só contado em `app_lorawan_on_downlink`.
- [ ] Amostragem adaptativa (mais frequente quando o nível sobe rápido), respeitando duty cycle
      e *fair use* do TTN (30 s de airtime/dia, 10 downlinks/dia).
- [ ] Alarmes por limiar com envio imediato.

### 6.7 Engenharia do repositório

#### 6.7.1 Pipeline de CI/CD (GitHub Actions)  ⟵ CI FUNCIONANDO; Release aguarda a 1ª tag

Feita na branch `feature/ci-cd` (2026-11-19). Detalhes de uso em AGENTS.md §2.6.

| Arquivo | Conteúdo |
|---------|----------|
| `.github/workflows/ci.yml` | jobs `host-tests` (presets `host` e `host-sanitize`, relatório JUnit) e `firmware-build` (Debug + Release, `-DNODE_WERROR=ON`, artefatos `.elf/.bin/.hex/.map`). Toolchain oficial Arm **14.3.rel1** baixado de developer.arm.com com **SHA-256 fixado** e em cache |
| `.github/workflows/release.yml` | em push na `main` e tags `v*`: calcula a versão (`git describe`), reusa o `ci.yml` com `fw-version`, empacota `node-<versão>-<sha7>.{bin,hex,elf,map}` + `SHA256SUMS.txt`; em tag cria o **GitHub Release** (`gh release create --generate-notes`, `-rc` vira pre-release). `contents: write` só no job `publish` |
| `node/CMakeLists.txt` | `node.bin`/`node.hex` + relatório de tamanho em todo build (POST_BUILD); opção `NODE_WERROR`; `FW_VERSION` (cache, padrão `0.0.0-dev`) e `FW_GIT_SHA` (lido no configure) |
| `node/App/app_node.c` | `g_fw_ident = "project_sinos node <versão>+<sha>"`, mantido na imagem por `-Wl,--undefined=g_fw_ident`; ver com `strings node.bin` ou no debugger |

**Verificado localmente (Windows):** Release com `-DNODE_WERROR=ON` e versão de teste compila
sem warnings, gera `node.bin` (28,7 KB) e a string de versão aparece no `.bin`; testes do host
passam. A URL e o SHA-256 do toolchain foram conferidos (download de ~150 MB).

**Primeira execução no GitHub (2026-11-19):** run
[44547900239](https://github.com/rafcunha/project_sinos/actions/runs/44547900239) na
`feature/ci-cd`, **verde de primeira** nos 4 jobs (`host-tests (host)`,
`host-tests (host-sanitize)`, `firmware-build (Debug)`, `firmware-build (Release)`), ~1,5 min.
Isso confirma o build Linux (sistema de arquivos que diferencia maiúsculas), o ASan/UBSan e a
sintaxe do `ci.yml`. Repositório público: os runners do GitHub não consomem minutos pagos.
Artefatos ficam 90 dias.

**Falta:**
- [ ] `release.yml` só roda na `main`/tags: validar na primeira release (6.7.2).
- [x] Proteção de branches **ativa** (conferida pela API do GitHub em 2026-11-19): ruleset
      `main` (id 27647885) e `develop` (id 27647967), ambas `enforcement: active`, com as regras
      de AGENTS.md §2.7. Para reconferir:
      `curl -s https://api.github.com/repos/rafcunha/project_sinos/rules/branches/main`.
- [ ] Script para reproduzir o job em WSL sem `sudo` existe só fora do repo; a execução foi
      barrada pelo controle de permissões (o WSL desta máquina não tem gcc/cmake e o `sudo`
      pede senha). Como o CI real funciona, não é mais necessário.

**Fora do escopo:** atualização do firmware em campo pelo ar (FOTA). "Deploy" aqui é publicar
o `.bin` versionado. Se o usuário quiser FOTA, abrir item próprio.

#### 6.7.2 Outros itens
- [ ] Primeira release pelo git flow com **PR para a `main`** (AGENTS.md §2.5):
      `release/0.1.0` → push → PR para `main` (o usuário mergeia pela web, com *merge commit*)
      → tag `v0.1.0` → push da tag (valida o `release.yml` e gera o Release com o `.bin`)
      → merge de volta no `develop`. Só com pedido do usuário.
- [ ] Opcional: instalar o `gh` (`winget install --id GitHub.cli -e` + `gh auth login`) para o
      agente abrir PRs e acompanhar o CI sem o script da API.
- [ ] Opcional: `app_node.c` tem a lógica de Stop 2 sem teste no host; considerar extrair o
      cálculo (segundos de wakeup, compensação do tick) para uma função pura testável.

### 6.8 Resto do sistema
- [ ] Gateway: firmware próprio (`firmware/gateway/` está vazio) ou gateway comercial.
- [ ] Backend: armazenamento, painel, alertas.
- [ ] Instalação em campo: caixa, antena, fixação do sensor, calibração.

---

## 7. Questões em aberto com o usuário

| #  | Questão                                                        | Bloqueia |
|----|----------------------------------------------------------------|----------|
| A  | Modelo e ligação do sensor ultrassônico                         | 6.2      |
| B  | Gateway e servidor de rede (TTN agora? ChirpStack depois?)      | 6.1      |
| C  | Estratégia do watchdog com Stop 2                               | 6.5      |
| D  | Período de envio em produção (padrão 15 min) e amostragem adaptativa | 6.6 |
| E  | Fonte de energia (bateria, solar) e autonomia desejada          | 6.4      |
| F  | Q9 da spec: clock reduzido nas fases ativas                     | 6.4      |

---

## 8. Como continuar com segurança

1. `git switch develop && git pull --ff-only` (rede só com autorização do usuário).
2. Crie `feature/<escopo>-<descrição>` a partir de `develop` (AGENTS.md §2).
3. Rode os testes do host antes de mexer, para ter a linha de base (157 passando).
4. Ao terminar: checklist do AGENTS.md §2.4, merge `--no-ff` em `develop`.
   **Push só com autorização explícita do usuário na conversa.**
5. Atualize este HANDOFF.md quando um passo for concluído ou uma decisão mudar.
