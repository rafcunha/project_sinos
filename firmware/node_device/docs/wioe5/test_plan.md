# Driver Wio-E5 — Plano de testes

Complementa [driver_spec.md](driver_spec.md). Os IDs de requisito/seção citados (§x.y) são
daquele documento.

## 1. Estratégia

| Nível | O quê                                    | Onde roda                 | Quando                       |
|-------|------------------------------------------|---------------------------|------------------------------|
| N1    | Testes unitários das camadas L0–L2       | PC (gcc do host + Unity)  | a cada commit / CI           |
| N2    | Testes de operação com **modem simulado** (L3–L4) | PC                | a cada commit / CI           |
| N3    | Replay de **transcrições reais** capturadas em bancada | PC           | a cada commit / CI           |
| N4    | Hardware-in-the-loop (STM32 + Wio-E5 + gateway) | bancada           | antes de merge de release    |

Princípio: toda lógica do driver é exercitada no PC. A porta STM32 é fina o bastante para
ser validada só no N4.

### 1.1 Ferramentas

- **Unity** (ThrowTheSwitch) como framework — C puro, pequeno, vendorizado em
  `tests/vendor/unity/` (sem download no build).
- **CMake + Ninja + gcc** do host. No Windows: MSYS2 UCRT64 (`mingw-w64-ucrt-x86_64-gcc`,
  `cmake`, `ninja`) ou WSL. *Hoje nenhum desses está no PATH desta máquina* — instalar é o
  primeiro passo da fase de testes.
- Flags: `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined` (sanitizers só no Linux/WSL/CI).
- Cobertura: `gcov`/`gcovr`, meta ≥ 90 % de linhas em `Lib/wioe5/src/`.
- CI: GitHub Actions (`ubuntu-latest`) rodando `ctest` em todo push/PR para `develop` e `main`.

### 1.2 Estrutura

```
firmware/node_device/tests/
├── CMakeLists.txt
├── vendor/unity/
├── support/
│   ├── fake_port.c/.h      relógio controlado + captura de TX + injeção de RX
│   └── modem_sim.c/.h      roteiro: "espera TX X → responde linhas Y após t ms"
├── fixtures/               transcrições reais (§15.1 da spec), formato abaixo
├── test_ringbuf.c
├── test_line.c
├── test_parse.c
├── test_hex.c
├── test_engine.c
├── test_op_setup.c
├── test_op_join.c
├── test_op_send.c
├── test_op_power.c          sleep / wake / reset
└── test_replay.c
```

### 1.3 Dublês de teste

**`fake_port`**
- `now_ms()` retorna um contador que só o teste avança (`fake_clock_advance(ms)`).
- `write()` concatena em um buffer inspecionável; pode ser configurado para falhar.
- `set_reset()` registra a sequência de níveis e o instante de cada mudança.

**`modem_sim`** — roteiro declarativo:

```c
static const sim_step_t script[] = {
    { .expect = "AT\r\n",          .reply = { {5, "+AT: OK"} } },
    { .expect = "AT+VER\r\n",      .reply = { {5, "+VER: 4.0.11"} } },
    { .expect = "AT+JOIN\r\n",     .reply = { {10, "+JOIN: Start"},
                                              {10, "+JOIN: NORMAL"},
                                              {6000, "+JOIN: NetID 000013 DevAddr 26:0B:12:34"},
                                              {5, "+JOIN: Done"} } },
};
```
O simulador injeta os bytes via `wioe5_rx_isr()` à medida que o relógio falso avança,
falha o teste se o driver enviar algo diferente de `expect`, e no fim confere que todo o
roteiro foi consumido. Isso testa ordem dos comandos, temporização e parsing juntos.

**Formato das fixtures (N3)**

```
> AT+MSGHEX="0102"
< +MSGHEX: Start
< +MSGHEX: Done
= OK
```
`>` = enviado pelo host, `<` = recebido, `=` = status esperado. O teste de replay converte
cada arquivo em um roteiro do `modem_sim`.

---

## 2. Casos de teste

Convenção: **TC-<área>-<nn>**. "Esperado" descreve o que o teste verifica.

### 2.1 L0 — Ring buffer (`test_ringbuf.c`)

