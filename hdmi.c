#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "pico/multicore.h"
#include "pico/bootrom.h" 
#include "pico/sem.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "hardware/adc.h" 
#include "dvi.h"
#include "dvi_serialiser.h"
#include "./include/common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

// Inclusão do arquivo de fonte
#include "./assets/font_teste.h"
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32

// AJUSTES PRINCIPAIS PARA DUPLICAÇÃO 3X (8x24)
#define FONT_CHAR_WIDTH 8        
#define FONT_CHAR_HEIGHT 24         
#define FONT_ORIGINAL_HEIGHT 8      
#define FONT_SCALE_FACTOR (FONT_CHAR_HEIGHT / FONT_ORIGINAL_HEIGHT) // Fator 3

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

// Watchdog configuration
#define WATCHDOG_TIMEOUT_MS 1000         
#define HEARTBEAT_THRESHOLD_MS 100        

// Flags de heartbeat dos núcleos (atualizadas periodicamente)
static volatile uint32_t hb_core0_ms = 0;
static volatile uint32_t hb_core1_ms = 0;

// Timer para alimentar o watchdog somente quando ambos os núcleos estão saudáveis
static repeating_timer_t wd_timer;
static bool feed_watchdog_cb(repeating_timer_t *t) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - hb_core0_ms) < HEARTBEAT_THRESHOLD_MS &&
        (now - hb_core1_ms) < HEARTBEAT_THRESHOLD_MS) {
        watchdog_update();
    }
    return true; // continua repetindo
}

// UART configuration
#define UART_ID uart0
#define UART_BAUD 115200
#define UART_RX_PIN 0
#define UART_TX_PIN 1

// Password configuration
#define PASSWORD_LEN 4
#define MAX_ATTEMPTS 3
#define LOCKOUT_MS 30000
static const char PASSWORD[PASSWORD_LEN + 1] = "3333";

struct dvi_inst dvi0;

// Definições do terminal de caracteres
// CHAR_COLS: 640 / 8 = 80 
// CHAR_ROWS: 480 / 24 = 20 
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

static inline void clear_line(uint y, uint8_t bg) {
    if (y >= CHAR_ROWS) return;
    for (uint x = 1; x < CHAR_COLS - 1; ++x) {
        set_char(x, y, ' ');
        set_colour(x, y, 0x00, bg);
    }
}

static inline void write_text(int start_x, int y, const char *text, uint8_t fg, uint8_t bg) {
    if (y < 0 || y >= (int)CHAR_ROWS || !text) return;
    for (int i = 0; text[i] != '\0'; ++i) {
        int x = start_x + i;
        if (x <= 0 || x >= (int)CHAR_COLS - 1) continue;
        set_char((uint)x, (uint)y, text[i]);
        set_colour((uint)x, (uint)y, fg, bg);
    }
}

static inline void write_centered(int y, const char *text, uint8_t fg, uint8_t bg) {
    int len = (int)strlen(text);
    int start_x = (CHAR_COLS / 2) - (len / 2);
    write_text(start_x, y, text, fg, bg);
}

