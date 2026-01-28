# Sistema de Telemetria DVI com Comunicação UART e Resiliência via Watchdog – Raspberry Pi Pico (RP2040)

<p align="center">
  <img src="image.png" alt="EmbarcaTech" width="420">
</p>

![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Raspberry Pi Pico](https://img.shields.io/badge/Raspberry%20Pi%20Pico-00A040?style=for-the-badge&logo=raspberrypi&logoColor=white)
![Pico SDK](https://img.shields.io/badge/Pico%20SDK-1.5%2B-blue?style=for-the-badge)

## Descrição do Projeto

Sistema embarcado que exibe uma IHM via DVI (HDMI) no RP2040, recebendo dados por UART. Além da funcionalidade básica, aplica o Watchdog Timer (WDT) para garantir resiliência: se a recepção de dados travar ou a renderização gráfica ficar instável, o hardware reinicia e a IHM volta automaticamente ao estado inicial.

Para demonstrar, foi implementada uma aplicação de “cofre eletrônico”: um microcontrolador com teclado matricial envia dígitos por UART para outro RP2040 (receptor). O receptor valida a senha, atualiza o painel DVI e só alimenta o Watchdog se ambos os núcleos estiverem saudáveis (Core 0: UART/IHM; Core 1: renderização).

## Visão Geral

- Arquivo principal (Receptor): [hdmi.c](hdmi.c) — inicializa DVI, UART e WDT; gerencia a IHM (prompt, entrada, validação de senha) e a renderização no Core 1.
- Emissor (Teclado): [Teclado.c](Teclado.c) — varre teclado matricial 4×4 e envia dígitos pela UART.
- Fontes e assets: `assets/` e `tmds_*` (fontes, tabelas e rotinas de codificação TMDS para DVI).

## Arquitetura e Fluxo

- Núcleo 0 (Core 0):
  - Recebe dados via UART, atualiza a IHM e o heartbeat do núcleo.
  - Lógica de senha (exibe `*`, valida, mensagens de sucesso/erro, lockout).
- Núcleo 1 (Core 1):
  - Renderiza a saída DVI estável usando `libdvi` e fonte 8×8 com duplicação vertical 3×.
  - Atualiza heartbeat por quadro renderizado (≈ 60 Hz).
- Watchdog (WDT):
  - Timeout padrão: 1000 ms; alimentação do WDT ocorre via timer periódico somente se ambos os heartbeats estiverem “frescos”.
  - Se qualquer núcleo travar, o WDT não é alimentado e reinicia o sistema.

Arquivos relevantes:
- DVI/IHM: [hdmi.c](hdmi.c)
- Teclado/UART emissor: [Teclado.c](Teclado.c)
- Configuração de pinos DVI: [include/common_dvi_pin_configs.h](include/common_dvi_pin_configs.h)

## Hardware e Ligações

Receptor (RP2040 — DVI + UART):
- UART0:
  - TX → GPIO1
  - RX → GPIO0
  - Conectar ao emissor cruzando TX/RX, GND comum.
- Saída DVI/HDMI:
  - Fiação conforme `picodvi` e [include/common_dvi_pin_configs.h](include/common_dvi_pin_configs.h).
  - Clock de bits TMDS a 252 MHz (modo 640×480@60 Hz).
- Botão BOOTSEL opcional no GPIO6 para saída de boot USB.

Emissor (RP2040 — Teclado matricial 4×4 + UART):
- Linhas do teclado (ROWS): GPIO16, GPIO9, GPIO8, GPIO4 (saída).
- Colunas (COLS): GPIO17, GPIO18, GPIO19, GPIO20 (entrada com pull-up).
- LED de atividade: GPIO13 (opcional).
- UART0:
  - TX → GPIO1
  - RX → GPIO0

## Funcionamento (Cofre Eletrônico)

- O receptor exibe título e prompt: “Digite a senha (4 dígitos)”.
- Cada dígito recebido pela UART aparece como `*` na IHM.
- Senha correta (padrão `3333`): tela verde com “Bem vindo”.
- Senha incorreta: tela vermelha e mensagem de erro; após 3 tentativas, lockout de 30 s.
- A IHM redesenha automaticamente o estado inicial após erro ou reinício por WDT.

## Watchdog e Resiliência

- Configuração:
  - `WATCHDOG_TIMEOUT_MS = 1000`
  - Janela de saúde por núcleo: `HEARTBEAT_THRESHOLD_MS = 100`
  - Alimentação do WDT via `add_repeating_timer_ms(50, ...)` apenas quando Core 0 e Core 1 atualizaram seus heartbeats dentro da janela.
- Simulações de falha (para validação):
  - Travamento no Core 0: inserir um laço infinito no início do `while(true)` do `main()` em [hdmi.c](hdmi.c).
  - Travamento no Core 1: inserir laço infinito no início de `core1_main()`.
  - UART problemática: tornar leitura bloqueante sem progresso ou desconectar fisicamente o cabo UART.
- Pós-reset:
  - O sistema detecta `watchdog_caused_reboot()`, limpa/reestrutura a IHM e volta ao prompt inicial.

## Como Compilar e Gravar

### Via Tarefas do VS Code

- Compilar: Task “Compile Project”.
- Carregar `.uf2` via `picotool`: Task “Run Project”.
- Gravar via CMSIS‑DAP/OpenOCD: Task “Flash”.

### Observações

- Pico SDK configurado e ferramentas (`ninja`, `picotool`, `openocd`) disponíveis conforme as tasks.
- Artefato gerado principal: `build/hdmi.uf2`.

## Personalização

- Senha: ajuste `PASSWORD` em [hdmi.c](hdmi.c).
- Watchdog: ajuste `WATCHDOG_TIMEOUT_MS` e `HEARTBEAT_THRESHOLD_MS` conforme sua tolerância a falhas e lockouts.
- UART: altere `UART_BAUD`, `UART_RX_PIN`, `UART_TX_PIN` conforme seu hardware.
- IHM: cores, bordas e mensagens em [hdmi.c](hdmi.c).
- Teclado: mapeamento e pinos em [Teclado.c](Teclado.c).

## Estrutura

- Aplicação receptor: [hdmi.c](hdmi.c)
- Emissor teclado: [Teclado.c](Teclado.c)
- Bibliotecas DVI: `libdvi/`, `libsprite/`
- Build: pasta `build/` (CMake/Ninja), gera `hdmi.uf2`

## Créditos

- `picodvi` e bibliotecas associadas para DVI no RP2040.