| ID        | Cenário                                                       | Esperado                                               |
|-----------|---------------------------------------------------------------|--------------------------------------------------------|
| TC-RB-01  | Buffer novo                                                    | vazio; `pop` falha                                     |
| TC-RB-02  | Push/pop de 1 byte                                             | mesmo byte, buffer vazio depois                        |
| TC-RB-03  | Preenche até a capacidade (= tamanho, §17.2 da spec)          | todos os pushes aceitos, ordem FIFO preservada          |
| TC-RB-04  | Push com buffer cheio                                          | byte descartado, contador de overflow +1, conteúdo intacto |
| TC-RB-05  | Wrap-around (N ciclos de push/pop > capacidade)                | ordem e conteúdo corretos                              |
| TC-RB-06  | Índices perto de `UINT32_MAX`                                  | sem corrupção no estouro do contador                   |

### 2.2 L1 — Montador de linhas (`test_line.c`)

| ID        | Entrada (bytes)                                  | Esperado                                               |
|-----------|--------------------------------------------------|--------------------------------------------------------|
| TC-LN-01  | `+AT: OK\r\n`                                    | 1 linha `+AT: OK`                                      |
| TC-LN-02  | `+AT: OK\n` (sem `\r`)                           | 1 linha                                                |
| TC-LN-03  | Linha entregue byte a byte, em várias chamadas  | 1 linha idêntica                                       |
| TC-LN-04  | Duas linhas no mesmo bloco                       | 2 linhas, na ordem                                     |
| TC-LN-05  | `\r\n\r\n` (linhas vazias)                       | nenhuma linha                                          |
| TC-LN-06  | `0x55` `+LOWPOWER: WAKEUP\r\n`                   | linha `+LOWPOWER: WAKEUP`                              |
| TC-LN-07  | `FF FF FF FF +AT: OK\r\n`                        | linha `+AT: OK`                                        |
| TC-LN-08  | Texto sem `+` (`garbage\r\n`)                    | descartada; `unknown_lines` +1                         |
| TC-LN-09  | Linha com `WIOE5_LINE_MAX` caracteres            | aceita                                                 |
| TC-LN-10  | Linha com `WIOE5_LINE_MAX + 1` caracteres + linha válida | 1ª descartada com evento de overflow; 2ª entregue intacta |
| TC-LN-11  | Byte `0x00` no meio da linha                     | byte descartado; linha entregue sem ele                |

### 2.3 L2 — Parser genérico e específico (`test_parse.c`, tabela)

| ID        | Linha                                                   | Esperado                                           |
|-----------|---------------------------------------------------------|----------------------------------------------------|
| TC-PR-01  | `+AT: OK`                                               | name=`AT`, body=`OK`, sem erro                      |
| TC-PR-02  | `+at: ok`                                               | casa com `AT` (case-insensitive)                   |
| TC-PR-03  | `+MSG: ERROR(-12)`                                      | erro, código −12                                   |
| TC-PR-04  | `+CMSGHEX: ERROR(-1)`                                   | erro, código −1                                    |
| TC-PR-05  | `+AT: ERROR(abc)` / `+AT: ERROR(`                       | `is_error = false` (o motor trata como `UNEXPECTED`) |
| TC-PR-06  | `+VER: 4.0.11`                                          | versão `"4.0.11"`                                  |
| TC-PR-07  | `+ID: DevEui, 2C:F7:F1:20:42:00:12:34`                  | 8 bytes corretos                                   |
| TC-PR-08  | `+ID: DevEui, 2C:F7:F1` (curto) / dígito inválido       | falha de parse                                     |
| TC-PR-09  | `+LW: LEN, 50` / `+LW: LEN, 0`                          | 50 / 0                                             |
| TC-PR-10  | `+JOIN: NetID 000013 DevAddr 26:0B:12:34`               | DevAddr = 26 0B 12 34                              |
| TC-PR-11  | `+JOIN: Join failed`                                    | `JOIN_FAILED`                                      |
| TC-PR-12  | `+JOIN: Joined already`                                 | sucesso de join                                    |
| TC-PR-13  | `+MSG: No band in 13469ms`                              | `NO_BAND`, 13469                                   |
| TC-PR-14  | `+MSG: No free channel -70` / `+MSG: DR error`          | `NO_FREE_CH` / `DR`                                |
| TC-PR-15  | `+MSG: Length error 0` / `Length error 12`              | `LENGTH`, N = 0 / 12                               |
| TC-PR-16  | `+MSG: LoRaWAN modem is busy`                           | `MODEM_BUSY`                                       |
| TC-PR-17  | `+MSG: Please join network first`                       | `NOT_JOINED`                                       |
| TC-PR-18  | `+MSGHEX: PORT: 8; RX: "12345678"`                      | porta 8, 4 bytes `12 34 56 78`                     |
| TC-PR-19  | `+MSGHEX: PORT: 8; RX: ""`                              | porta 8, 0 bytes                                   |
| TC-PR-20  | `RX: "123"` (ímpar) / `RX: "12GZ"` / 243 bytes          | downlink inválido                                  |
| TC-PR-21  | `+MSG: RXWIN1, RSSI -93, SNR 6.25`                      | janela 1, −93 dBm, 625 cdB                         |
| TC-PR-22  | `+MSG: RXWIN2, RSSI -106, SNR -7.5` / `SNR 4`           | −750 / 400 cdB                                     |
| TC-PR-23  | `+MSG: Link 20, 1`                                      | margem 20, 1 gateway                               |
| TC-PR-24  | `+MSG: ACK Received` / `+CMSGHEX: Wait ACK`             | flag ack / ignorado                                |
| TC-PR-25  | `+LOWPOWER: SLEEP` / `+LOWPOWER: WAKEUP`                | reconhecidos                                       |
| TC-PR-26  | `+INFO: Input timeout`                                  | URC reconhecido                                    |
| TC-PR-27  | Espaços extras: `+PORT:   8  `                          | valor 8                                            |

