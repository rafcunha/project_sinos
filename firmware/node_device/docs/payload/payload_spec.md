# Formato do payload LoRaWAN do nó — Especificação

| Campo      | Valor                                                                    |
|------------|--------------------------------------------------------------------------|
| Componente | `node_payload` (codificador/decodificador C) + *payload formatter* do TTN |
| Escopo     | mensagens **uplink** do nó (nó → servidor). Downlinks ficam para o passo 6.6 |
| Status     | **Aprovado** pelo usuário em 2026-11-19 (Q1–Q5 como propostas) e **implementado** (§6) |

---

## 1. Objetivo

Definir os bytes que o nó envia, de forma que:

1. caibam em **qualquer** taxa de dados do AU915, mesmo com *dwell time* ligado: **≤ 11 bytes**
   (driver spec §10.4);
2. possam **evoluir** sem quebrar o que já está em campo (versão no próprio payload);
3. digam ao servidor **se a medida é confiável** (flags e qualidade), não só o número;
4. tenham um **único contrato** testado nos dois lados: o mesmo conjunto de vetores de teste
   valida o codificador em C (firmware) e o decodificador em JavaScript (TTN).

Fora do escopo: o driver do sensor ultrassônico, a medição da bateria e da temperatura (ainda
não existem no hardware; os campos são enviados como "desconhecido") e os comandos por downlink.

---

## 2. Decisões de formato

| #  | Decisão | Motivo |
|----|---------|--------|
| F1 | **Todos os uplinks na porta LoRaWAN 1** (`fport = 1`); o tipo vai no 1º byte | um único ponto de entrada no servidor; o tipo e a versão andam juntos no cabeçalho |
| F2 | **1º byte = cabeçalho**: bits 7–5 = **versão** do formato (0–7), bits 4–0 = **tipo** (0–31) | 1 byte para versionar e tipar; com versão 0, o heartbeat atual (`0x00`) continua válido sem mudar nada |
| F3 | **Big-endian** (byte mais significativo primeiro) em todos os campos multibyte | é como o heartbeat atual já é enviado; leitura natural no hexdump do console do TTN |
| F4 | **Inteiros com escala fixa**, sem ponto flutuante | o firmware não usa `float` (driver spec D5); escala documentada por campo |
| F5 | **Valor sentinela = "desconhecido"** em cada campo (ex.: `0xFFFF`) | um campo que o hardware ainda não mede vai como desconhecido, sem mudar o formato quando o sensor chegar |
| F6 | Sem número de sequência no payload | o LoRaWAN já tem o contador de quadros (`FCnt`), visível no servidor |
| F7 | O nó envia a **distância medida**, não o nível do rio (ver Q1) | a altura de instalação fica no servidor: recalibrar não exige regravar o nó |

---

## 3. Cabeçalho (byte 0)

```
 bit   7   6   5 | 4   3   2   1   0
      versão     |      tipo
```

| Tipo | Nome          | Versão atual | Tamanho total | Quando é enviado |
|------|---------------|--------------|---------------|------------------|
| 0    | `heartbeat`   | 0            | 3 bytes       | só enquanto não há sensor (comportamento atual) |
| 1    | `measurement` | 0            | 9 bytes       | a cada ciclo de medição (padrão 15 min) |
| 2    | `status`      | 0            | 10 bytes      | no primeiro envio depois de cada boot e a cada 24 h (ver Q2) |
| 3–31 | reservado     | —            | —             | — |

Cabeçalho com **tipo reservado** ou **versão desconhecida** para o tipo: o decodificador
devolve um **erro** (não tenta adivinhar).

---

## 4. Mensagens

### 4.1 Tipo 0 — `heartbeat` (v0), 3 bytes

Já em uso pelo firmware atual (`app_node.c`), formalizado aqui sem mudança:

| Byte | Campo     | Tipo | Unidade | Faixa      | Desconhecido |
|------|-----------|------|---------|------------|--------------|
| 0    | cabeçalho | u8   | —       | `0x00`     | —            |
| 1–2  | `counter` | u16  | —       | 0–65535, incrementa a cada envio, dá a volta | — |

### 4.2 Tipo 1 — `measurement` (v0), 9 bytes

| Byte | Campo           | Tipo | Unidade / escala      | Faixa válida        | Desconhecido |
|------|-----------------|------|-----------------------|---------------------|--------------|
| 0    | cabeçalho       | u8   | —                     | `0x01`              | —            |
| 1–2  | `distance_mm`   | u16  | mm, sensor → água     | 0–65 534 (até 65,5 m) | `0xFFFF`   |
| 3–4  | `battery_mv`    | u16  | mV                    | 0–65 534            | `0xFFFF`     |
| 5–6  | `temperature_dc`| i16  | 0,1 °C (décimos)      | −400 a +850 (−40,0 a +85,0 °C) | `0x7FFF` (32767) |
| 7    | `flags`         | u8   | bits (§4.4)           | —                   | —            |
| 8    | `samples`       | u8   | nº de leituras válidas usadas na medida | 0–254 | `0xFF` |

