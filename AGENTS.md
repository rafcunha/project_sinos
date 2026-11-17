# AGENTS.md — Como agentes (e pessoas) trabalham neste repositório

Este arquivo vale para qualquer agente de código (Claude Code, Codex, Copilot etc.) e para
humanos. Leia inteiro antes de alterar qualquer coisa.

## 1. O projeto

**project_sinos** — monitoramento da altura de rio por LoRaWAN.

```
project_sinos/
├── firmware/
│   ├── node_device/            nó sensor (STM32U585 + Wio-E5 + sensor ultrassônico)
│   │   ├── node/               projeto STM32CubeMX (CMake, gcc-arm-none-eabi)
│   │   │   ├── Core/           código gerado pelo CubeMX (+ trechos USER CODE)
│   │   │   ├── Drivers/        HAL/CMSIS da ST — NÃO editar
│   │   │   └── Lib/            bibliotecas do projeto (ex.: Lib/wioe5)
│   │   ├── tests/              testes no host (Unity + CMake)
│   │   └── docs/               especificações e planos de teste
│   └── gateway/                firmware do gateway
└── AGENTS.md
```

## 2. Git flow

### 2.1 Branches

| Branch            | Origem    | Destino (merge)         | Para quê                                   |
|-------------------|-----------|-------------------------|--------------------------------------------|
| `main`            | —         | —                       | só versões liberadas; cada merge = uma tag |
| `develop`         | `main`    | —                       | integração; sempre compila e passa testes  |
| `feature/<nome>`  | `develop` | `develop`               | funcionalidade, driver, refatoração, docs  |
| `release/<x.y.z>` | `develop` | `main` **e** `develop`  | estabilização de uma versão                |
| `hotfix/<x.y.z>`  | `main`    | `main` **e** `develop`  | correção urgente em versão liberada        |

**Regras invioláveis**

1. **Nunca** fazer commit direto em `main` ou `develop`. Todo trabalho começa em uma branch
   `feature/*`, `release/*` ou `hotfix/*`.
2. **Nunca** usar `git push --force` em `main` ou `develop`, nem reescrever histórico delas.
3. **Nunca** pular hooks (`--no-verify`) nem desabilitar assinatura de commits.
4. **Push e criação de PR só com autorização explícita do usuário** na conversa atual.
   Merge local em `develop` é permitido quando o checklist da §2.4 estiver completo.
5. Merge em `main` só via `release/*` ou `hotfix/*`, e só quando o usuário pedir.

### 2.2 Nome de branch

`feature/<escopo>-<descricao-curta>` em kebab-case, sem acentos:

- `feature/wioe5-driver`
- `feature/ultrasonic-sensor`
- `feature/cubemx-usart1-irq`
- `feature/ci-host-tests`

### 2.3 Fluxo de uma feature (passo a passo)

```bash
git switch develop
git pull --ff-only            # só se houver remote e o usuário autorizou rede
git switch -c feature/<escopo>-<descricao>
# ... trabalho em commits pequenos ...
# checklist da §2.4
git switch develop
git merge --no-ff feature/<escopo>-<descricao> -m "Merge branch 'feature/<...>' into develop"
git branch -d feature/<escopo>-<descricao>
```

- Use `--no-ff` para manter visível no histórico onde cada feature começou e terminou.
- Se `develop` andou enquanto você trabalhava: `git rebase develop` **na sua feature**
  (nunca em branches compartilhadas) e rode os testes de novo antes do merge.
- Conflito que você não entende → pare e pergunte ao usuário.

### 2.4 Checklist antes de mergear em `develop` (Definition of Done)

- [ ] Firmware compila sem warnings (`cmake --preset Debug && cmake --build --preset Debug`
      em `firmware/node_device/node`).
- [ ] Testes do host passam (`firmware/node_device/tests`, ver §4).
- [ ] Código novo tem testes; bug corrigido tem teste que falhava antes.
- [ ] Documentação em `docs/` atualizada se a interface ou o comportamento mudou.
- [ ] Nenhum segredo (AppKey, NwkSKey, AppSKey, tokens) em código, log ou commit.
- [ ] `git status` limpo; sem arquivos de build versionados.
- [ ] O usuário revisou e aprovou o resultado (para mudanças de especificação ou de API).

### 2.5 Release e hotfix