### 2.4 Utilitários hex (`test_hex.c`)

| ID        | Cenário                                     | Esperado                                  |
|-----------|---------------------------------------------|-------------------------------------------|
| TC-HX-01  | Encode `{0x00,0xAB,0xFF}`                   | `"00ABFF"` (maiúsculas, sem espaço)       |
| TC-HX-02  | Encode 0 bytes                              | `""`                                      |
| TC-HX-03  | Encode que não cabe no buffer               | erro, buffer não estourado                |
| TC-HX-04  | Decode minúsculas/maiúsculas                | iguais                                    |
| TC-HX-05  | Decode ímpar / caractere inválido / estouro | erro                                      |
| TC-HX-06  | Round-trip de 242 bytes aleatórios          | idêntico                                  |

### 2.5 L3 — Motor de comandos (`test_engine.c`)

| ID        | Cenário                                                           | Esperado                                                   |
|-----------|-------------------------------------------------------------------|------------------------------------------------------------|
| TC-EN-01  | Comando com resposta dentro do prazo                              | `OK`; TX exatamente `"AT\r\n"`                             |
| TC-EN-02  | Sem resposta; relógio avança até `timeout − 1`                    | ainda ocupado                                              |
| TC-EN-03  | Relógio avança até `timeout`                                      | `TIMEOUT`; `consecutive_timeouts` = 1                       |
| TC-EN-04  | 3 timeouts seguidos                                               | evento `UNRESPONSIVE` emitido uma vez                      |
| TC-EN-05  | Resposta após timeouts                                            | `consecutive_timeouts` volta a 0                           |
| TC-EN-06  | Timeout com relógio passando por `UINT32_MAX`                     | prazo calculado corretamente                               |
| TC-EN-07  | Linha de outro comando chega durante a espera                     | ignorada/URC; comando atual continua                       |
| TC-EN-08  | Bytes chegando continuamente antes do envio                       | TX adiado até `T_GUARD` de silêncio                        |
| TC-EN-09  | `port->write()` retorna erro                                      | `UART` imediato                                            |
| TC-EN-10  | Payload grande: timeout inclui tempo de TX                        | não estoura timeout com 500 B de comando                   |
| TC-EN-11  | `start_*` com operação em andamento                               | `BUSY`; operação atual intacta                             |
| TC-EN-12  | `on_op_done` inicia nova operação                                 | aceita (driver já está `IDLE`)                             |
| TC-EN-13  | Overflow do ring buffer durante comando                           | operação termina com `OVERFLOW`                            |
| TC-EN-14  | `wioe5_wait()` com `idle_hook`                                    | hook chamado; retorna o status final                        |

### 2.6 L4 — Setup (`test_op_setup.c`)

