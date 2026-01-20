
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h" 
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"
#include "hardware/adc.h" 
#include "dvi.h"
#include "dvi_serialiser.h"
#include "./include/common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"

// Inclusão do arquivo de fonte
#include "./assets/font_8x8.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

struct dvi_inst dvi0;

// Definições do terminal de caracteres
#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)

// Buffers para caracteres e cores
#define COLOUR_PLANE_SIZE_WORDS (CHAR_ROWS * CHAR_COLS * 4 / 32)
char charbuf[CHAR_ROWS * CHAR_COLS];
uint32_t colourbuf[3 * COLOUR_PLANE_SIZE_WORDS];

// Uso do botão B para o BOOTSEL
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events) {
    reset_usb_boot(0, 0);
}

// Função para definir um caractere na tela
static inline void set_char(uint x, uint y, char c) {
    if (x >= CHAR_COLS || y >= CHAR_ROWS)
        return;
    charbuf[x + y * CHAR_COLS] = c;
}

// Função para definir a cor de um caractere (formato RGB222)
static inline void set_colour(uint x, uint y, uint8_t fg, uint8_t bg) {
    if (x >= CHAR_COLS || y >= CHAR_ROWS)
        return;
    uint char_index = x + y * CHAR_COLS;
    uint bit_index = char_index % 8 * 4;
    uint word_index = char_index / 8;
    for (int plane = 0; plane < 3; ++plane) {
        uint32_t fg_bg_combined = (fg & 0x3) | (bg << 2 & 0xc);
        colourbuf[word_index] = (colourbuf[word_index] & ~(0xfu << bit_index)) | (fg_bg_combined << bit_index);
        fg >>= 2;
        bg >>= 2;
        word_index += COLOUR_PLANE_SIZE_WORDS;
    }
}

// --- NOVA FUNÇÃO PARA DESENHAR A BORDA ---
void draw_border() {
    const uint8_t fg = 0x15; // Cor cinza para a borda (RGB222: 010101)
    const uint8_t bg = 0x00; // Fundo preto

    // Cantos
    set_char(0, 0, '+');
    set_colour(0, 0, fg, bg);
    set_char(CHAR_COLS - 1, 0, '+');
    set_colour(CHAR_COLS - 1, 0, fg, bg);
    set_char(0, CHAR_ROWS - 1, '+');
    set_colour(0, CHAR_ROWS - 1, fg, bg);
    set_char(CHAR_COLS - 1, CHAR_ROWS - 1, '+');
    set_colour(CHAR_COLS - 1, CHAR_ROWS - 1, fg, bg);

    // Linhas horizontais
    for (uint x = 1; x < CHAR_COLS - 1; ++x) {
        set_char(x, 0, '-');
        set_colour(x, 0, fg, bg);
        set_char(x, CHAR_ROWS - 1, '-');
        set_colour(x, CHAR_ROWS - 1, fg, bg);
    }
    // Linhas verticais
    for (uint y = 1; y < CHAR_ROWS - 1; ++y) {
        set_char(0, y, '|');
        set_colour(0, y, fg, bg);
        set_char(CHAR_COLS - 1, y, '|');
        set_colour(CHAR_COLS - 1, y, fg, bg);
    }
}

// Função principal do Core 1 (renderização DVI)
void core1_main() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (true) {
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            uint32_t *tmdsbuf;
            queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
            for (int plane = 0; plane < 3; ++plane) {
                tmds_encode_font_2bpp(
                    (const uint8_t*)&charbuf[y / FONT_CHAR_HEIGHT * CHAR_COLS],
                    &colourbuf[y / FONT_CHAR_HEIGHT * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
                    tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                    FRAME_WIDTH,
                    (const uint8_t*)&font_8x8[y % FONT_CHAR_HEIGHT * FONT_N_CHARS] - FONT_FIRST_ASCII
                );
            }
            queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
}

// Função principal do Core 0 (lógica principal)
int __not_in_flash("main") main() {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    // Roda o sistema no clock de bits do TMDS
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    // --- CONFIGURAÇÃO DO BOTÃO BOOTSEL ---
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);


    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = picodvi_dvi_cfg;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    // Limpa a tela inteira com fundo preto uma única vez no início
    for (uint y = 0; y < CHAR_ROWS; ++y) {
        for (uint x = 0; x < CHAR_COLS; ++x) {
            set_char(x, y, ' ');
            set_colour(x, y, 0x00, 0x00); // Fundo preto
        }
    }

    // Desenha a borda
    draw_border();

    // Inicia o hardware do ADC
    adc_init();
    adc_gpio_init(27); // Habilita ADC no GPIO 27 (Canal 1 do ADC)
    adc_select_input(1); // Seleciona o canal 1 para leitura

    // Inicia o Core 1 para renderização
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);

    // Loop principal do Core 0 para ler e exibir o valor do ADC
    while (true) {
        // Lê o valor do ADC (resultado de 12 bits, 0 a 4095)
        uint16_t result = adc_read();

        // Define as partes da mensagem
        const char *prompt_msg = "A entrada analogica e: ";
        char value_msg[10]; // Buffer para o valor numérico
        sprintf(value_msg, "%d", result);

        // Calcula o comprimento total e a posição inicial para centralizar
        const int prompt_len = strlen(prompt_msg);
        const int value_len = strlen(value_msg);
        const int total_len = prompt_len + value_len;
 //       int start_y = CHAR_ROWS / 2;
        int start_y = 2;
        // Centraliza o texto DENTRO da borda
 //       int start_x = 1 + ((CHAR_COLS - 2 - total_len) / 2);
        int start_x = 1;        

        // Limpa a área de texto DENTRO da borda
        for (int i = 1; i < CHAR_COLS - 1; ++i) {
            set_char(i, start_y, ' ');
        }

        // Escreve a parte do texto estático em branco
        for (int i = 0; i < prompt_len; ++i) {
            set_char(start_x + i, start_y, prompt_msg[i]);
            set_colour(start_x + i, start_y, 0x3f, 0x00); // Texto branco, fundo preto
        }

        // Escreve a parte do valor numérico em verde
        for (int i = 0; i < value_len; ++i) {
            int current_x = start_x + prompt_len + i;
            set_char(current_x, start_y, value_msg[i]);
            set_colour(current_x, start_y, 0x0c, 0x00); // Texto verde, fundo preto
        }

        // Pausa para controlar a taxa de atualização da tela
        sleep_ms(100);
    }
}