- `distance_mm` é a distância já **compensada pela temperatura** quando houver sensor de
  temperatura; sem ele, o bit `TEMP_UNCOMPENSATED` vai ligado.
- `samples` = 0 com `distance_mm = 0xFFFF` significa "nenhum eco válido".
- Sobram **2 bytes** do limite de 11 para evoluções futuras (ex.: dispersão das leituras).

### 4.3 Tipo 2 — `status` (v0), 10 bytes

| Byte | Campo             | Tipo | Unidade        | Faixa       | Desconhecido |
|------|-------------------|------|----------------|-------------|--------------|
| 0    | cabeçalho         | u8   | —              | `0x02`      | —            |
| 1    | `fw_major`        | u8   | —              | 0–255       | —            |
| 2    | `fw_minor`        | u8   | —              | 0–255       | —            |
| 3    | `fw_patch`        | u8   | —              | 0–255       | —            |
| 4    | `reset_cause`     | u8   | enum (§4.5)    | 0–7         | 0 = desconhecida |
| 5–6  | `uptime_h`        | u16  | horas desde o boot | 0–65 534 (~7,5 anos) | `0xFFFF` |
| 7    | `join_failures`   | u8   | desde o boot, satura em 255 | 0–255 | — |
| 8    | `uplink_failures` | u8   | desde o boot, satura em 255 | 0–255 | — |
| 9    | `module_resets`   | u8   | resets do Wio-E5 desde o boot, satura em 255 | 0–255 | — |

- A versão vem de `FW_VERSION` (ex.: `v0.1.0`), gravada pelo CI (AGENTS.md §2.6). Uma build
  local `0.0.0-dev` envia `0.0.0`.
- Os contadores vêm de `app_lorawan_stats` e são reiniciados no boot.

### 4.4 Bits de `flags` (measurement)

| Bit | Nome                  | Significado quando ligado |
|-----|-----------------------|---------------------------|
| 0   | `SENSOR_ERROR`        | o sensor não respondeu ou deu erro; `distance_mm` = desconhecido |
| 1   | `UNSTABLE`            | leituras muito dispersas (ondas, chuva, eco falso); valor menos confiável |
| 2   | `TEMP_UNCOMPENSATED`  | distância sem compensação de temperatura |
| 3   | `BATTERY_LOW`         | bateria abaixo do limiar configurado |
| 4   | `FIRST_AFTER_BOOT`    | primeira medida desde o boot |
| 5–7 | reservados            | enviados como 0; o decodificador ignora |

### 4.5 `reset_cause` (status)

| Valor | Causa |
|-------|-------|
| 0     | desconhecida |
| 1     | power-on / brown-out |
| 2     | pino de reset (NRST) |
| 3     | reset por software |
| 4     | watchdog independente (IWDG) |
| 5     | watchdog de janela (WWDG) |
| 6     | low-power reset |
| 7     | outro |

Vem dos flags `RCC_CSR` do STM32U585 lidos no boot (implementado junto com o watchdog, 6.5;
até lá o nó envia 0).

---

## 5. Exemplos (vetores de teste)

| Mensagem | Bytes (hex) | Decodificado |
|----------|-------------|--------------|
| heartbeat nº 1 | `00 00 01` | `{type: "heartbeat", version: 0, counter: 1}` |
| measurement completa | `01 0B B8 0E 74 00 E1 00 0A` | distância 3000 mm, bateria 3700 mV, 22,5 °C, sem flags, 10 amostras |
| measurement só com distância (hoje: sem bateria nem temperatura) | `01 0B B8 FF FF 7F FF 04 0A` | distância 3000 mm, bateria e temperatura desconhecidas, `TEMP_UNCOMPENSATED` |
| measurement sem eco | `01 FF FF 0E 74 00 E1 01 00` | distância desconhecida, `SENSOR_ERROR`, 0 amostras |
| temperatura negativa | `01 0B B8 0E 74 FF 9C 00 0A` | −10,0 °C |
| status | `02 00 01 00 01 00 18 00 02 00` | fw 0.1.0, power-on, 24 h no ar, 0 falhas de join, 2 falhas de uplink, 0 resets do módulo |
| tipo reservado | `03 00` | **erro**: tipo desconhecido |
| tamanho errado | `01 0B B8` | **erro**: measurement exige 9 bytes |

Todos esses vetores (e outros de borda) ficam em **um arquivo só**,
`tests/fixtures/payload_vectors.json`, lido pelos testes em C **e** pelos testes do JavaScript.