| ID        | Cenário                                                            | Esperado                                              |
|-----------|--------------------------------------------------------------------|-------------------------------------------------------|
| TC-SU-01  | Caminho feliz com config padrão                                    | sequência exata §10.1; `IDLE`; versão e DevEUI salvos |
| TC-SU-02  | 1º `AT` sem resposta, 2º OK                                        | sucesso                                               |
| TC-SU-03  | 3 `AT` sem resposta                                                | `TIMEOUT`; estado `UNINIT`                            |
| TC-SU-04  | `AT+LOG=QUIET` retorna `ERROR(-10)`                                | ignorado (opcional); setup continua                   |
| TC-SU-05  | `AT+MODE=LWOTAA` retorna `ERROR(-1)`                               | `AT`, código −1; `UNINIT`; comandos seguintes não enviados |
| TC-SU-06  | `channel_mask = NULL`                                              | `AT+CH=NUM` não é enviado                             |
| TC-SU-07  | ADR off, DR 3                                                      | envia `AT+ADR=OFF` e `AT+DR=3`                        |
| TC-SU-08  | `provision` preenchido                                             | envia `AppEui` e `APPKEY`; AppKey **não** aparece no log |
| TC-SU-09  | Eco diferente do pedido (`+MODE: LWABP`)                           | `UNEXPECTED`                                          |
| TC-SU-10  | `send`/`join` antes do setup                                       | `STATE`                                               |
| TC-SU-11  | Setup com pino de reset (módulo já em reset pelo boot)             | 1ª ação: `set_reset(true)` mantido ≥ `T_RESET_PULSE`, depois `set_reset(false)`; nenhum byte TX antes de `T_BOOT`; depois `AT` |
| TC-SU-12  | Setup com pino de reset e lixo de boot recebido durante `T_BOOT`   | lixo descartado; probe `AT` OK                        |
| TC-SU-13  | Setup sem pino (`set_reset = NULL`)                                | começa direto pelo probe `AT`                         |

### 2.7 L4 — Join (`test_op_join.c`)

| ID        | Cenário                                                            | Esperado                                         |
|-----------|--------------------------------------------------------------------|--------------------------------------------------|
| TC-JN-01  | Start → NORMAL → NetID/DevAddr → Done                             | `OK`; `joined`; DevAddr salvo                    |
| TC-JN-02  | Variante `Starting` (texto do PDF)                                 | `OK`                                             |
| TC-JN-03  | `Join failed` → `Done`                                             | `JOIN_FAILED` assim que chega `Done`             |
| TC-JN-04  | `Join failed` sem `Done`                                           | `JOIN_FAILED` após `T_JOIN_FAIL_GRACE`           |
| TC-JN-05  | `Joined already`                                                   | `OK`; `joined`                                   |
| TC-JN-06  | `No band in 5000ms`                                                | `NO_BAND`; `retry_after_ms = 5000`               |
| TC-JN-07  | `LoRaWAN modem is busy`                                            | `MODEM_BUSY`                                     |
| TC-JN-08  | Silêncio total                                                     | `TIMEOUT` exatamente em `T_JOIN` (+ TX)          |
| TC-JN-09  | `force = true`                                                     | TX `AT+JOIN=FORCE`                               |
| TC-JN-10  | Driver dormindo                                                    | wake automático antes do `AT+JOIN`               |
| TC-JN-11  | `Done` sem `NetID` antes                                           | `UNEXPECTED` (ou `JOIN_FAILED`; fixar após bancada) |

### 2.8 L4 — Envio (`test_op_send.c`)

