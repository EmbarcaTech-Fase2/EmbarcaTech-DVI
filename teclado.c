#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define ROWS 4
#define COLS 4

#define UART_ID uart0
#define UART_RX_PIN 0
#define UART_TX_PIN 1

const uint8_t row_pins[ROWS] = {16, 9, 8, 4};
const uint8_t col_pins[COLS] = {17, 18, 19, 20};

const char key_map[ROWS][COLS] = {
    {'0', '*', 'D', '#'},
    {'7', '8', '9', 'C'},
    {'4', '5', '6', 'B'},
    {'1', '2', '3', 'A'}
};

void pad_init() {
    for (int i = 0; i < ROWS; i++) {
        gpio_init(row_pins[i]);
        gpio_set_dir(row_pins[i], GPIO_OUT);
        gpio_put(row_pins[i], 1);
    }
    for (int i = 0; i < COLS; i++) {
        gpio_init(col_pins[i]);
        gpio_set_dir(col_pins[i], GPIO_IN);
        gpio_pull_up(col_pins[i]);
    }
}

char scan_pad() {
    for (int row = 0; row < ROWS; row++) {
        gpio_put(row_pins[row], 0);
        for (int col = 0; col < COLS; col++) {
            if (gpio_get(col_pins[col]) == 0) {
                sleep_ms(50); // debounce
                while (gpio_get(col_pins[col]) == 0);
                gpio_put(row_pins[row], 1);
                return key_map[col][row]; 
            }
        }
        gpio_put(row_pins[row], 1);
    }
    return '\0';
}

void uart_handler_init(uart_inst_t *uart_id, uint tx_pin, uint rx_pin) {
    uart_init(uart_id, 115200);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
}

void uart_handler_send(uart_inst_t *uart_id, const char *msg) {
    uart_puts(uart_id, msg);
}

int main() {
    stdio_init_all();
    pad_init();
    uart_handler_init(UART_ID, UART_TX_PIN, UART_RX_PIN);

    gpio_init(13);
    gpio_set_dir(13, GPIO_OUT);
    gpio_put(13, 0);

    while (true) {
        char key = scan_pad();
        if (key != '\0') {
            gpio_put(13, 1);
            char str[3] = {key, '\n', '\0'}; 
            uart_handler_send(UART_ID, str);
        }
        sleep_ms(200);
        gpio_put(13, 0);
    }
}