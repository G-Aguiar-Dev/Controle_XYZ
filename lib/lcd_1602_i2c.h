/**
 * @file lcd_1602_i2c.h
 * @brief Biblioteca refatorada para LCD 16x2 I2C (baseada no exemplo do Raspberry Pi).
 *
 * Permite a inicialização em qualquer porta I2C e endereço.
 */

#ifndef LCD_1602_I2C_H
#define LCD_1602_I2C_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

// --- Comandos do LCD ---
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// --- Flags para modo de entrada ---
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYLEFT 0x02

// --- Flags para controle do display ---
#define LCD_BLINKON 0x01
#define LCD_CURSORON 0x02
#define LCD_DISPLAYON 0x04

// --- Flags para shift do display/cursor ---
#define LCD_MOVERIGHT 0x04
#define LCD_DISPLAYMOVE 0x08

// --- Flags para function set ---
#define LCD_5x10DOTS 0x04
#define LCD_2LINE 0x08
#define LCD_8BITMODE 0x10

// --- Flag para backlight ---
#define LCD_BACKLIGHT 0x08

#define LCD_ENABLE_BIT 0x04

// --- Modos para envio de byte ---
#define LCD_CHARACTER 1
#define LCD_COMMAND 0

// --- Dimensões do Display ---
#define MAX_LINES 2
#define MAX_CHARS 16

/**
 * @brief Inicializa o display LCD.
 *
 * @param i2c A instância I2C (ex: i2c0, i2c1).
 * @param i2c_addr O endereço I2C do display (ex: 0x27).
 */
void lcd_init(i2c_inst_t *i2c, uint8_t i2c_addr);

/**
 * @brief Limpa o display.
 */
void lcd_clear(void);

/**
 * @brief Posiciona o cursor.
 *
 * @param line Linha (0 ou 1).
 * @param position Coluna (0 a 15).
 */
void lcd_set_cursor(int line, int position);

/**
 * @brief Escreve uma string no display.
 *
 * @param s Ponteiro para a string.
 */
void lcd_string(const char *s);

/**
 * @brief Escreve um único caractere no display.
 *
 * @param val O caractere.
 */
void lcd_char(char val);

/**
 * @brief Envia um byte (comando ou dado) para o LCD.
 *
 * @param val O byte a ser enviado.
 * @param mode LCD_COMMAND (0) ou LCD_CHARACTER (1).
 */
void lcd_send_byte(uint8_t val, int mode);

#endif // LCD_1602_I2C_H