| ID        | Cenário                                                            | Esperado                                             |
|-----------|--------------------------------------------------------------------|------------------------------------------------------|
| TC-TX-01  | Não confirmado, 2 bytes, caminho feliz                             | TX `AT+LW=LEN` e `AT+MSGHEX="0102"`; `OK`             |
| TC-TX-02  | `len = 0`                                                          | TX `AT+MSGHEX` (sem `=`)                             |
| TC-TX-03  | Porta diferente da atual                                           | `AT+PORT=<p>` antes; na 2ª vez com a mesma porta, não reenvia |
| TC-TX-04  | Confirmado com `ACK Received`                                      | TX `AT+CMSGHEX=…`; `OK`                              |
| TC-TX-05  | Confirmado sem `ACK Received`                                      | `NO_ACK`                                             |
| TC-TX-06  | Downlink + RSSI no envio                                           | `on_downlink` com porta, dados, RSSI, SNR, janela; antes de `on_op_done` |
| TC-TX-07  | Downlink com hex inválido                                          | evento `BAD_DOWNLINK`; envio termina `OK`            |
| TC-TX-08  | `LEN` = 0 → flush → `LEN` = 50                                     | TX: LEN, MSGHEX vazio, LEN, MSGHEX com dados; `OK`   |
| TC-TX-09  | `LEN` = 0 → flush → `LEN` = 0                                      | `LENGTH`; dados não transmitidos                     |
| TC-TX-10  | `len` > `LEN`                                                      | `LENGTH` sem transmitir payload                      |
| TC-TX-11  | `check_len_before_send = false`                                    | sem `AT+LW=LEN`                                      |
| TC-TX-12  | `Please join network first`                                        | `NOT_JOINED`; `joined = false`                       |
| TC-TX-13  | `No band in Nms`, `No free channel`, `DR error`, `Length error N`, `ERROR(-n)` | status correspondente (teste parametrizado) |
| TC-TX-14  | Linhas com prefixo `+MSG:` em envio `MSGHEX`                       | aceitas                                              |
| TC-TX-15  | `Link 20, 1` durante envio                                         | `link_margin_db = 20`, `link_gw_count = 1`           |
| TC-TX-16  | Argumentos inválidos: `data = NULL` com `len > 0`, `len > MAX`, `fport = 0`, `fport = 224` | `PARAM`, nada transmitido |
| TC-TX-17  | Payload de `WIOE5_MAX_PAYLOAD` bytes                               | comando ≤ 528 caracteres; TX correto                 |
| TC-TX-18  | Timeout confirmado escala com `RETRY`                              | prazo = fórmula §11                                  |

### 2.9 L4 — Energia e reset (`test_op_power.c`)

| ID        | Cenário                                                            | Esperado                                             |
|-----------|--------------------------------------------------------------------|------------------------------------------------------|
| TC-PW-01  | `sleep` → `+LOWPOWER: SLEEP`                                       | `SLEEPING`; `info.sleeping`                          |
| TC-PW-02  | `sleep` sem resposta                                               | `TIMEOUT`; continua `IDLE`                           |
| TC-PW-03  | `wake`: TX `0xFF`, espera ≥ `T_WAKE_SETTLE`, `AT` → OK              | `IDLE`; nenhum byte enviado antes do settle          |
| TC-PW-04  | `wake` com 1º `AT` → `ERROR(-23)`, 2º OK                           | `OK`                                                 |
| TC-PW-05  | `0x55` + `+LOWPOWER: WAKEUP` chegando durante o wake               | tratado como URC; não atrapalha o probe              |
| TC-PW-06  | Em `SLEEPING`, nenhum `process()` gera TX espontâneo                | buffer de TX vazio                                   |
| TC-PW-07  | Reset com pino: pulso ≥ `T_RESET_PULSE`, espera `T_BOOT`, `AT`      | sequência e tempos de `set_reset` corretos; `UNINIT`; `joined = false` |
| TC-PW-08  | Reset sem pino                                                     | TX `AT+RESET`; espera `T_BOOT`; `AT`                 |
| TC-PW-09  | Linhas de boot com lixo após reset                                 | ignoradas                                            |
| TC-PW-10  | `wioe5_can_stop()` em cada estado                                  | `true` só em `SLEEPING`/`UNINIT` sem operação e com ring buffer vazio |
| TC-PW-11  | `can_stop()` com byte pendente no ring buffer                      | `false`                                              |
| TC-PW-12  | Relógio salta (simula Stop 2) com driver em `SLEEPING`, depois `send` | wake + envio normais; nenhum timeout falso         |
| TC-PW-13  | `send` com driver em `SLEEPING`                                    | ordem do TX: byte de wake, `AT`, `AT+LW=LEN`, `AT+MSGHEX` |

### 2.10 N3 — Replay (`test_replay.c`)

> **Pendente:** depende das capturas de bancada (§15.1 da spec). O `test_replay.c` e o
> leitor de fixtures serão escritos junto com as primeiras capturas.

Um caso por arquivo em `tests/fixtures/`. Mínimo exigido, capturado em bancada:

