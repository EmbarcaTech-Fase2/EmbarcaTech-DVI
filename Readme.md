# IHM Digital via DVI com Raspberry Pi Pico

Este projeto demonstra a implementação de uma **Interface Homem-Máquina (IHM)** utilizando o microcontrolador **RP2040** (Raspberry Pi Pico W). O sistema realiza a leitura de sinais analógicos e os projeta em tempo real em um monitor através de uma saída digital DVI gerada inteiramente via software.

---


## Visão Geral Técnica
O projeto utiliza uma arquitetura de processamento paralelo e máquinas de estado para superar a ausência de um controlador de vídeo dedicado no hardware original.Está focado na geração de vídeo em tempo real com conector de saída tipo HDMI.

### Arquitetura Dual-Core
Para garantir a estabilidade do sinal de vídeo a 60Hz, as tarefas são divididas entre os núcleos do processador:
* **Core 0 (Lógica e Aquisição):** Inicializa o hardware, realiza a leitura do canal 1 do ADC (GPIO 27) e gerencia os buffers de caracteres (`charbuf`) e cores (`colourbuf`).
* **Core 1 (Renderização em Tempo Real):** Dedicado exclusivamente à geração do sinal DVI, realizando a codificação TMDS e o envio dos dados para o monitor.

### Especificações de Vídeo
* **Resolução:** 640x480p a 60Hz.
* **Escalonamento Vertical (3x):** Implementação de uma lógica que triplica a altura da fonte original (8x8 para 8x24 pixels), garantindo legibilidade superior em telas LCD.
* **Codificação TMDS:** Uso de rotinas em **Assembly** (`tmds_encode_font_2bpp.S`) para converter dados RGB222 para o protocolo digital de forma ultra-rápida.

### Funcionalidades do Projeto
* **Leitura ADC:** Monitoramento contínuo de entrada analógica (0 a 4095) com exibição dinâmica na tela.
* **Interface Visual:** Moldura personalizada (`draw_border`) com texto centralizado e suporte a cores distintas para valores e prompts.

### Principais Características:
* **Protocolo DVI via Software:** Utiliza a biblioteca `libdvi` para implementar o sinal **TMDS** através das máquinas de estado **PIO**, permitindo saída de vídeo digital sem hardware dedicado.
* **Otimização de Memória:** Configurado para gerenciar o stack do **Core 1**, garantindo que o processamento do vídeo ocorra de forma paralela no segundo núcleo.
* **Processamento de Fontes:** Inclui rotinas em Assembly (`tmds_encode_font_2bpp.S`) para codificação rápida, essencial para exibir dados de sensores com baixa latência.

---

## Bibliotecas Vinculadas
O executável final integra:
1. **pico_stdlib & pico_multicore**: Base do sistema e suporte ao processamento paralelo.
2. **libdvi**: Camada de transporte do sinal de vídeo digital.
3. **libsprite**: Motor para gestão de elementos visuais na tela.
4. **hardware_adc**: Leitura de dados analógicos para exibição na IHM.