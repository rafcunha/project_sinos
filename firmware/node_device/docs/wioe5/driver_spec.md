# Driver Wio-E5 — Especificação

| Campo        | Valor                                                                 |
|--------------|-----------------------------------------------------------------------|
| Componente   | `wioe5` — driver de comunicação MCU ↔ módulo LoRaWAN Seeed Wio-E5      |
| Host         | STM32U585 (bare-metal, STM32 HAL, sem RTOS)                            |
| Módulo       | Seeed Wio-E5 (STM32WLE5JC) com firmware AT de fábrica (ref. v4.0.x)    |
| Referência   | *LoRa-E5 AT Command Specification V1.0* (Seeed, 2020-07-20)            |
| Status       | **Implementado** (`node/Lib/wioe5`); testes do host passando; validação em bancada pendente (§15.1) |

---

## 1. Objetivo e escopo

O nó mede periodicamente a altura do rio com um sensor ultrassônico e envia a medida por
LoRaWAN. Este driver é a única peça do firmware que conversa com o Wio-E5. Ele deve:

**Dentro do escopo**

1. Transportar comandos AT pela USART e interpretar as respostas do módulo.
2. Configurar o módulo para LoRaWAN OTAA, Classe A, na região definida em configuração.
3. Executar o *join* OTAA e manter o estado "conectado / não conectado".
4. Enviar uplinks binários (`AT+MSGHEX` / `AT+CMSGHEX`) em uma porta LoRaWAN.
5. Entregar downlinks recebidos (porta + bytes + RSSI/SNR) para a aplicação.
6. Colocar o módulo em baixo consumo (`AT+LOWPOWER`) e acordá-lo.
7. Reportar falhas de forma tipada e oferecer primitivas de recuperação (reset).
8. Ser testável no PC (host), sem hardware, por meio de uma camada de porta abstrata.

**Fora do escopo (nesta versão)**

- Classes B e C, multicast, beacon, modo TEST/P2P, DFU, ABP.
- Codificação do payload da aplicação (o driver recebe `uint8_t[]` prontos).
- Política de agendamento de medições, retentativas de alto nível e backoff de *join*
  (ficam na aplicação; o driver fornece as primitivas e as informações para isso).
- Mudança de baud rate em campo (`AT+UART=BR`).
- Gerenciamento de modos de baixo consumo do próprio STM32U585.

---

## 2. Contexto de hardware

| Item                | Valor atual (CubeMX `node.ioc`)                                  |
|---------------------|------------------------------------------------------------------|
| Periférico          | `USART1` (`huart1`)                                              |
| Pinos               | `PA9` = USART1_TX → Wio-E5 RX · `PA10` = USART1_RX ← Wio-E5 TX    |
| Formato             | 9600 baud, 8N1, sem controle de fluxo (default do módulo, §2.1)  |
| Clock do periférico | PCLK2 (160 MHz)                                                  |
| FIFO                | desabilitada                                                     |
| Interrupção         | `USART1_IRQn` habilitada (prioridade 0) ✔                        |
| Reset do módulo     | `PA0` (`reset_wioe5`) → `RST` do Wio-E5 — configuração em P2     |

**Pré-requisitos no CubeMX:**

- **P1** ✔ — `USART1 global interrupt` habilitada. A recepção é por interrupção (byte a
  byte) alimentando um ring buffer. A 9600 baud isso dá ~960 interrupções/s no máximo,
  carga desprezível. DMA circular + IDLE fica como otimização futura.
- **P2** — `RST` do Wio-E5 ligado em **`PA0`** (label `reset_wioe5`). Configuração exigida:
  **saída open-drain, sem pull, nível inicial baixo** (módulo segurado em reset).
  - O `RST` do Wio-E5 é o NRST do STM32WLE5: ativo em nível baixo, com pull-up interno,
    e também puxado para baixo pelo próprio módulo (watchdog, brown-out). Com push-pull em
    nível alto haveria disputa elétrica nesse momento. Por isso tem de ser **open-drain**.
  - **Nível inicial baixo:** o MCU segura o módulo em reset desde o `MX_GPIO_Init()` até
    o driver liberá-lo no `setup` (§10.1, passo 0). Assim o módulo sempre parte de um
    estado conhecido, com o MCU já pronto para receber. Isso vale também quando só o MCU
    reiniciou (watchdog, falha), mesmo que o módulo estivesse no meio de um join, de um
    envio ou em `LOWPOWER`.
  - Antes do `MX_GPIO_Init()` (reset e início do boot do MCU), o PA0 fica em alta
    impedância e o pull-up do módulo o libera. O módulo começa a bootar e é resetado de
    novo logo em seguida. É inofensivo, porque o passo 0 do `setup` refaz o reset de forma
    controlada.
  - A porta STM32 implementa `set_reset(true)` escrevendo nível baixo e
    `set_reset(false)` escrevendo nível alto (em open-drain, isso só libera o pino).
  Motivo: `AT+RESET` só funciona se o interpretador AT do módulo ainda estiver respondendo.
  Num nó de campo sem acesso físico, o reset por hardware é a única saída se o módulo
  travar. O driver funciona sem o pino (`set_reset = NULL`); nesse caso a recuperação fica
  limitada a `AT+RESET` e ao ciclo de alimentação da placa.
- **P3** ✔ — **Pull-up interno em `PA9` (USART1_TX) e `PA10` (USART1_RX).**
  - `PA9`: mantém a linha RX do módulo em nível alto mesmo se a USART for desinicializada.
    Qualquer borda nessa linha acorda o módulo (§4.30 do PDF). Ver §10.7.
  - `PA10`: evita que a entrada RX do MCU flutue enquanto o Wio-E5 está em reset (o TX
    dele fica em alta impedância), o que geraria bytes falsos e erros de framing.

---

## 3. Decisões de projeto

| #  | Decisão                                                     | Motivo                                                                                                                   |
|----|-------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| D1 | Núcleo **não-bloqueante** com `wioe5_process()` chamado no laço principal | Não há RTOS; o join pode levar ~10 s e um envio confirmado dezenas de segundos. O MCU precisa continuar livre (ou dormir com `__WFI`). Um *wrapper* bloqueante opcional é oferecido para conveniência. |
| D2 | Camada de **porta** (`wioe5_port_t`) com ponteiros de função | Isola a HAL do STM32. Os testes no host usam uma porta falsa com relógio controlado.                                     |
| D3 | **Um comando por vez** em voo                               | O módulo é half-duplex em termos de comando; respostas não carregam ID de correlação.                                    |
| D4 | Parser como **funções puras** sobre uma linha               | Fácil de testar exaustivamente com tabelas de casos e transcrições reais.                                                |
| D5 | Sem `malloc`, sem `printf`/`sscanf`, sem estado global       | Determinismo de memória; permite múltiplas instâncias (ex.: simulador nos testes).                                        |
| D6 | Payload sempre em **hex** (`MSGHEX`/`CMSGHEX`)              | O payload é binário; o modo string não aceita bytes arbitrários.                                                         |
| D7 | Ativação **OTAA**, Classe **A**                             | Padrão para nó a bateria; Classe A tem o menor consumo.                                                                  |
| D8 | Política de retentativa/backoff fica na **aplicação**       | O driver reporta `No band in N ms`, falha de join etc. com dados suficientes para a aplicação decidir.                    |

---

## 4. Arquitetura

