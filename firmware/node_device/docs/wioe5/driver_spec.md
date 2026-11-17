# Driver Wio-E5 — Especificação

| Campo        | Valor                                                                 |
|--------------|-----------------------------------------------------------------------|
| Componente   | `wioe5` — driver de comunicação MCU ↔ módulo LoRaWAN Seeed Wio-E5      |
| Host         | STM32U585 (bare-metal, STM32 HAL, sem RTOS)                            |
| Módulo       | Seeed Wio-E5 (STM32WLE5JC) com firmware AT de fábrica (ref. v4.0.x)    |
| Referência   | *LoRa-E5 AT Command Specification V1.0* (Seeed, 2020-07-20)            |
| Status       | **Rascunho para revisão** — nenhuma linha de código escrita ainda      |

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
| Interrupção         | **ainda não habilitada** — ver pré-requisito P1                  |
| Reset do módulo     | **não mapeado** — ver questão em aberto Q4                       |

**Pré-requisitos no CubeMX** (fazer antes da implementação, em branch própria):

- **P1** — Habilitar `USART1 global interrupt` no NVIC. A recepção será por interrupção
  (byte a byte) alimentando um ring buffer. A 9600 baud isso dá ~960 interrupções/s no
  máximo, carga desprezível. DMA circular + IDLE fica como otimização futura.
- **P2** — (Opcional, recomendado) Reservar um GPIO push-pull ligado ao pino `RST` do Wio-E5
  para reset por hardware. Sem ele, a recuperação depende de `AT+RESET`, que não funciona
  se o módulo travar.

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

- `node/CMakeLists.txt`: adicionar `Lib/wioe5/src/*.c` e `port/wioe5_port_stm32.c` em
  `target_sources`, e `Lib/wioe5/inc` em `target_include_directories`.
- `tests/CMakeLists.txt` compila `Lib/wioe5/src/*.c` (sem a porta STM32) com o gcc do host.
- Código gerado pelo CubeMX só é alterado dentro de `/* USER CODE BEGIN … END */`.

---

## 15. Questões em aberto (precisam de resposta antes/durante a implementação)

| #  | Questão                                                                                       | Proposta padrão                                           |
|----|-----------------------------------------------------------------------------------------------|-----------------------------------------------------------|
| Q1 | Região e sub-banda. O nó está no Brasil (Rio dos Sinos/RS)?                                   | `AU915`, sub-banda 2 (canais 8–15 + 65), usada pelo TTN/ChirpStack no Brasil |
| Q2 | Network server (TTN, ChirpStack próprio, Everynet…)?                                          | define sub-banda e se `LW=NET` (rede pública) muda        |
| Q3 | Chaves: gravadas uma vez no módulo via terminal, ou pelo firmware no *cold boot*?             | gravar uma vez via terminal; `provision = NULL`           |
| Q4 | Há trilha do pino `RST` do Wio-E5 para um GPIO do STM32?                                     | recomendado; senão só `AT+RESET`                          |
| Q5 | Uplinks confirmados ou não?                                                                   | não confirmados (economia de energia e duty cycle)        |
| Q6 | `AT+LOG` é persistente?                                                                       | enviar sempre no setup (opcional, barato)                 |
| Q7 | O STM32 vai usar Stop 2 (RAM mantida) ou Standby (reinicia) entre medições?                   | Stop 2: estado do driver sobrevive, `setup` só no cold boot |
| Q8 | Tamanho do payload da aplicação                                                               | ≤ 11 bytes (cabe em qualquer DR do AU915, mesmo com dwell time) |

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
8. Versão do firmware AT do módulo usado (`AT+VER`).