---

## 6. Implementação

| Peça | Onde | Notas |
|------|------|-------|
| Codificador + decodificador C | `node/Lib/node_payload/` (`node_payload.h/.c`) | sem HAL, sem `float`, sem `malloc`; `encode_*` retorna o tamanho ou 0 se o buffer não couber; `decode` devolve a mensagem tipada ou um erro (`EMPTY`, `UNKNOWN_TYPE`, `UNKNOWN_VERSION`, `BAD_LENGTH`). O codificador zera os bits reservados de `flags`; o decodificador os mantém crus |
| Política de mensagens | `node/App/app_payload.c/h` (sem HAL) | escolhe a mensagem de cada envio: `status` no primeiro envio após o boot e depois de 24 h de *uptime* desde o último `status` (contadas a partir do envio, para não gerar rajada depois de um longo período sem rede); `heartbeat` nos demais. Mantém o *uptime* em 64 bits a partir do tick de 32 bits (que dá a volta em 49,7 dias) |
| Testes C | `tests/test_payload.c`, `tests/test_app_payload.c` | vetores do JSON + casos de borda (sentinelas, saturação, buffer pequeno, versão desconhecida, volta do tick) |
| Vetores | `tests/fixtures/payload_vectors.json` | lido direto pelo JS; para o C, o CMake gera `payload_vectors.h` no configure (`tests/cmake/payload_vectors.cmake`, com o parser JSON do próprio CMake, sem Python). Cada vetor válido tem `fields` (valores crus, para o C) e `ttn` (saída esperada do formatter) |
| *Payload formatter* do TTN | `firmware/node_device/ttn/uplink_formatter.js` | `decodeUplink(input)` no formato do TTN (`{data, warnings, errors}`); ES5 puro para o motor JavaScript do TTN; desconhecido vira `null`; os erros começam com o código do vetor (`unknown_type: …`); `fPort` diferente de 1 é erro; `reset_cause` fora da faixa vira `"unknown"` com um *warning* |
| Testes JS | `firmware/node_device/ttn/uplink_formatter.test.js` | Node.js puro (`node --test ttn/uplink_formatter.test.js`), sem dependências |
| CI | job `ttn-formatter` no `ci.yml` | Node 22 |
| Integração no firmware | `App/app_node.c` | `app_payload_build` é a função de payload do ciclo; `app_payload_tick` roda a cada volta do laço. `measurement` entra quando houver sensor (6.2) |

Saída do formatter (`data`):

| Tipo | Campos |
|------|--------|
| `heartbeat` | `type`, `version`, `counter` |
| `measurement` | `type`, `version`, `distance_m`, `battery_v`, `temperature_c`, `flags` (`sensor_error`, `unstable`, `temp_uncompensated`, `battery_low`, `first_after_boot`), `samples` |
| `status` | `type`, `version`, `firmware` (ex.: `"0.1.0"`), `reset_cause` (`unknown`, `power_on`, `pin`, `software`, `iwdg`, `wwdg`, `low_power`, `other`), `uptime_h`, `join_failures`, `uplink_failures`, `module_resets` |

Limitações conhecidas:
- O `status` conta como enviado quando o payload é montado. Se aquele uplink (não confirmado)
  se perder, o próximo `status` só sai 24 h depois.
- `reset_cause` vai como 0 (desconhecida) até o watchdog (6.5) ler os flags do `RCC_CSR`.

O decodificador em C existe para os testes de ida e volta (codifica → decodifica → compara) e
para uso futuro no gateway/backend.

**Instalar no TTN:** *Application → Payload formatters → Uplink → Custom Javascript
formatter*, colando o conteúdo inteiro de `ttn/uplink_formatter.js`.

---

## 7. Questões (aprovadas em 2026-11-19, todas como propostas)

| #  | Questão | Decisão |
|----|---------|---------|
| Q1 | O nó envia a **distância** sensor→água (servidor calcula o nível com a altura de instalação) ou já calcula o **nível**? | **distância**: recalibrar não exige mexer no nó; o nível pode ser calculado no formatter do TTN quando a altura for conhecida |
| Q2 | Frequência do `status` | no primeiro envio após cada boot e a cada **24 h** |
| Q3 | Enviar bateria e temperatura como "desconhecido" desde já, mesmo sem hardware para medir? | **sim**: o formato fica estável; quando o hardware existir, só os valores mudam |
| Q4 | Manter o heartbeat (tipo 0) depois que o sensor existir? | **não**: com sensor, cada ciclo envia `measurement`; o tipo 0 fica para testes de enlace |
| Q5 | Resolução da distância em **mm** (u16, até 65,5 m) é suficiente? | **sim**: sensores ultrassônicos comuns têm resolução de ~1 cm e alcance < 10 m |