```
            ┌───────────────────────────────────────────────────────────┐
 Aplicação  │  app_lorawan.c (agenda medições, chama a API, trata erros) │
            └──────────────▲──────────────────────────────┬─────────────┘
                callbacks  │                              │ API pública (wioe5.h)
            ┌──────────────┴──────────────────────────────▼─────────────┐
            │ L4  Operações: setup, join, send, sleep, wake, reset       │  wioe5.c
            │     (sequências de passos sobre o motor de comandos)       │
            ├────────────────────────────────────────────────────────────┤
            │ L3  Motor de comandos: 1 comando em voo, timeout, guarda,  │  wioe5.c
            │     despacho de linhas p/ operação ou p/ URC               │
            ├────────────────────────────────────────────────────────────┤
            │ L2  Parser de resposta (puro): linha → wioe5_line_t        │  wioe5_parse.c
            │     + utilitários hex encode/decode                        │
            ├────────────────────────────────────────────────────────────┤
            │ L1  Montador de linhas: bytes → linhas terminadas em \n    │  wioe5_line.c
            ├────────────────────────────────────────────────────────────┤
            │ L0  Ring buffer SPSC (ISR escreve, laço principal lê)      │  wioe5_ringbuf.c
            └──────────────▲──────────────────────────────┬─────────────┘
              rx byte (ISR)│                              │ write()/now_ms()
            ┌──────────────┴──────────────────────────────▼─────────────┐
            │ Porta STM32 (wioe5_port_stm32.c): HAL_UART_*, HAL_GetTick  │
            └───────────────────────────────────────────────────────────┘
```

Fluxo de dados:

- **RX:** `HAL_UART_RxCpltCallback` → `wioe5_rx_isr(dev, byte)` → ring buffer →
  `wioe5_process()` drena → montador de linhas → parser → motor de comandos.
- **TX:** operação monta o comando no buffer de TX da instância → `port->write()`.

---

## 5. Regras do protocolo AT relevantes (extraídas do PDF)

| Regra                                                                                                        | Seção PDF | Impacto no driver                                                                                                       |
|--------------------------------------------------------------------------------------------------------------|-----------|-------------------------------------------------------------------------------------------------------------------------|
| Comando termina em `\n`; `\r\n` também é válido                                                              | 2.1       | Driver sempre envia `\r\n`.                                                                                             |
| Comando ≤ 528 caracteres                                                                                     | 2.1       | `WIOE5_TX_BUF_SIZE` ≥ 528; checar antes de enviar.                                                                     |
| Comandos case-insensitive                                                                                    | 2.1       | Driver envia maiúsculas; parser compara prefixos sem diferenciar maiúsculas/minúsculas.                                 |
| Todo comando tem resposta                                                                                    | 2.1       | Todo comando tem timeout; ausência de resposta = `WIOE5_ERR_TIMEOUT`.                                                    |
| Resposta no formato `+CMD: dados`                                                                            | 2.3.3     | Linha útil começa com `+`. Prefixo identifica o comando.                                                                |
| Erro no formato `+CMD: ERROR(n)` com códigos -1, -10, -11, -12, -20..-24                                     | 2.4       | Mapeia para `WIOE5_ERR_AT`, guarda `n` em `info.last_at_error`.                                                         |
| Parâmetros com espaço devem ir entre aspas                                                                   | 2.2       | Hex sempre entre aspas: `AT+MSGHEX="0A1B"`.                                                                             |
| Configurações vão para EEPROM do módulo (canais, DR, ADR, chaves, IDs, porta, REPT, RETRY, modo, delays)      | 2.5       | Evitar regravar a cada ciclo (desgaste) — ver §10.1.                                                                    |
| `AT+LOWPOWER`: qualquer byte acorda; esperar ≥ 5 ms; módulo envia `0x55` extra e, 15 ms depois, `+LOWPOWER: WAKEUP` | 4.30      | Montador de linhas descarta lixo antes de `+`; sequência de wake na §10.5.                                               |
| `AUTOON` exige `FF FF FF FF` antes de cada comando e resposta                                                 | 4.30.1    | **Não usado** nesta versão (complica framing). O montador de linhas ainda tolera `0xFF`.                                |
| `+INFO: Input timeout` se o fim de linha atrasar                                                              | 4.35.1    | Tratado como URC; nunca deve ocorrer porque o comando inteiro sai em um `write()`.                                      |
| `+MSG: Length error 0` → há comando MAC pendente; enviar `AT+MSG` vazio                                       | 4.5.2     | Pré-checagem `AT+LW=LEN` + flush automático (§10.4).                                                                    |
| Logs extras quando `AT+LOG` ≠ `QUIET`                                                                        | 4.37      | Setup envia `AT+LOG=QUIET`; parser ignora linhas não reconhecidas.                                                       |

---

## 6. Interface de porta

```c
typedef struct {
    /* Envia len bytes. Pode ser bloqueante ou assíncrona; o driver garante que
     * 'data' permanece válido até o próximo write(). Retorna 0 em sucesso. */
    int      (*write)(void *ctx, const uint8_t *data, size_t len);

    /* Tempo monotônico em ms (HAL_GetTick). Wrap-around de 32 bits deve ser tratado
     * pelo driver via aritmética sem sinal (now - start >= timeout). */
    uint32_t (*now_ms)(void *ctx);

    /* Opcional (NULL se não houver pino): true = segura o módulo em reset. */
    void     (*set_reset)(void *ctx, bool asserted);

    /* Opcional (NULL desliga): log de diagnóstico, uma linha por chamada, sem '\n'. */
    void     (*log)(void *ctx, wioe5_log_level_t level, const char *msg);

    void      *ctx;
} wioe5_port_t;
```

A implementação STM32 (`wioe5_port_stm32.c`) é a única que inclui `stm32u5xx_hal.h`.
Ela re-arma `HAL_UART_Receive_IT(&huart1, &rx_byte, 1)` a cada byte e trata
`HAL_UART_ErrorCallback` (overrun/framing) re-armando a recepção e incrementando um contador.

---

## 7. API pública (`wioe5.h`)

```c
/* ---- Ciclo de vida ---------------------------------------------------- */
wioe5_status_t wioe5_init(wioe5_t *dev, const wioe5_port_t *port,
                          const wioe5_config_t *cfg);      /* sem I/O */
void           wioe5_process(wioe5_t *dev);                 /* laço principal */
void           wioe5_rx_isr(wioe5_t *dev, uint8_t byte);    /* contexto de ISR */

/* ---- Operações assíncronas (retornam imediatamente) -------------------- */
wioe5_status_t wioe5_start_setup(wioe5_t *dev);   /* probe + configuração */
wioe5_status_t wioe5_start_join(wioe5_t *dev, bool force);
wioe5_status_t wioe5_start_send(wioe5_t *dev, uint8_t fport,
                                const uint8_t *data, size_t len, bool confirmed);
wioe5_status_t wioe5_start_sleep(wioe5_t *dev);
wioe5_status_t wioe5_start_wake(wioe5_t *dev);
wioe5_status_t wioe5_start_reset(wioe5_t *dev);   /* HW se houver pino, senão AT+RESET */

/* ---- Consulta --------------------------------------------------------- */
bool                wioe5_is_busy(const wioe5_t *dev);
bool                wioe5_can_stop(const wioe5_t *dev);   /* seguro entrar em Stop 2? §10.7 */
wioe5_status_t      wioe5_last_result(const wioe5_t *dev);
const wioe5_info_t *wioe5_info(const wioe5_t *dev);

/* ---- Conveniência bloqueante (opcional) ------------------------------- */
/* Chama wioe5_process() até a operação atual terminar; idle_hook pode ser
 * NULL ou uma função que executa __WFI(). */
wioe5_status_t wioe5_wait(wioe5_t *dev, void (*idle_hook)(void));
```

Regras da API:

- `start_*` retorna `WIOE5_OK` se a operação foi **aceita**, `WIOE5_ERR_BUSY` se outra está
  em andamento, `WIOE5_ERR_PARAM` para argumentos inválidos, `WIOE5_ERR_STATE` se o estado
  não permite (ex.: `send` antes do `setup`). O resultado final chega via
  `cfg->on_op_done` e fica em `wioe5_last_result()`.
- Se o módulo estiver dormindo, `start_join/send/setup` executam **wake automático** como
  primeiro passo (o chamador não precisa lembrar).
- `fport` válido: 1..223 (portas de aplicação LoRaWAN; o PDF aceita até 255, mas 224+ são
  reservadas pela especificação LoRaWAN).
- `len` válido: 0..`WIOE5_MAX_PAYLOAD`. `len == 0` envia frame vazio (útil para flush de MAC).

### 7.1 Callbacks (em `wioe5_config_t`)