// Função principal do Core 1 (renderização DVI)
void core1_main() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (true) {
        for (uint y = 0; y < FRAME_HEIGHT; ++y) {
            
            // Lógica de Duplicação Vertical (Fator 3)
            // Calcula a linha do pixel da fonte original (0 a 7), repetindo 3 vezes
            uint font_row = (y % FONT_CHAR_HEIGHT) / FONT_SCALE_FACTOR; 

            uint32_t *tmdsbuf;
            queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
            for (int plane = 0; plane < 3; ++plane) {
                tmds_encode_font_2bpp(
                    (const uint8_t*)&charbuf[y / FONT_CHAR_HEIGHT * CHAR_COLS],
                    &colourbuf[y / FONT_CHAR_HEIGHT * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
                    tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                    FRAME_WIDTH,
                    
                    (const uint8_t*)&font_8x8[font_row * FONT_N_CHARS] - FONT_FIRST_ASCII
                );
            }
            queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
        }
        // Heartbeat do Core 1 por frame completo
        hb_core1_ms = to_ms_since_boot(get_absolute_time());
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

    // Inicializa heartbeat de ambos os núcleos para evitar reset precoce
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    hb_core0_ms = now_ms;
    hb_core1_ms = now_ms;

    // Habilita Watchdog (pausa em debug: true)
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    // Timer periódico para tentar alimentar o WD somente se ambos estiverem operantes
    add_repeating_timer_ms(50, feed_watchdog_cb, NULL, &wd_timer);

    // Limpa a tela inteira com fundo preto uma única vez no início
    for (uint y = 0; y < CHAR_ROWS; ++y) {
        for (uint x = 0; x < CHAR_COLS; ++x) {
            set_char(x, y, ' ');
            set_colour(x, y, 0x00, 0x00); // Fundo preto
        }
    }

    // Inicializa UART para receber senha
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Inicia o Core 1 para renderização
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);

    // Lógica de validação de senha via UART
    const char *prompt_msg = "Digite a senha (4 digitos):";
    const char *title = "COFRE ELETRONICO";
    int prompt_y = (CHAR_ROWS / 2) - 1;
    int input_y = prompt_y + 1;
    int attempts = 0;
    char input[PASSWORD_LEN + 1] = {0};
    int input_index = 0;

    clear_line(prompt_y, 0x00);
    clear_line(input_y, 0x00);
    write_centered(prompt_y - 2, title, 0x3f, 0x00); // branco sobre preto
    write_centered(prompt_y, prompt_msg, 0x3f, 0x00); // branco sobre preto

    int input_base_x = (CHAR_COLS / 2) - (PASSWORD_LEN / 2);

    // Detecta reinicialização por watchdog e gerencia a contagem
    if (watchdog_caused_reboot())
    {
        // Recupera o valor salvo no registrador scratch[0], incrementa e salva novamente
        uint32_t reset_count = watchdog_hw->scratch[0];
        reset_count++;
        watchdog_hw->scratch[0] = reset_count;

        printf("\n\n>>> Reiniciado pelo Watchdog! Contagem de resets: %d\n", reset_count);
    }
    else
    {
        // Reset normal (energia ligada agora): zera o contador
        printf(">>> Reset normal (Power On). Iniciando contador em 0.\n");
        watchdog_hw->scratch[0] = 0;
    }

    while (true) {
        // Heartbeat do Core 0 por laço principal
        hb_core0_ms = to_ms_since_boot(get_absolute_time());
        if (uart_is_readable(UART_ID)) {
            char ch = (char)uart_getc(UART_ID);
            if (ch >= '0' && ch <= '9') {
                if (input_index < PASSWORD_LEN) {
                    input[input_index] = ch;
                    // Mostrar '*' para cada dígito
                    set_char((uint)(input_base_x + input_index), (uint)input_y, '*');
                    set_colour((uint)(input_base_x + input_index), (uint)input_y, 0x3C, 0x00); // amarelo sobre preto
                    input_index++;
                }
                if (input_index == PASSWORD_LEN) {
                    input[PASSWORD_LEN] = '\0';
                    // Verifica senha
                    if (strncmp(input, PASSWORD, PASSWORD_LEN) == 0) {
                         // Limpa a tela inteira com fundo preto uma única vez no início
                        for (uint y = 0; y < CHAR_ROWS; ++y) {
                            for (uint x = 0; x < CHAR_COLS; ++x) {
                                set_char(x, y, ' ');
                                set_colour(x, y, 0x0C, 0x0C); // Fundo verde
                            }
                        }
                        write_centered(prompt_y - 2, title, 0x3f, 0x0c); // branco sobre verde
                        write_centered(prompt_y, "Bem vindo", 0x3f, 0x0c); // branco sobre verde
                        // Sucesso: permanece mostrando mensagem
                    } else {
                         // Limpa a tela inteira com fundo preto uma única vez no início
                        for (uint y = 0; y < CHAR_ROWS; ++y) {
                            for (uint x = 0; x < CHAR_COLS; ++x) {
                                set_char(x, y, ' ');
                                set_colour(x, y, 0x30, 0x30); // Fundo vermelho
                            }
                        }
                        attempts++;
                        write_centered(prompt_y - 2, title, 0x3f, 0x30); // branco sobre vermelho escuro
                        write_centered(prompt_y, "Senha incorreta. Tente novamente.", 0x3f, 0x30); // branco sobre vermelho escuro
                        sleep_ms(3000);

                        if (attempts >= MAX_ATTEMPTS) {
                            write_centered(input_y+2, "Bloqueado por 30 segundos...", 0x3f, 0x30);
                            sleep_ms(LOCKOUT_MS);
                            attempts = 0;
                            clear_line(prompt_y, 0x00);
                            clear_line(input_y+2, 0x00);
                        }

                        // Reinicia entrada
                        memset(input, 0, sizeof(input));
                        input_index = 0;
                         // Limpa a tela inteira com fundo preto
                        for (uint y = 0; y < CHAR_ROWS; ++y) {
                            for (uint x = 0; x < CHAR_COLS; ++x) {
                                set_char(x, y, ' ');
                                set_colour(x, y, 0x00, 0x00); // Fundo preto
                            }
                        }
                        write_centered(prompt_y - 2, title, 0x3f, 0x00); // branco sobre preto
                        write_centered(prompt_y, prompt_msg, 0x3f, 0x00);
                    }
                }
            }
        }
        sleep_ms(10);
    }
}


