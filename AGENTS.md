# AGENTS.md — Como agentes (e pessoas) trabalham neste repositório

Este arquivo vale para qualquer agente de código (Claude Code, Codex, Copilot etc.) e para
humanos. Leia inteiro antes de alterar qualquer coisa.

## 0. Comece por aqui: HANDOFF.md

[HANDOFF.md](HANDOFF.md) é a memória do projeto entre sessões: estado atual, o que já foi
validado na placa, ambiente e comandos, armadilhas já encontradas, decisões tomadas, próximos
passos e perguntas em aberto com o usuário.

- **No início de toda sessão**, leia o HANDOFF.md antes de propor ou alterar qualquer coisa.
  Se ele contradisser o código ou o git, confie no código/git e corrija o HANDOFF.md.
- **Mantenha-o atualizado sempre.** Atualize na mesma branch do trabalho, antes do merge em
  `develop`, quando:
  - um passo da lista de próximos passos for concluído ou mudar de escopo;
  - uma decisão for tomada ou revertida (com o usuário);
  - algo for validado (ou falhar) na placa;
  - você descobrir uma armadilha que custou tempo (ferramenta, CubeMX, debugger, git no Windows);
  - mudar o ambiente: ferramentas, caminhos, comandos de build/teste/debug;
  - uma pergunta em aberto for respondida ou surgir outra.
- Atualize também a data e o resumo do topo. Escreva para quem não viu esta conversa:
  fatos verificáveis, com caminhos de arquivo e comandos, e sem "como vimos antes".
- Se a sessão terminar no meio de uma tarefa, registre no HANDOFF.md onde parou, em qual
  branch e o que falta, mesmo que o trabalho não tenha sido mergeado.

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
├── AGENTS.md                   regras do repositório (este arquivo)
└── HANDOFF.md                  estado atual e próximos passos (ler primeiro)
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
5. `main` só recebe código por **pull request** de `release/*` ou `hotfix/*`, com o CI verde,
   e só quando o usuário pedir. Nunca faça merge local em `main` nem push direto nela: a
   proteção do GitHub recusa (§2.7).

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
      em `firmware/node_device/node`; o CI usa `-DNODE_WERROR=ON`).
- [ ] CI verde na branch, se ela já foi enviada ao GitHub (§2.6).
- [ ] Testes do host passam (`firmware/node_device/tests`, ver §4).
- [ ] Código novo tem testes; bug corrigido tem teste que falhava antes.
- [ ] Documentação em `docs/` atualizada se a interface ou o comportamento mudou.
- [ ] Nenhum segredo (AppKey, NwkSKey, AppSKey, tokens) em código, log ou commit.
- [ ] `git status` limpo; sem arquivos de build versionados.
- [ ] O usuário revisou e aprovou o resultado (para mudanças de especificação ou de API).
- [ ] `HANDOFF.md` atualizado com o que mudou (seção 0).

### 2.5 Release e hotfix (PR para `main`)

A `main` é protegida: o código só entra por pull request, depois que os 4 checks do CI
passam. Cada passo que envia algo ao GitHub (push, PR, merge do PR, tag) precisa de
autorização do usuário.

**Release**

```bash
# 1. preparar a release a partir do develop
git switch develop && git pull --ff-only
git switch -c release/1.2.0
# só correções de estabilização, versão, changelog; commits normais
git push -u origin release/1.2.0          # o CI roda na branch

# 2. abrir o PR release/1.2.0 -> main
#    - pela web: https://github.com/rafcunha/project_sinos/compare/main...release/1.2.0
#    - ou: gh pr create --base main --head release/1.2.0 --title "Release v1.2.0"
#    O usuário faz o merge pelo GitHub com "Create a merge commit" (não squash nem rebase).

# 3. depois do merge: tag na main (dispara o GitHub Release com o .bin, §2.6)
git switch main && git pull --ff-only
git tag -a v1.2.0 -m "v1.2.0"
git push origin v1.2.0

# 4. levar a release de volta para o develop (merge local, permitido)
git switch develop && git merge --no-ff release/1.2.0 -m "Merge branch 'release/1.2.0' into develop"
git push origin develop
git branch -d release/1.2.0 && git push origin --delete release/1.2.0
```