```c
void (*on_op_done)(void *user, wioe5_op_t op, wioe5_status_t status);
void (*on_downlink)(void *user, const wioe5_downlink_t *dl);
void (*on_event)(void *user, wioe5_event_t evt);   /* URC, falha, overflow… */
void  *user;
```

- Chamados **somente** a partir de `wioe5_process()` (nunca da ISR).
- `on_op_done` é chamado **depois** de o driver voltar a `IDLE`, então é permitido iniciar
  outra operação dentro dele (encadeamento).
- `on_downlink` é chamado antes de `on_op_done` da operação de envio que o recebeu.
  O ponteiro `dl->data` só é válido durante o callback.

### 7.2 Tipos principais

```c
typedef enum {
    WIOE5_OK = 0,
    WIOE5_ERR_PARAM,        /* argumento inválido na API                          */
    WIOE5_ERR_BUSY,         /* driver já tem operação em andamento                */
    WIOE5_ERR_STATE,        /* operação não permitida no estado atual             */
    WIOE5_ERR_UART,         /* port->write() falhou                               */
    WIOE5_ERR_TIMEOUT,      /* sem resposta terminal dentro do prazo              */
    WIOE5_ERR_AT,           /* módulo respondeu ERROR(n); ver info.last_at_error   */
    WIOE5_ERR_UNEXPECTED,   /* resposta com prefixo certo mas conteúdo não previsto */
    WIOE5_ERR_OVERFLOW,     /* ring buffer ou linha estourou durante a operação    */
    WIOE5_ERR_MODEM_BUSY,   /* "LoRaWAN modem is busy"                            */
    WIOE5_ERR_NOT_JOINED,   /* "Please join network first"                        */
    WIOE5_ERR_JOIN_FAILED,  /* "Join failed"                                      */
    WIOE5_ERR_NO_FREE_CH,   /* "No free channel"                                  */
    WIOE5_ERR_NO_BAND,      /* "No band in N ms"; ver info.retry_after_ms          */
    WIOE5_ERR_DR,           /* "DR error"                                         */
    WIOE5_ERR_LENGTH,       /* payload > máximo do DR atual / "Length error N"      */
    WIOE5_ERR_NO_ACK,       /* confirmado terminou sem "ACK Received"             */
} wioe5_status_t;

typedef enum { WIOE5_OP_NONE, WIOE5_OP_SETUP, WIOE5_OP_JOIN, WIOE5_OP_SEND,
               WIOE5_OP_SLEEP, WIOE5_OP_WAKE, WIOE5_OP_RESET } wioe5_op_t;

typedef struct {
    uint8_t        port;
    const uint8_t *data;
    size_t         len;
    int16_t        rssi_dbm;      /* INT16_MIN se não informado          */
    int16_t        snr_cdb;       /* SNR em centésimos de dB (6.25 → 625) */
    uint8_t        rx_window;     /* 1, 2 (0 = classe C, 3 = ping slot)   */
} wioe5_downlink_t;

typedef struct {
    char     fw_version[16];      /* "4.0.11"                              */
    uint8_t  dev_eui[8];
    uint8_t  dev_addr[4];         /* válido após join                       */
    bool     joined;
    bool     sleeping;
    int16_t  last_rssi_dbm;
    int16_t  last_snr_cdb;
    uint8_t  link_margin_db;      /* de "Link M, G" (LinkCheckAns)           */
    uint8_t  link_gw_count;
    int16_t  last_at_error;       /* n de ERROR(n); 0 se nenhum              */
    uint32_t retry_after_ms;      /* de "No band in N ms"                    */
    uint16_t max_payload;         /* último AT+LW=LEN                         */
    uint32_t consecutive_timeouts;
    uint32_t rx_overflows;
    uint32_t unknown_lines;
} wioe5_info_t;
```

---

## 8. Máquina de estados

### 8.1 Estado do driver

```
           wioe5_init()
                │
                ▼
          ┌──────────┐ start_setup ┌─────────┐
          │  UNINIT  │────────────▶│  BUSY   │──── op terminou ───┐
          └──────────┘             │ (op X)  │                    │
                                   └────▲────┘                    ▼
                                        │ start_*            ┌─────────┐
                                        └────────────────────│  IDLE   │
                                                             └────┬────┘
                                                  start_sleep ok  │  ▲ start_wake ok
                                                                  ▼  │ (ou wake automático)
                                                             ┌─────────┐
                                                             │SLEEPING │
                                                             └─────────┘
```

- `UNINIT` → só aceita `start_setup` e `start_reset`.
- Falha no `setup` volta para `UNINIT` (não para `IDLE`).
- `reset` sempre leva para `UNINIT` e limpa `joined`; a aplicação deve chamar `setup` de novo.
- Flags ortogonais: `joined` (atualizada por join/send/reset), `setup_done`.

### 8.2 Motor de comandos (dentro de `BUSY`)

Cada operação é uma lista de **passos**. Um passo é:

```c
typedef struct {
    const char *cmd_fmt;        /* ex.: "AT+PORT=%u" (formatação própria, sem printf) */
    const char *resp_prefix;    /* ex.: "PORT" — compara com "+PORT:"                 */
    uint32_t    timeout_ms;     /* base; o motor soma o tempo de TX (§11)            */
    step_kind_t kind;           /* SIMPLE | JOIN | MSG | LOWPOWER | DELAY | ...       */
    bool        optional;       /* falha não aborta a operação                        */
} step_t;
```

Ciclo de um passo:

1. **Guarda de linha ociosa:** só transmite depois de ≥ `T_GUARD` (10 ms ≈ 10 caracteres a
   9600 baud) sem bytes recebidos. Evita que restos de uma resposta multi-linha anterior
   sejam lidos como resposta do próximo comando.
2. Monta o comando em `tx_buf`, chama `port->write()`, registra `t0`.
3. Para cada linha recebida:
   - Prefixo ≠ `resp_prefix` → trata como URC (§9.4) ou "linha desconhecida".
   - Prefixo = `resp_prefix` → entrega ao manipulador do `kind`, que decide:
     `CONTINUE`, `DONE_OK`, `DONE_ERR(status)`.
4. Se `now - t0 ≥ timeout` → `WIOE5_ERR_TIMEOUT` (`consecutive_timeouts++`).
5. Qualquer resposta (mesmo erro) zera `consecutive_timeouts`.

---

## 9. Parsing

### 9.1 Montador de linhas (L1)

- Acumula bytes até `\n`. Remove `\r`.
- Descarta tudo **antes do primeiro `+`** da linha (elimina `0x55` de wake, `0xFF`, ruído
  de boot). Linha sem `+` → descartada e contada em `unknown_lines`.
- Linha maior que `WIOE5_LINE_MAX` → descarta até o próximo `\n`, sinaliza
  `WIOE5_EVT_LINE_OVERFLOW`; se houver operação em voo, ela termina com `WIOE5_ERR_OVERFLOW`.
- Linhas vazias são ignoradas.

### 9.2 Linha genérica (L2)

```
linha    := "+" NOME ":" [ " " ] CORPO
NOME     := 1..16 caracteres [A-Za-z0-9]
CORPO    := resto da linha (aparado nas pontas)
erro     := CORPO casa com "ERROR(" INT ")"
```

`wioe5_parse_line(const char *s, wioe5_line_t *out)` preenche `name`, `body`, `is_error`,
`error_code`. Função pura, sem estado.

### 9.3 Respostas específicas