| Fixture                     | Conteúdo                                         |
|-----------------------------|--------------------------------------------------|
| `boot.log`                  | power-on até primeiro `+AT: OK`                  |
| `setup_au915.log`           | sequência completa de setup                       |
| `join_ok.log` / `join_fail.log` | join com e sem gateway                        |
| `msghex_ok.log`             | envio não confirmado sem downlink                 |
| `msghex_downlink.log`       | envio com downlink agendado no servidor           |
| `cmsghex_ack.log` / `cmsghex_noack.log` | confirmado com e sem ack              |
| `lowpower_wake.log`         | sleep + wake, em hexdump (inclui `0x55`)          |

---

## 3. N4 — Hardware-in-the-loop (manual, checklist de release)

Montagem: placa do nó + Wio-E5 + gateway AU915 (sub-banda 2) registrado no The Things
Network + console do TTN (*Live data*), conforme §16 da spec. HIL-01, HIL-06 e HIL-07
não precisam de gateway.

| ID        | Procedimento                                                            | Critério de aceite                                      |
|-----------|-------------------------------------------------------------------------|---------------------------------------------------------|
| HIL-01    | Power-on, `setup`                                                       | `OK` em < 5 s; versão e DevEUI corretos no log           |
| HIL-02    | `join`                                                                  | `OK`; dispositivo aparece "ativado" no servidor          |
| HIL-03    | 10 uplinks não confirmados, 1 a cada 60 s                               | 10/10 recebidos no servidor; payload idêntico           |
| HIL-04    | Downlink agendado no servidor                                           | `on_downlink` com bytes e porta corretos                 |
| HIL-05    | `sleep` / espera 10 min / `wake` / `send`                               | envio OK sem re-join; corrente do módulo em sleep medida e registrada |
| HIL-06    | Gateway desligado → `join`                                              | `JOIN_FAILED` ou `TIMEOUT` no prazo; sem travar o MCU    |
| HIL-07    | Desconectar TX do módulo durante operação                               | `TIMEOUT`; após 3, evento `UNRESPONSIVE`; reset recupera |
| HIL-08    | Teste de longa duração: 24 h com 1 uplink a cada 20 min (dentro do *fair use* do TTN) | 0 travamentos; perda < 5 %; contadores de overflow = 0 |
| HIL-09    | Ciclo completo com STM32 em Stop 2 entre medições (RTC wakeup)          | uplinks continuam sem re-join; corrente média do conjunto medida e registrada |
| HIL-10    | Com o nó em Stop 2, osciloscópio em `PA9`                               | linha estável em nível alto; nenhum `+LOWPOWER: WAKEUP` espúrio |

---

## 4. Situação da implementação dos testes

| Arquivo            | Testes  | Cobre                                                           |
|--------------------|---------|-----------------------------------------------------------------|
| `test_ringbuf.c`   | 8       | TC-RB-00..07                                                    |
| `test_line.c`      | 13      | TC-LN-01..13                                                    |
| `test_parse.c`     | 27      | TC-PR-01..28                                                    |
| `test_hex.c`       | 7       | TC-HX-01..06                                                    |
| `test_engine.c`    | 19      | TC-CF-01..04, TC-EN-01..15 (TC-EN-10 está em `test_op_send.c`)  |
| `test_op_setup.c`  | 18      | TC-SU-01..14                                                    |
| `test_op_join.c`   | 14      | TC-JN-01..13                                                    |
| `test_op_send.c`   | 23      | TC-TX-01..20                                                    |
| `test_op_power.c`  | 19      | TC-PW-01..16                                                    |
| **Total**          | **148** |                                                                 |

Os IDs com sufixo (`b`, `c`) e os números além das tabelas acima são casos acrescentados
durante a escrita. Cada função de teste tem o ID no comentário acima dela.

Os testes foram escritos **antes** do driver. Com os stubs atuais, tudo compila sem
warnings e 144 dos 148 testes falham. Os 4 que passam são testes de rejeição de entrada;
passam por acaso, porque o stub sempre retorna `false`.

Comandos (em `firmware/node_device/tests`):

```
cmake --preset host
cmake --build --preset host
ctest --preset host
```

O CI (GitHub Actions) fica para quando o repositório for enviado ao GitHub.

## 5. Critérios de saída

- Todos os TC de N1–N3 passam no CI, sem warnings, com sanitizers no Linux.
- Cobertura de linhas ≥ 90 % em `Lib/wioe5/src/`; todos os valores de `wioe5_status_t`
  produzidos por ao menos um teste.
- Fixtures de bancada (§2.10) capturadas e passando.
- Checklist N4 executado e anexado ao PR de release.