**Hotfix:** igual, com `hotfix/1.2.1` criada a partir da `main`
(`git switch main && git pull --ff-only && git switch -c hotfix/1.2.1`), PR `hotfix/1.2.1` → `main`,
tag `v1.2.1` na `main` e merge `--no-ff` de volta no `develop`.

Versionamento: SemVer (`MAJOR.MINOR.PATCH`), tags `vX.Y.Z` só em `main`. Tag com hífen
(`v1.2.0-rc1`) gera *pre-release*.

O `gh` (GitHub CLI) não está instalado nesta máquina. Sem ele, quem abre e mergeia o PR é o
usuário, pela web. Para o agente fazer isso: `winget install --id GitHub.cli -e` e `gh auth login`.

### 2.6 CI/CD (GitHub Actions)

| Workflow | Dispara em | O que faz |
|----------|------------|-----------|
| `.github/workflows/ci.yml` | push em `develop`, `feature/**`, `release/**`, `hotfix/**`; PR para `develop`/`main`; manual | testes do host (`host` e `host-sanitize`) + firmware Debug e Release com `-DNODE_WERROR=ON`; publica `node.elf/.bin/.hex/.map` como artefatos |
| `.github/workflows/release.yml` | push em `main`; tag `v*`; manual | roda o CI com a versão gravada no firmware, empacota `node-<versão>-<sha>.{bin,hex,elf,map}` + `SHA256SUMS.txt`; **em tag `v*` cria o GitHub Release** |

- O **deploy** é o push da tag: depois do merge do PR `release/x.y.z` → `main`,
  `git push origin vX.Y.Z` (com autorização do usuário) gera o Release com o `.bin` (§2.5).
  Tag com hífen (`v1.0.0-rc1`) vira *pre-release*.
- O toolchain ARM do CI é o oficial da Arm **14.3.rel1**, com SHA-256 fixado no `ci.yml`.
  Se mudar a versão no STM32Cube (`node/.settings/bundles-lock.store.json`), atualize
  `ARM_TOOLCHAIN_VERSION` e `ARM_TOOLCHAIN_SHA256` juntos.
- Um CI vermelho bloqueia o merge: corrija na própria branch.

### 2.7 Proteção de branches no GitHub (rulesets)

Configurada pelo usuário em *Settings → Rules → Rulesets*:

| Ruleset   | Alvo      | Regras |
|-----------|-----------|--------|
| `main`    | branch padrão (`main`) | bloquear exclusão e force push; **exigir pull request** (0 aprovações, porque o projeto tem um único desenvolvedor); **exigir os checks** `host-tests (host)`, `host-tests (host-sanitize)`, `firmware-build (Debug)`, `firmware-build (Release)`; sem histórico linear (o git flow usa merge commits) |
| `develop` | `develop` | bloquear exclusão e force push |

As duas também têm *Restrict creations*, o que só impede recriar essas branches se forem
apagadas. Estado conferido ativo em 2026-11-19; para reconferir sem login (repo público):
`curl -s https://api.github.com/repos/rafcunha/project_sinos/rules/branches/<branch>`.

O `develop` **não** exige checks de propósito: os merges de feature são locais (§2.3), e um
commit de merge recém-criado ainda não tem CI, então o push seria recusado. O CI roda logo
depois do push. Se ele ficar vermelho no `develop`, a correção é a próxima prioridade, numa
`feature/fix-*`.

Se um nome de job mudar no `ci.yml`, avise o usuário: o check exigido na ruleset da `main`
precisa ser trocado também, senão nenhum PR consegue ser mergeado.

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