| Comando                   | Linhas esperadas (exemplos do PDF)                                         | Extração                                                       |
|---------------------------|-----------------------------------------------------------------------------|----------------------------------------------------------------|
| `AT`                      | `+AT: OK`                                                                  | —                                                              |
| `AT+VER`                  | `+VER: 4.0.11`                                                             | `fw_version`                                                   |
| `AT+ID=DevEui`            | `+ID: DevEui, 01:23:45:67:89:AB:CD:EF`                                     | 8 bytes hex separados por `:`                                  |
| `AT+MODE=LWOTAA`          | `+MODE: LWOTAA`                                                            | confere eco do valor                                           |
| `AT+DR=<banda>`           | `+DR: AU915` (**a confirmar em bancada**, pode ter várias linhas)          | confere nome da banda                                          |
| `AT+CH=NUM,<máscara>`     | `+CH: NUM, 8-15, 65`                                                       | confere eco                                                    |
| `AT+ADR=ON`               | `+ADR: ON`                                                                 | confere eco                                                    |
| `AT+DR=<n>`               | `+DR: DR2`                                                                 | confere eco                                                    |
| `AT+PORT=<n>`             | `+PORT: 8`                                                                 | confere eco                                                    |
| `AT+RETRY=<n>`/`AT+REPT=` | `+RETRY: 3` / `+REPT: 1`                                                   | confere eco                                                    |
| `AT+LOG=QUIET`            | `+LOG: QUIET` (**a confirmar**)                                            | opcional                                                       |
| `AT+LW=LEN`               | `+LW: LEN, 50`                                                             | `max_payload`                                                  |
| `AT+KEY=APPKEY,"…"`       | `+KEY: APPKEY <hex>`                                                       | confere (não loga a chave)                                     |
| `AT+ID=AppEui,"…"`        | `+ID: AppEui, xx:…`                                                        | confere                                                        |
| `AT+RESET`                | `+RESET: OK`                                                               | —                                                              |
| `AT+LOWPOWER`             | `+LOWPOWER: SLEEP`                                                         | —                                                              |
| `AT+JOIN`                 | `Starting`/`Start` → `NORMAL` → `NetID 000024 DevAddr 48:00:00:01` → `Done` | `dev_addr`, `joined=true`                                      |
| `AT+MSGHEX` / `CMSGHEX`   | `Start` → [`Wait ACK`] → [`FPENDING`] → [`Link 20, 1`] → [`ACK Received`] → [`PORT: 8; RX: "…"`] → [`RXWIN1, RSSI -93, SNR 6.25`] → `Done` | ack, downlink, RSSI/SNR, link check |

**Corpos de erro comuns a JOIN/MSG/CMSG/MSGHEX/CMSGHEX** (§4.5.2 do PDF):

| Corpo (case-insensitive, por prefixo)  | Status                     | Observação                                       |
|----------------------------------------|----------------------------|--------------------------------------------------|
| `LoRaWAN modem is busy`                | `WIOE5_ERR_MODEM_BUSY`     | terminal                                         |
| `Please join network first`            | `WIOE5_ERR_NOT_JOINED`     | terminal; zera `joined`                          |
| `Joined already`                       | `WIOE5_OK` (para JOIN)     | terminal; `joined=true`                          |
| `No free channel`                      | `WIOE5_ERR_NO_FREE_CH`     | terminal                                         |
| `No band in <N>ms`                     | `WIOE5_ERR_NO_BAND`        | terminal; `retry_after_ms = N`                   |
| `DR error`                             | `WIOE5_ERR_DR`             | terminal                                         |
| `Length error <N>`                     | `WIOE5_ERR_LENGTH`         | terminal; ver §10.4                              |
| `Join failed`                          | `WIOE5_ERR_JOIN_FAILED`    | aguarda `Done` por até `T_JOIN_FAIL_GRACE`       |
| `ERROR(<n>)`                           | `WIOE5_ERR_AT`             | terminal                                         |

**Nota sobre prefixo do envio:** o PDF documenta as linhas do `MSGHEX` com prefixo `+MSG:`
em alguns exemplos e `+MSGHEX:` em outros. O manipulador do `kind MSG` aceita os prefixos
`MSG`, `MSGHEX`, `CMSG`, `CMSGHEX` enquanto a operação de envio estiver ativa.

**Downlink:** `PORT: <p>; RX: "<hex>"` → valida hex par, decodifica para
`dl_buf[WIOE5_MAX_DOWNLINK]` (242 B). Hex inválido ou grande demais → evento
`WIOE5_EVT_BAD_DOWNLINK`, envio continua.

**RSSI/SNR:** `RXWIN<w>, RSSI <int>, SNR <decimal>`; SNR convertido sem float
(`"6.25"` → 625, `"-7.5"` → -750).

### 9.4 Mensagens não solicitadas (URC)

| Linha                       | Ação                                                    |
|-----------------------------|---------------------------------------------------------|
| `+LOWPOWER: WAKEUP`         | `sleeping=false`; evento `WIOE5_EVT_WOKE`               |
| `+INFO: Input timeout`      | evento `WIOE5_EVT_INPUT_TIMEOUT` (indica bug no TX)     |
| `+JOIN: …` fora de operação | atualiza `joined` se `Done`/`NetID`; evento             |
| `+MSG: …` fora de operação  | ignorado (classe A não deveria gerar)                   |
| qualquer outra              | `unknown_lines++`; log nível DEBUG                       |

---

## 10. Operações

### 10.1 `setup`

| # | Comando                       | Opcional | Persistente (EEPROM) | Notas                                               |
|---|-------------------------------|----------|----------------------|-----------------------------------------------------|
| 0 | reset por hardware            | —        | —                    | só se `set_reset` ≠ NULL: garante reset por ≥ `T_RESET_PULSE`, libera, espera `T_BOOT`; bytes de boot são descartados |
| 1 | `AT` (até 3 tentativas)       | não      | —                    | probe; 1ª tentativa pode falhar com lixo de boot   |
| 2 | `AT+LOG=QUIET`                | sim      | ? (Q6)               | reduz tráfego e consumo                              |
| 3 | `AT+VER`                      | não      | —                    | guarda versão                                       |
| 4 | `AT+ID=DevEui`                | não      | —                    | DevEUI de fábrica, único                            |
| 5 | `AT+MODE=LWOTAA`              | não      | sim                  |                                                     |
| 6 | `AT+DR=<banda>`               | não      | sim                  | ex.: `AU915`                                         |
| 7 | `AT+CH=NUM,<máscara>`         | sim*     | sim                  | *só se `cfg.channel_mask` ≠ NULL (ex.: `"8-15,65"`)  |
| 8 | `AT+ADR=ON\|OFF`              | não      | sim                  |                                                     |
| 9 | `AT+DR=<n>`                   | sim*     | sim                  | *só se ADR=OFF                                      |
|10 | `AT+PORT=<n>`                 | não      | sim                  | porta padrão                                        |
|11 | `AT+RETRY=<n>`                | não      | sim                  | confirmados                                          |
|12 | `AT+REPT=<n>`                 | não      | sim                  | não confirmados                                      |
|13 | `AT+ID=AppEui,"…"`            | sim*     | sim                  | *só se `cfg.provision` ≠ NULL                        |
|14 | `AT+KEY=APPKEY,"…"`           | sim*     | sim                  | *só se `cfg.provision` ≠ NULL; nunca logar a chave   |

- Classe A não é configurada: o módulo sempre liga em Classe A (§4.26).
- **Desgaste da EEPROM:** os passos 5–14 gravam EEPROM. O `setup` deve rodar no *cold boot*
  (power-on ou após `reset`), não a cada ciclo de medição. Entre medições usa-se só
  `sleep`/`wake`. Se o projeto adotar Standby do STM32 (que reinicia o MCU), a aplicação
  deve distinguir *cold* de *warm boot* (registrador de backup) — ver Q7.
- Sucesso → `setup_done=true`, estado `IDLE`. Falha → estado `UNINIT`.

### 10.2 `join`

1. (wake automático se dormindo)
2. `AT+JOIN` ou `AT+JOIN=FORCE`.
3. Termina com:
   - `Done` com `NetID … DevAddr …` visto → `WIOE5_OK`, `joined=true`, `dev_addr` salvo.
   - `Joined already` → `WIOE5_OK`.
   - `Join failed` → espera `Done` por até `T_JOIN_FAIL_GRACE`; resultado `WIOE5_ERR_JOIN_FAILED`.
   - Erros da tabela §9.3.
   - Timeout `T_JOIN`.
- O driver **não** reenvia join. Backoff é da aplicação (respeitar o duty cycle de join
  do §3.5 do PDF; o módulo já o aplica e responde `No band in N ms`).
- Auto-join (`AJMODE`) do módulo **não** é usado: mantém o controle de tempo no host.

### 10.3 `send`

