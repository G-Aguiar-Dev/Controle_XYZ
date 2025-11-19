/**
 * @file lcd_1602_i2c.c
 * @brief Implementação da biblioteca refatorada para LCD 16x2 I2C.
 */

#include "lcd_1602_i2c.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Variáveis estáticas para armazenar a configuração do I2C
static i2c_inst_t *i2c_port;
static uint8_t i2c_addr;

/* Função auxiliar para escrita de um byte no barramento I2C */
static void i2c_write_byte(uint8_t val) {
    i2c_write_blocking(i2c_port, i2c_addr, &val, 1, false);
}

/* Função auxiliar para pulsar o pino Enable */
static void lcd_toggle_enable(uint8_t val) {
    // Não podemos fazer isso muito rápido
#define DELAY_US 600
    sleep_us(DELAY_US);
    i2c_write_byte(val | LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
    i2c_write_byte(val & ~LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
}

// Envia um byte como duas transferências de 4 bits (nibbles)
void lcd_send_byte(uint8_t val, int mode) {
    uint8_t high = mode | (val & 0xF0) | LCD_BACKLIGHT;
    uint8_t low = mode | ((val << 4) & 0xF0) | LCD_BACKLIGHT;

    i2c_write_byte(high);
    lcd_toggle_enable(high);
    i2c_write_byte(low);
    lcd_toggle_enable(low);
}

void lcd_clear(void) {
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
}

void lcd_set_cursor(int line, int position) {
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_send_byte(val, LCD_COMMAND);
}

void lcd_string(const char *s) {
    while (*s) {
        lcd_char(*s++);
    }
}

void lcd_char(char val) {
    lcd_send_byte(val, LCD_CHARACTER);
}

void lcd_init(i2c_inst_t *i2c, uint8_t addr) {
    // Armazena a configuração do I2C
    i2c_port = i2c;
    i2c_addr = addr;

    // Sequência de inicialização do LCD
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x02, LCD_COMMAND);

    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
    lcd_clear();
}