```bash
# release
git switch -c release/1.2.0 develop
# só correções, versão, changelog
git switch main    && git merge --no-ff release/1.2.0 && git tag -a v1.2.0 -m "v1.2.0"
git switch develop && git merge --no-ff release/1.2.0
git branch -d release/1.2.0

# hotfix
git switch -c hotfix/1.2.1 main
# correção + teste
git switch main    && git merge --no-ff hotfix/1.2.1 && git tag -a v1.2.1 -m "v1.2.1"
git switch develop && git merge --no-ff hotfix/1.2.1
git branch -d hotfix/1.2.1
```

Versionamento: SemVer (`MAJOR.MINOR.PATCH`), tags `vX.Y.Z` só em `main`.

## 3. Commits

[Conventional Commits](https://www.conventionalcommits.org/), em inglês, imperativo:

```
<tipo>(<escopo>): <resumo em até 72 caracteres>

<corpo opcional: o porquê, não o como>
```

- Tipos: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`, `perf`.
- Escopos comuns: `wioe5`, `sensor`, `node`, `cubemx`, `gateway`, `tests`, `repo`.
- Um commit = uma mudança lógica que compila. Não misturar código gerado pelo CubeMX com
  lógica própria no mesmo commit.
- Agentes terminam a mensagem com a linha de coautoria configurada na sessão.

## 4. Build e testes

| O quê            | Onde                          | Comando                                                        |
|------------------|-------------------------------|----------------------------------------------------------------|
| Firmware (ARM)   | `firmware/node_device/node`   | `cmake --preset Debug` e `cmake --build --preset Debug`        |
| Testes do host   | `firmware/node_device/tests`  | `cmake --preset host` e `cmake --build --preset host` e `ctest --preset host` |

Ferramentas: `arm-none-eabi-gcc` (STM32CubeCLT ou o do STM32CubeIDE), CMake ≥ 3.22, Ninja,
e um gcc do host para os testes (no Windows: `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`).
Bibliotecas de terceiros usadas nos testes ficam vendorizadas e sem modificação em
`tests/vendor/` (hoje: Unity v2.6.0).

### 4.1 Debug na placa

- **VS Code (extensão STM32Cube):** abrir a pasta `firmware/node_device/node`, preset CMake
  `Debug`, e usar a configuração **"Node: Debug (ST-LINK)"** de `.vscode/launch.json`.
- **Linha de comando** (o mesmo servidor que a extensão usa; ferramentas em
  `%LOCALAPPDATA%\stm32cube\bundles`):
  `ST-LINK_gdbserver -e -p 61234 -d -s -k -m 0 -cp <bundles>\programmer\<versão>\bin`
  e `arm-none-eabi-gdb build/Debug/node.elf` com `target extended-remote :61234`.
  O STM32U585 responde no **access port 0** (`-m 0`). Encerre o servidor antes de depurar
  pelo VS Code: a sonda só aceita um cliente.
- **Trace da UART:** builds Debug guardam o tráfego com o Wio-E5 em `wioe5_trace`
  (4 KB circular). Para ler: `set print elements 0` e `print wioe5_trace.buf`. As capturas
  de bancada viram fixtures em `tests/fixtures/`.

Se uma ferramenta não estiver instalada, **diga isso ao usuário** — não marque o checklist
como cumprido sem ter rodado.

## 5. Regras de código (firmware)

- C11. Sem `malloc`/`free`, sem `printf`/`sscanf` em bibliotecas, sem ponto flutuante em
  drivers, sem estado global mutável (estado vive em structs de instância).
- Bibliotecas em `Lib/<nome>/` não incluem headers da HAL; o acesso ao hardware passa por
  uma struct de porta (`*_port_t`) implementada em `Lib/<nome>/port/`.
- Arquivos do CubeMX (`Core/`, `cmake/stm32cubemx/`, `*.ioc`): alterar código **apenas**
  entre `/* USER CODE BEGIN ... */` e `/* USER CODE END ... */`. Mudanças de pinos/periféricos
  são feitas no `.ioc` pelo CubeMX e regeneradas, em commit separado
  (`build(cubemx): ...`).
- `Drivers/` (HAL/CMSIS da ST) nunca é editado à mão.
- Identificadores, comentários de código e mensagens de commit em inglês; documentação em
  `docs/` em português.
- Antes de implementar um componente novo, deve existir uma especificação em
  `docs/<componente>/` aprovada pelo usuário.

## 6. Segredos

Chaves LoRaWAN ficam em `lorawan_secrets.h` (ignorado pelo git). Versione apenas
`lorawan_secrets.example.h` com valores fictícios. Se encontrar um segredo versionado,
pare e avise o usuário — não tente "limpar" o histórico por conta própria.