1. Valida argumentos; exige `setup_done`. (Wake automático se dormindo.)
2. Se `fport` ≠ porta atual do módulo → `AT+PORT=<fport>` (o driver memoriza a porta atual).
3. Se `cfg.check_len_before_send` (padrão `true`) → `AT+LW=LEN` (§10.4).
4. `AT+MSGHEX="<hex>"` ou `AT+CMSGHEX="<hex>"` (`len == 0` → sem `="…"`).
5. Processa linhas até `Done`:
   - `ACK Received` → `acked=true`.
   - Downlink → `on_downlink` (após receber também a linha de RSSI, ou no `Done`).
   - `Link M, G` → `link_margin_db`, `link_gw_count`.
6. Resultado:
   - Não confirmado + `Done` → `WIOE5_OK`.
   - Confirmado + `Done` + `acked` → `WIOE5_OK`; sem `acked` → `WIOE5_ERR_NO_ACK`.
   - `Please join network first` → `WIOE5_ERR_NOT_JOINED` e `joined=false`.

### 10.4 Tamanho máximo e flush de MAC

- `AT+LW=LEN` → `max_payload`.
- Se `max_payload == 0`: há comando MAC pendente. Envia `AT+MSGHEX` (vazio) uma vez, espera
  `Done`, consulta `LEN` de novo. Se continuar 0 → `WIOE5_ERR_LENGTH`.
- Se `len > max_payload` → `WIOE5_ERR_LENGTH` **sem transmitir** (a aplicação pode reduzir
  o payload ou esperar o ADR subir o DR).
- Para AU915 DR0–DR2 (SF12–SF10/125) o máximo é 51 bytes (Tabela 3-3), mas cai para 11
  bytes se o servidor ativar *uplink dwell time* (400 ms) via `TxParamSetupReq`. Com ADR o
  limite muda a cada envio. **Manter o payload da aplicação ≤ 11 bytes** garante envio no
  pior caso.

### 10.5 `sleep` / `wake`

- `sleep`: `AT+LOWPOWER` → `+LOWPOWER: SLEEP` → `sleeping=true`, estado `SLEEPING`.
  (Sem argumento: dorme até ser acordado pela UART.)
- Enquanto `SLEEPING`, o driver **não transmite nada** além da sequência de wake (qualquer
  sinal no RX do módulo o acorda, §4.30).
- `wake`:
  1. Envia 1 byte `cfg.wake_byte` (padrão `0xFF`).
  2. Espera `T_WAKE_SETTLE` (padrão 20 ms; PDF exige ≥ 5 ms, e o `+LOWPOWER: WAKEUP` vem
     ~15 ms após o `0x55`).
  3. Probe `AT` → `+AT: OK` (até 2 tentativas; a 1ª pode retornar `ERROR(-23)` pelo byte de wake).
  4. `sleeping=false`, estado `IDLE`.

### 10.6 `reset`

- Com `port->set_reset`: assert por `T_RESET_PULSE` (10 ms), libera, aguarda `T_BOOT` (2 s),
  probe `AT`.
- Sem pino: `AT+RESET` → `+RESET: OK`, aguarda `T_BOOT`, probe `AT`.
- Sempre: `joined=false`, `setup_done=false`, estado `UNINIT`.
- A **política** de quando resetar é da aplicação. Recomendação: resetar quando
  `info.consecutive_timeouts ≥ 3` (o driver emite `WIOE5_EVT_UNRESPONSIVE` nesse ponto).

### 10.7 Integração com o baixo consumo do STM32U585 (Stop 2)

Entre medições o STM32 fica em **Stop 2** e o Wio-E5 em `AT+LOWPOWER`. O Stop 2 mantém a
RAM e os registradores dos periféricos, então o estado do driver (`joined`, porta atual,
`setup_done`) sobrevive. `setup` e `join` rodam só no *cold boot*. Isso resolve a Q7 e
evita o desgaste da EEPROM do módulo.

Ciclo de medição esperado (implementado na aplicação, não no driver):

```
cold boot ─▶ setup ─▶ join ─▶ ┌──────────────────────────────────────────────┐
                              │ medir sensor                                 │
                              │ send (wake automático do Wio-E5)             │
                              │ sleep do Wio-E5 (AT+LOWPOWER)                │
                              │ wioe5_can_stop() == true ?                   │
                              │ RTC wakeup timer ─▶ HAL_PWREx_EnterSTOP2Mode │
                              │ acorda: SystemClock_Config()                 │
                              └──────────────────────── loop ────────────────┘
```

Regras que o driver impõe ou exige:

1. **Nova função** `bool wioe5_can_stop(const wioe5_t *dev)`: retorna `true` só se não há
   operação em andamento, o ring buffer está vazio e o estado é `SLEEPING` (ou `UNINIT`).
   A aplicação só entra em Stop 2 quando ela retorna `true`. Motivo: o `HAL_GetTick()`
   (SysTick) para em Stop 2. Um timeout em andamento ficaria congelado, e um byte chegando
   do módulo seria perdido, porque a USART1 não recebe em Stop 2.
2. **Nada chega do módulo enquanto o STM32 está em Stop 2.** Em Classe A e com o módulo em
   `LOWPOWER`, o Wio-E5 só fala depois de ser acordado pelo host. Por isso não é preciso
   acordar o MCU pela UART. Downlinks só chegam dentro de um `send`, quando o MCU já está
   acordado.
3. **A linha TX do STM32 (RX do módulo) precisa ficar em nível alto durante o Stop 2.**
   Em Stop 2 o pino `PA9` mantém a função alternada e o nível ocioso alto. A USART **não**
   deve ser desinicializada antes do Stop. Se um dia for (para economizar), `PA9` deve
   antes virar saída em nível alto. O pull-up de P3 é a proteção extra contra uma borda
   espúria, que acordaria o módulo e gastaria energia sem ninguém perceber.
4. **Ao acordar:** a aplicação chama `SystemClock_Config()` (o clock volta em MSI/HSI). A
   recepção por interrupção armada antes do Stop continua válida, porque os registradores
   da USART são mantidos. A porta STM32 oferece `wioe5_port_stm32_resume()`, que re-arma
   o `HAL_UART_Receive_IT` por segurança e limpa flags de erro (ORE/FE/NE) que possam ter
   ficado pendentes.
5. **Durante uma operação** (join ≈ 6–10 s, envio ≈ 2–4 s com janelas RX1/RX2), o MCU fica
   em **Sleep** (`__WFI` no `idle_hook` do `wioe5_wait()` ou no laço principal). As
   interrupções da USART e do SysTick continuam ativas. Nessa fase o gasto do MCU depende
   da frequência do clock. 160 MHz não é necessário para 9600 baud, e reduzir o SYSCLK
   (ex.: MSI 16 MHz) nos períodos de comunicação baixa bastante o consumo. Fica como
   decisão da aplicação (Q9).
6. **Otimização futura (fora desta versão):** usar Stop 1 durante as esperas, com o
   *wake-up* da USART1 por recepção e um `now_ms()` baseado em LPTIM (que roda em Stop).
   A interface de porta já permite trocar a fonte de tempo sem mudar o driver.

---

## 11. Temporização

Tempo de TX a 9600 8N1 ≈ 1,042 ms/byte. O motor soma `len(cmd) × 1,1 ms` ao timeout de cada
passo, porque a transmissão pode ser assíncrona.

| Constante              | Padrão     | Justificativa                                                                     |
|------------------------|------------|-----------------------------------------------------------------------------------|
| `T_GUARD`              | 10 ms      | ~10 caracteres de silêncio antes de cada comando                                  |
| `T_CMD`                | 1000 ms    | comandos simples (AT, VER, ID, LEN)                                               |
| `T_CMD_EEPROM`         | 2000 ms    | comandos que gravam EEPROM                                                        |
| `T_JOIN`               | 20000 ms   | JRX2 = 6 s (Tabela 4-2) + airtime + margem; inclui possível `No band` imediato    |
| `T_JOIN_FAIL_GRACE`    | 2000 ms    | espera pelo `Done` depois de `Join failed`                                        |
| `T_MSG`                | 15000 ms × REPT | RX2 = 2 s + airtime SF12 (~2 s para 51 B) + margem                           |
| `T_CMSG`               | 15000 ms × RETRY + 10000 ms × (RETRY−1) | atraso aleatório de 3–10 s entre retentativas (§4.17) |
| `T_LOWPOWER`           | 1000 ms    | resposta `SLEEP`                                                                  |
| `T_WAKE_SETTLE`        | 20 ms      | ≥ 5 ms exigidos; folga para o `WAKEUP`                                            |
| `T_RESET_PULSE`        | 10 ms      | pulso no pino RST                                                                 |
| `T_BOOT`               | 2000 ms    | boot do módulo (**a medir em bancada**)                                           |

Todas são campos de `wioe5_config_t` com esses valores padrão (`WIOE5_CONFIG_DEFAULT`).

---

## 12. Memória e concorrência

| Buffer                 | Tamanho padrão | Motivo                                                        |
|------------------------|----------------|---------------------------------------------------------------|
| Ring buffer RX         | 512 B (2ⁿ)     | ~0,5 s de dados a 9600 sem drenar                             |
| Linha                  | 560 B          | downlink de 242 B em hex (484) + prefixo                      |
| TX                     | 532 B          | comando máximo 528 + `\r\n` + folga                           |
| Downlink decodificado  | 242 B          | maior payload LoRaWAN                                          |
| **Total aprox.**       | **~1,9 KB**    | tudo dentro de `wioe5_t`; tamanhos ajustáveis por macro       |

- Ring buffer **SPSC**: ISR escreve `head`, laço principal escreve `tail`; índices
  `volatile` de largura nativa (32 bits), sem desabilitar interrupções. Cheio → descarta o
  byte e incrementa `rx_overflows` (sinalizado no próximo `process`).
- Nenhuma função do driver é reentrante; apenas `wioe5_rx_isr` pode ser chamada de ISR.
- Sem `malloc`, sem ponto flutuante, sem `printf`/`sscanf` (formatação e parsing próprios).
- C11, compila com `-Wall -Wextra -Werror` em `arm-none-eabi-gcc` e no gcc do host.

---

## 13. Configuração (`wioe5_config_t`)

| Campo                   | Tipo                 | Padrão         | Descrição                                            |
|-------------------------|----------------------|----------------|------------------------------------------------------|
| `band`                  | `const char *`       | `"AU915"`      | plano de frequência (Q1)                             |
| `channel_mask`          | `const char *`       | `"8-15,65"`    | `AT+CH=NUM,…`; `NULL` = não mexe (Q1)                |
| `adr`                   | `bool`               | `true`         |                                                      |
| `data_rate`             | `uint8_t`            | 2              | usado só com ADR off                                 |
| `default_port`          | `uint8_t`            | 1              |                                                      |
| `retry`                 | `uint8_t`            | 3              | `AT+RETRY` (confirmados)                             |
| `rept`                  | `uint8_t`            | 1              | `AT+REPT` (não confirmados)                          |
| `check_len_before_send` | `bool`               | `true`         | §10.4                                                |
| `provision`             | `const wioe5_provision_t *` | `NULL`  | AppEUI + AppKey; `NULL` = já gravados no módulo (Q3) |
| `wake_byte`             | `uint8_t`            | `0xFF`         | §10.5                                                |
| `timeouts`              | `wioe5_timeouts_t`   | §11            |                                                      |
| callbacks + `user`      |                      | `NULL`         | §7.1                                                 |

**Segredos:** AppKey nunca vai para o repositório. Se `provision` for usado, os valores vêm de
um header local ignorado pelo git (`lorawan_secrets.h`, com `lorawan_secrets.example.h`
versionado como modelo).

---

## 14. Estrutura de arquivos e integração

```
firmware/node_device/
├── docs/wioe5/
│   ├── driver_spec.md          ← este documento
│   └── test_plan.md
├── node/                       ← projeto STM32CubeMX (CMake)
│   └── Lib/wioe5/
│       ├── inc/wioe5.h               API pública + tipos
│       ├── inc/wioe5_config.h        macros de tamanho
│       ├── src/wioe5.c               L3 + L4
│       ├── src/wioe5_parse.c/.h      L2 (puro)
│       ├── src/wioe5_line.c/.h       L1
│       ├── src/wioe5_ringbuf.c/.h    L0
│       └── port/wioe5_port_stm32.c   porta STM32 (único arquivo com HAL)
└── tests/                      ← testes no host (ver test_plan.md)
```

- `node/CMakeLists.txt` já compila `Lib/wioe5/src/*.c` e `Lib/wioe5/port/wioe5_port_stm32.c`,
  com `Lib/wioe5/inc` e `Lib/wioe5/port` nos includes.
- Tamanho medido (arm-none-eabi-gcc 14.3, `-Os`): **~8,7 KB de flash** para driver + porta;
  RAM = `sizeof(wioe5_t)` (~2,3 KB com os tamanhos padrão), sem `.data`/`.bss` próprios.

### 14.1 Integração da porta STM32

A porta (`port/wioe5_port_stm32.h`) não define os callbacks globais da HAL, porque a
aplicação pode ter outras UARTs. A aplicação encaminha para ela:

```c
static wioe5_t       wio;
static wioe5_stm32_t wio_hw = { .huart    = &huart1,
                                .rst_port = reset_wioe5_GPIO_Port,
                                .rst_pin  = reset_wioe5_Pin };

wioe5_port_t   port = wioe5_port_stm32(&wio_hw, &wio);
wioe5_config_t cfg;
wioe5_config_default(&cfg);          /* + callbacks */
wioe5_init(&wio, &port, &cfg);
wioe5_port_stm32_start(&wio_hw);     /* arma a RX por interrupção */
wioe5_start_setup(&wio);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) { wioe5_port_stm32_rx_cplt(&wio_hw, h); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)  { wioe5_port_stm32_error(&wio_hw, h); }
/* ao sair do Stop 2, depois de SystemClock_Config(): */
wioe5_port_stm32_resume(&wio_hw);
```

- **TX bloqueante** (`HAL_UART_Transmit`): o comando mais longo (~500 bytes) leva ~520 ms
  a 9600 baud. A RX continua por interrupção nesse tempo.
- `rst_port = NULL` desliga o reset por hardware (`set_reset = NULL`).
- A porta não gera log (`log = NULL`). Para depurar, a aplicação pode trocar por uma função
  própria antes do `wioe5_init`.
- `tests/CMakeLists.txt` compila `Lib/wioe5/src/*.c` (sem a porta STM32) com o gcc do host.
- Código gerado pelo CubeMX só é alterado dentro de `/* USER CODE BEGIN … END */`.

---

## 15. Questões

| #  | Questão                                                       | Situação                                                                                       |
|----|---------------------------------------------------------------|------------------------------------------------------------------------------------------------|
| Q1 | Região e sub-banda                                            | **Decidido:** Rio dos Sinos/RS → `AU915`, sub-banda 2 (canais 8–15 + 65), a mesma do TTN no Brasil |
| Q2 | Network server                                                | **Adiado.** Avaliado o TTN (§16), viável; fica para depois. Os testes com rede (N4: HIL-02 a 05, 08, 09) aguardam |
| Q3 | Chaves: gravadas uma vez via terminal ou pelo firmware?       | Padrão mantido: gravar uma vez via terminal (§16.3); `provision = NULL`                         |
| Q4 | Pino `RST` ligado a um GPIO?                                  | **Decidido:** `PA0`, open-drain (P2)                                                           |
| Q5 | Uplinks confirmados ou não?                                   | Padrão mantido: não confirmados (energia e limite de downlinks do TTN, §16.4)                   |
| Q6 | `AT+LOG` é persistente?                                       | Verificar em bancada (§15.1); enviar sempre no setup                                           |
| Q7 | Stop 2 ou Standby entre medições?                             | **Decidido:** Stop 2 (§10.7)                                                                   |
| Q8 | Tamanho do payload da aplicação                               | Padrão mantido: ≤ 11 bytes                                                                     |
| Q9 | Frequência do SYSCLK durante a comunicação                    | **Aberto (aplicação):** reduzir de 160 MHz durante as esperas do driver (§10.7 item 5)          |

### 15.1 Comportamentos a confirmar em bancada

O PDF tem inconsistências e exemplos incompletos. Antes de fechar o parser, capturar com um
adaptador USB-UART + terminal (9600 8N1) as respostas reais e salvar como *fixtures* de teste
(`tests/fixtures/*.log`):

1. Sequência real do `AT+JOIN` com sucesso e com falha (`Start` vs `Starting`; `Done` após `Join failed`).
2. Prefixo das linhas do `AT+MSGHEX`/`AT+CMSGHEX` (`+MSGHEX:` vs `+MSG:`) com e sem downlink.
3. Resposta de `AT+DR=AU915` (uma linha ou várias) e de `AT+CH=NUM,8-15,65`.
4. Resposta de `AT+LOG=QUIET`.
5. Sequência de wake: bytes exatos recebidos após enviar `0xFF` com o módulo em `LOWPOWER`.
6. O que o módulo imprime no boot e quanto tempo leva até aceitar `AT`.
7. Se o estado de join sobrevive a `AT+RESET` / power cycle.
8. Versão do firmware AT do módulo usado (`AT+VER`) e versão LoRaWAN ativa (`AT+LW=VER`),
   que precisa bater com o cadastro no TTN (§16.3).

#### Resultados da primeira bancada (2026-11-17)

Captura pelo buffer `wioe5_trace` (build Debug), lido com o debugger. Fixture:
`tests/fixtures/bench_setup_join_fail.log`. Sem gateway e sem AppKey gravada.

| Item | Resultado                                                                                     | Situação      |
|------|-----------------------------------------------------------------------------------------------|---------------|
| 1    | Falha: `Start` → `NORMAL` → `Join failed` → `Done` (~7 s). Usa `Start`, não `Starting`. O `Done` vem depois do `Join failed`, então o driver termina no `Done` sem precisar da espera de 2 s | falha ✔; sucesso pendente |
| 2    | —                                                                                             | pendente (precisa de join) |
| 3    | `+DR: AU915` e `+CH: NUM, 8-15, 65`, uma linha cada                                           | ✔             |
| 4    | `+LOG: QUIET`                                                                                 | ✔             |
| 5    | —                                                                                             | pendente (o ciclo só põe o módulo em sleep depois de um envio) |
| 6    | Depois do reset pelo PA0 e dos 2 s de espera, o primeiro `AT` já respondeu `+AT: OK`. Nenhum lixo chegou ao parser | ✔ (tempo mínimo de boot não medido) |
| 7    | —                                                                                             | pendente      |
| 8    | `+VER: 4.0.11`; `AT+LW=VER` não consultado                                                    | parcial       |

Contadores do driver no fim da captura: `uart_errors = 0`, `rx_overflows = 0`,
`line_overflows = 0`, `unknown_lines = 0`, `consecutive_timeouts = 0`.

---

## 16. Ambiente de teste com The Things Network

> **Adiado.** O foco atual é só a comunicação MCU ↔ Wio-E5. Esta seção fica registrada
> para quando os testes com rede forem retomados.

### 16.1 Avaliação: é viável?

**Sim.** O foco é a comunicação MCU ↔ Wio-E5, e o TTN só entra no último nível de teste:

| O que se quer validar                                         | Precisa de rede? |
|---------------------------------------------------------------|------------------|
| Parser, motor de comandos, operações (testes N1–N3)           | Não (PC)         |
| `setup`, `sleep`/`wake`, `reset`, caminho de falha do `join`  | Não (bancada, só a placa) |
| `join` com sucesso, uplink, downlink, ADR (testes N4)         | **Sim**          |

Para a última linha o TTN atende bem:

- **Região:** o TTN usa no Brasil o plano *AU915-928 FSB 2* (canais 8–15 + canal 65,
  916,8–918,2 MHz + 917,5 MHz). É o mesmo padrão do driver (Q1).
- **Gateway:** precisa de **um gateway AU915 ao alcance do nó, conectado ao TTN**. Pode ser
  o "outro gateway" que você mencionou: qualquer gateway LoRaWAN AU915 (Semtech UDP
  packet forwarder ou LoRa Basics Station) pode ser registrado no TTN. Ele precisa estar
  configurado na **sub-banda 2**. Um gateway em outra sub-banda não escuta o nó, e o
  sintoma é só `Join failed`.
- **Cobertura comunitária:** pode haver gateways TTN públicos na região. Para bancada,
  um gateway próprio na mesma sala é o mais confiável.
- **Módulo:** o Wio-E5 está no *LoRaWAN Device Repository* do TTN (fabricante Seeed
  Studio), então o cadastro preenche versão MAC e parâmetros regionais.
- **Custo:** gratuito, com limites de uso justo (§16.4) suficientes para testes.

### 16.2 Riscos

| Risco                                                          | Mitigação                                                             |
|----------------------------------------------------------------|-----------------------------------------------------------------------|
| Gateway em sub-banda diferente do nó                           | Conferir no console do TTN que o gateway está em `AU_915_928_FSB_2`   |
| Gateway e aplicação em *clusters* TTN diferentes               | Registrar os dois no mesmo cluster (ex.: `nam1` ou `au1`)             |
| Versão LoRaWAN do módulo ≠ cadastro no TTN                     | Conferir `AT+LW=VER` (§15.1 item 8) com o cadastro                    |
| Nó muito perto do gateway (saturação do receptor)              | Manter alguns metros e uma parede entre eles, ou reduzir `AT+POWER`   |
| Limite de *fair use* em testes de longa duração                | Com 11 bytes: intervalo ≥ 18 min em SF10, ≥ 3 min em SF7 (§16.4)      |

### 16.3 Passo a passo (feito uma vez, fora do firmware)

1. Criar conta no TTN e uma *application* no cluster escolhido.
2. Registrar o gateway nesse cluster com o plano *Australia 915–928 MHz, FSB 2*.
3. Ler o DevEUI do módulo por terminal serial (9600 8N1): `AT+ID=DevEui`.
4. Registrar o *end device* pelo Device Repository (Seeed Studio → Wio-E5, plano FSB 2),
   usando o DevEUI lido, um JoinEUI (pode ser `0000000000000000`) e deixando o TTN gerar
   a AppKey.
5. Gravar no módulo, pelo terminal, uma única vez:
   `AT+ID=AppEui,"<JoinEUI>"` e `AT+KEY=APPKEY,"<AppKey>"`.
   A AppKey não vai para o repositório (Q3, §13).
6. Na bancada: `AT+MODE=LWOTAA`, `AT+DR=AU915`, `AT+CH=NUM,8-15,65`, `AT+JOIN`. Ver o
   join no *Live data* do TTN. Essa sessão manual também gera as fixtures da §15.1.

### 16.4 Limites do TTN que afetam os testes

- **Uplink:** até 30 s de tempo no ar por dispositivo por dia. Um uplink de 11 bytes leva
  ~0,4 s em SF10/125 kHz e ~0,06 s em SF7. Isso dá algo como 70 a 500 uplinks por dia,
  dependendo do DR.
- **Downlink:** até 10 por dispositivo por dia, contando os ACKs de uplinks confirmados.
  Por isso o padrão é uplink **não confirmado** (Q5). O teste HIL-04 (downlink) usa um
  downlink agendado manualmente no console.

---

## 17. Contratos definidos na fase de testes

Os testes do host (`firmware/node_device/tests/`) fixaram estes detalhes, que antes estavam
em aberto ou implícitos. A implementação deve segui-los. Mudar algum deles exige mudar a
spec e o teste juntos.

### 17.1 Módulos internos e headers

| Header                     | Camada | Visibilidade                                         |
|----------------------------|--------|------------------------------------------------------|
| `inc/wioe5.h`              | API    | pública                                              |
| `inc/wioe5_config.h`       | —      | pública (tamanhos, sobrescrevíveis com `-D`)          |
| `inc/wioe5_ringbuf.h`      | L0     | interna; em `inc/` só porque `wioe5_t` a embute      |
| `inc/wioe5_line.h`         | L1     | interna; idem                                        |
| `src/wioe5_parse.h`        | L2     | interna (testada diretamente)                        |
| `src/wioe5_hex.h`          | L2     | interna (testada diretamente)                        |

`wioe5_config_default()` substitui a macro `WIOE5_CONFIG_DEFAULT` citada na §11.

### 17.2 Regras fixadas

| Assunto                         | Regra                                                                                                   |
|---------------------------------|---------------------------------------------------------------------------------------------------------|
| Ring buffer                     | índices livres de 32 bits; tamanho potência de 2; **capacidade = tamanho** (não tamanho − 1)            |
| Montador de linhas              | `\r` e `NUL` descartados em qualquer posição; overflow reportado uma vez por linha                      |
| `ERROR(...)` malformado         | não é erro (`is_error = false`); no motor vira `WIOE5_ERR_UNEXPECTED`                                   |
| `start_*`                       | só enfileira; o primeiro byte sai no próximo `wioe5_process()`                                          |
| Guarda de RX                    | conta a partir do último byte recebido; sem bytes recebidos, não há espera                              |
| Tempo de TX no timeout          | `ceil(len × 1,1)` ms somado ao timeout do passo; o prazo conta a partir do `write()`                    |
| Timeout                         | dispara no `process()` em que `now − t_write ≥ limite` (exatamente no limite)                           |
| `wioe5_init`                    | `WIOE5_ERR_PARAM` se `write`/`now_ms`/`band` forem nulos, `default_port` fora de 1..223 ou `rept == 0`  |
| Eco no setup                    | conferido em `MODE` (valor), `ADR` (ON/OFF) e `PORT`/`RETRY`/`REPT` (número). `DR`, `CH` e `LOG` aceitam qualquer resposta sem erro até a validação em bancada (§15.1) |
| Probe `AT` no setup             | até 3 tentativas; erro ou silêncio contam como tentativa falha                                          |
| Probe `AT` no wake              | até 2 tentativas; se falhar, o driver continua considerando o módulo dormindo                            |
| `AT+LOG=QUIET`                  | opcional: erro **ou timeout** não abortam o setup                                                       |
| `send` com `len == 0`           | envia `AT+MSGHEX`/`AT+CMSGHEX` sem `=` e **pula** a checagem `AT+LW=LEN`                                |
| `send` antes do join            | permitido; o próprio módulo responde `Please join network first`                                        |
| Payload                         | copiado em `start_send`; o buffer do chamador pode ser reutilizado logo depois                          |
| Downlink                        | entregue em `on_downlink` no `Done`, antes de `on_op_done`. Sem linha de RSSI: `rssi = INT16_MIN` e `rx_window = 0xFF` |
| `JOIN: Done` sem `NetID`        | `WIOE5_ERR_UNEXPECTED` (a confirmar em bancada)                                                         |
| URC `+JOIN: NetID … / Done` fora de operação | atualiza `joined`/`dev_addr` e emite `WIOE5_EVT_JOINED`                                     |
| URC `+LOWPOWER: WAKEUP` com o módulo dormindo | `sleeping = false`, estado `IDLE`, evento `WIOE5_EVT_WOKE`                                 |
| `start_sleep`                   | `WIOE5_ERR_STATE` antes do setup ou se o módulo já estiver dormindo                                     |
| `start_wake`                    | `WIOE5_ERR_STATE` se o módulo não estiver dormindo                                                      |
| `start_reset`                   | permitido em `UNINIT`, `IDLE` e `SLEEPING`. Sem pino e dormindo: acorda antes do `AT+RESET`. Termina em `UNINIT` mesmo com sucesso |
| `wioe5_can_stop`                | `true` em `UNINIT` ou `SLEEPING`, sem operação e com ring buffer vazio                                  |
| `wioe5_wait`                    | sem operação em andamento, retorna `wioe5_last_result()` na hora                                        |

---

## 18. Integração no firmware do nó

O driver é usado pela camada de aplicação em `node/App/`:

| Arquivo          | Papel                                                                                   | HAL? |
|------------------|-----------------------------------------------------------------------------------------|------|
| `app_lorawan.c/h`| Ciclo de uplink e política de retentativa (D8). Testado no host (`test_app_lorawan.c`) | não  |
| `app_node.c/h`   | Instâncias, callbacks da HAL (`HAL_UART_RxCpltCallback`/`ErrorCallback`), payload e política de ociosidade | sim  |

`main.c` só chama `app_node_init(&huart1)` em `USER CODE 2` e `app_node_process()` no laço
(`USER CODE 3`). O código gerado pelo CubeMX não foi alterado fora das seções USER CODE.

### 18.1 Ciclo e política

```
setup ──ok──▶ join ──ok──▶ send ─▶ sleep do módulo ─▶ espera o período ─┐
  ▲  falha:      │ falha:                                               │
  │  espera 10 s │ backoff 15 s → 30 s → … → 30 min                     │
  │              │ (nunca menos que "No band in N ms")                  │
  │              ◀── "Please join network first" no send ───────────────┤
  └── reset do módulo ◀── WIOE5_EVT_UNRESPONSIVE (3 timeouts seguidos)  │
                                                   próximo send ◀───────┘
```

| Parâmetro (`app_lorawan_cfg_t`) | Padrão  |
|---------------------------------|---------|
| `uplink_period_ms`              | 15 min  |
| `fport`                         | 1       |
| `confirmed`                     | `false` |
| `join_backoff_min_ms` / `max`   | 15 s / 30 min |
| `fail_backoff_ms`               | 10 s    |

O período conta do início de um envio ao início do próximo.

### 18.2 Provisório até existir o resto do nó

- **Payload:** enquanto o driver do sensor ultrassônico não existe, o nó envia um
  *heartbeat* de 3 bytes, `[0x00][contador MSB][contador LSB]`. Ele prova o enlace de ponta
  a ponta sem inventar uma leitura de nível. O formato de todos os uplinks (heartbeat,
  `status` no boot e a cada 24 h, e a futura `measurement`) está em
  [`docs/payload/payload_spec.md`](../payload/payload_spec.md).

### 18.3 Baixo consumo do MCU (Stop 2 com RTC)

RTC no LSE (32,768 kHz, PC14/PC15), prescalers 127/255 (1 Hz), *wakeup timer* interno com
interrupção `RTC_IRQn`. O CubeMX liga o *wakeup timer* no init; `USER CODE RTC_Init 2`
desliga-o, e a aplicação só o programa antes de cada Stop 2.

`idle_mcu()` em `app_node.c` decide a cada volta do laço:

| Condição                                                          | Modo              |
|-------------------------------------------------------------------|-------------------|
| `app_lorawan_can_idle()` falso (operação em andamento)            | nenhum (roda)     |
| espera < 1,5 s **ou** `wioe5_can_stop()` falso (módulo acordado)  | Sleep (`__WFI`)   |
| espera ≥ 1,5 s **e** módulo dormindo                              | **Stop 2**        |

Sequência do Stop 2:
1. Lê a hora do RTC (com subsegundos).
2. Programa o *wakeup* para `floor(espera / 1 s)` segundos (máximo 65 536 s); o resto
   da espera roda em Sleep.
3. `HAL_SuspendTick()` e `HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI)`.
4. Ao acordar: `SystemClock_Config()`, desliga o *wakeup timer* e soma ao `uwTick` o tempo
   dormido medido no RTC. Assim `HAL_GetTick()` continua refletindo o tempo real, e o
   período do ciclo e os timeouts do driver não se perdem.
5. `HAL_ResumeTick()` e `wioe5_port_stm32_resume()` (rearma a RX da UART).

Em builds Debug, `HAL_DBGMCU_EnableDBGStopMode()` mantém o depurador conectado durante o
Stop 2. Isso aumenta o consumo; **medições de corrente devem usar a build Release**.

A lógica de "quanto dormir" (`app_lorawan_idle_ms`) é testada no host (APP-07/08). A
entrada e a saída do Stop 2 em si só podem ser validadas na placa (HIL-09/10).

### 18.4 Chaves

O firmware não grava AppEUI/AppKey (`provision = NULL`). Elas precisam ser gravadas uma
vez no módulo pelo terminal (§16.3).
