// Bibliotecas
#include "pico/stdlib.h"        // Biblioteca padrão do Pico
#include "hardware/gpio.h"      // Biblioteca de GPIO
#include "hardware/adc.h"       // Biblioteca de ADC
#include "hardware/i2c.h"       // Biblioteca de I2C
#include "hardware/pwm.h"       // Biblioteca de PWM
#include "hardware/pio.h"
#include "lib/ssd1306.h"        // Biblioteca de display OLED
#include "lib/font.h"           // Biblioteca de fontes
#include "lib/HTML.h"           // Biblioteca para geração de HTML
#include "hardware/clocks.h"
// Inclui arquivo PIO para matriz LED
#include "pio_matrix.pio.h"

#include "pico/cyw43_arch.h"    // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/tcp.h"           // Biblioteca de LWIP para manipulação de TCP/IP

#include "FreeRTOS.h"           // Biblioteca de FreeRTOS
#include "task.h"               // Biblioteca de tasks

#include <stdio.h>              // Biblioteca de entrada e saída padrão
#include <stdlib.h>             // Biblioteca padrão
#include <string.h>             // Biblioteca de strings
#include <ctype.h>              // Biblioteca de caracteres

//-----------------------------------------HTML----------------------------------------

//----------------------------------VÁRIAVEIS GLOBAIS----------------------------------

#define WIFI_SSID ""                    // Nome da rede Wi-Fi
#define WIFI_PASS ""                   // Senha da rede Wi-Fi

// Configurações da matriz WS2812
#define LED_PIN 7
#define NUM_PIXELS 25
#define MATRIZ_LARGURA 5
#define MATRIZ_ALTURA 5

// Configurações do eletroímã
#define ELECTROMAGNET_PIN 13
#define ELECTROMAGNET_LED_PIN 13  // LED no mesmo pino do eletroímã

// Configurações dos botões
#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6

struct http_state                               // Struct para manter o estado da conexão HTTP
{
    char response[32768]; // 32KB - tamanho para HTML completo
    size_t len;
    size_t sent;
    size_t offset; // bytes já enfileirados para envio
};

// Variáveis da matriz LED
static uint32_t matriz_leds[NUM_PIXELS];
PIO pio = pio0;
uint sm;

// Variáveis do eletroímã
static bool electromagnet_active = false;

// Variáveis de controle manual
static int current_position = 0;  // Posição atual (0-24)
static bool led_states[25] = {false};  // Estado de cada LED (false = apagado, true = aceso)
static bool button_a_pressed = false;
static bool button_b_pressed = false;
static uint32_t last_button_time = 0;

//---------------------------------------FUNÇÕES---------------------------------------

// Função para enviar o próximo pedaço de resposta se houver espaço na janela
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs);

// Função de callback para enviar dados HTTP
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

// Função de callback para receber dados HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Função de callback para aceitar conexões
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função para iniciar o servidor HTTP
static void start_http_server(void);

// Função para extrair parâmetros da URL
static void extract_url_parameters(char *request);

// Funções da matriz LED
static int coordenada_para_indice(int x, int y);
static void atualiza_matriz(void);
static void acende_led_matriz(int x, int y, uint32_t cor);
static void apaga_led_matriz(int x, int y);
static void inicializa_matriz_led(void);
static int converte_posicao_para_coordenadas(char *posicao, int *x, int *y);

// Funções do eletroímã
static void inicializa_eletroima(void);
static void ativar_eletroima(void);
static void desativar_eletroima(void);
static void toggle_eletroima(void);

// Funções de controle manual
static void inicializa_botoes(void);
static void processa_botoes(void);
static void atualiza_display_manual(void);
static int indice_para_coordenadas(int indice, int *x, int *y);

//----------------------------------------TASKS----------------------------------------

// Task de polling para manter a conexão Wi-Fi ativa
void vPollingTask(void *pvParameters)
{
    while (true)
    {
        cyw43_arch_poll(); // Polling do Wi-Fi para manter a conexão ativa
        vTaskDelay(1000);  // Aguarda 1 segundo antes de repetir
    }
}

// Task para controle manual dos botões
void vButtonControlTask(void *pvParameters)
{
    while (true)
    {
        processa_botoes();
        vTaskDelay(50);  // Aguarda 50ms antes de verificar novamente
    }
}

//----------------------------------------MAIN-----------------------------------------

int main()
{
    stdio_init_all();

    sleep_ms(4000);               // Aguarda 4 segundos para inicialização

    if (cyw43_arch_init())          // Inicializa o Wi-fi
    {
        printf("Falha ao inicializar o módulo Wi-Fi\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();   // Habilita o modo Station do Wi-Fi

    // Verifica se o Wi-Fi está conectado
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        return 1;
    }

    // Variáveis para armazenar o endereço IP
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    printf("Conectado ao Wi-Fi %s\n", WIFI_SSID);   // Exibe o nome da rede Wi-Fi
    printf("Endereço IP: %s\n", ip_str);            // Exibe o endereço IP

    inicializa_matriz_led();                        // Inicializa matriz LED
    inicializa_eletroima();                         // Inicializa eletroímã
    inicializa_botoes();                            // Inicializa botões
    atualiza_display_manual();                      // Inicializa display manual
    start_http_server();                            // Inicia o servidor HTTP

    // Tasks
    xTaskCreate(vPollingTask, "Polling Task", 512, NULL, 1, NULL);
    xTaskCreate(vButtonControlTask, "Button Control Task", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    panic_unsupported();
}

//---------------------------------DECLARAÇÃO DAS FUNÇÕES-----------------------------

#define CHUNK_SIZE 1024

// Função para enviar o próximo pedaço de resposta se houver espaço na janela
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs)
{
    if (hs->offset >= hs->len) return;
    size_t remaining = hs->len - hs->offset;
    size_t to_send = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    if (tcp_sndbuf(tpcb) < to_send) return; // Aguardar janela
    err_t err = tcp_write(tpcb, hs->response + hs->offset, to_send, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK)
    {
        hs->offset += to_send;
        tcp_output(tpcb);
    }
    else if (err != ERR_MEM)
    {
        // log erro irreversível
        printf("tcp_write fatal: %d\n", err);
    }
}

// Função de callback para enviar dados HTTP
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    else
    {
        send_next_chunk(tpcb, hs);
    }
    return ERR_OK;
}

// Função de callback para receber dados HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }
    tcp_recved(tpcb, p->tot_len);

    char *req = (char *)p->payload;
    
    extract_url_parameters(req);
    
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;
    hs->offset = 0;

    // Endpoint JSON para /web
    if (strstr(req, "GET /web"))
    {
        char json[256];
        int jsonlen = snprintf(json, sizeof(json),
            "{\""); //Conteúdo do JSON
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            jsonlen, json);
        tcp_arg(tpcb, hs);
        tcp_sent(tpcb, http_sent);
        send_next_chunk(tpcb, hs);
        pbuf_free(p);
        return ERR_OK;
    }
    
    // Endpoint JSON para /electromagnet-status
    if (strstr(req, "GET /electromagnet-status"))
    {
        char json[128];
        int jsonlen = snprintf(json, sizeof(json),
            "{\"active\":%s,\"status\":\"%s\"}",
            electromagnet_active ? "true" : "false",
            electromagnet_active ? "Ativado" : "Desativado");
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            jsonlen, json);
        tcp_arg(tpcb, hs);
        tcp_sent(tpcb, http_sent);
        send_next_chunk(tpcb, hs);
        pbuf_free(p);
        return ERR_OK;
    }

    if (strstr(req, "POST /toggle-electromagnet")) 
    {
        // Executa a função que liga/desliga o LED
        toggle_eletroima();
        
        // Prepara uma resposta simples de sucesso (200 OK)
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n");

        // Envia a resposta de volta para o navegador
        tcp_arg(tpcb, hs);
        tcp_sent(tpcb, http_sent);
        send_next_chunk(tpcb, hs);
        
        pbuf_free(p);
        return ERR_OK;
    }

    else
    {
        preencher_html();
        
        hs->len = strlen(html);
        if (hs->len >= sizeof(hs->response)) {
            hs->len = sizeof(hs->response) - 1;
        }
        memcpy(hs->response, html, hs->len);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    // inicia envio por fatias
    send_next_chunk(tpcb, hs);

    pbuf_free(p);
    return ERR_OK;
}

// Função de callback para aceitar novas conexões TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Função para iniciar o servidor HTTP
static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

// Função para extrair parâmetros da URL
static void extract_url_parameters(char *request)
{
    char *slot_param = strstr(request, "slot=");
    char *position_param = strstr(request, "position=");
    char *electromagnet_param = strstr(request, "electromagnet=");
    
    if (slot_param)
    {
        slot_param += 5; // Pula "slot="
        char slot_value[10];
        int i = 0;
        while (*slot_param && *slot_param != '&' && *slot_param != ' ' && *slot_param != '\r' && *slot_param != '\n' && i < 9)
        {
            slot_value[i++] = *slot_param++;
        }
        slot_value[i] = '\0';
        printf("Pallet clicado - Slot: %s\n", slot_value);
        
        // Acende LED da posição do slot
        int x, y;
        if (converte_posicao_para_coordenadas(slot_value, &x, &y)) {
            uint32_t cor = 0x00FF00; // Verde para slot
            acende_led_matriz(x, y, cor);
            printf("LED acendido na posição (%d,%d) - Slot: %s\n", x, y, slot_value);
        }
    }
    
    if (position_param)
    {
        position_param += 9; // Pula "position="
        char position_value[10];
        int i = 0;
        while (*position_param && *position_param != '&' && *position_param != ' ' && *position_param != '\r' && *position_param != '\n' && i < 9)
        {
            position_value[i++] = *position_param++;
        }
        position_value[i] = '\0';
        printf("Pallet clicado - Position: %s\n", position_value);
        
        // Apaga LED da posição
        int x, y;
        if (converte_posicao_para_coordenadas(position_value, &x, &y)) {
            apaga_led_matriz(x, y);
            printf("LED apagado na posição (%d,%d) - Position: %s\n", x, y, position_value);
        }
    }
    
    if (electromagnet_param)
    {
        electromagnet_param += 14; // Pula "electromagnet="
        char electromagnet_value[10];
        int i = 0;
        while (*electromagnet_param && *electromagnet_param != '&' && *electromagnet_param != ' ' && *electromagnet_param != '\r' && *electromagnet_param != '\n' && i < 9)
        {
            electromagnet_value[i++] = *electromagnet_param++;
        }
        electromagnet_value[i] = '\0';
        
        if (strcmp(electromagnet_value, "toggle") == 0) {
            toggle_eletroima();
            printf("Eletroímã alternado - Status: %s\n", electromagnet_active ? "Ativado" : "Desativado");
        }
    }
}

// Funções da matriz LED

// Converte coordenada (x,y) para índice do LED no array
static int coordenada_para_indice(int x, int y) {
    if (x < 0 || x >= MATRIZ_LARGURA || y < 0 || y >= MATRIZ_ALTURA) {
        return -1; 
    }

    if (y % 2 == 0) {
        // linhas pares: esquerda -> direita
        return y * MATRIZ_LARGURA + x;
    } else {
        // linhas ímpares: direita -> esquerda
        return y * MATRIZ_LARGURA + (MATRIZ_LARGURA - 1 - x);
    }
}

// Envia buffer completo para a matriz
static void atualiza_matriz(void) {
    for (int i = 0; i < NUM_PIXELS; i++) {
        // WS2812 usa formato GRB (shift p/ alinhar com protocolo)
        uint32_t cor = matriz_leds[i];
        pio_sm_put_blocking(pio, sm, cor << 8u);
    }
}

// Acende LED em (x,y) com uma cor RGB
static void acende_led_matriz(int x, int y, uint32_t cor) {
    int idx = coordenada_para_indice(x, y);
    if (idx >= 0) {
        matriz_leds[idx] = cor;
        atualiza_matriz();
    }
}

// Apaga LED em (x,y)
static void apaga_led_matriz(int x, int y) {
    int idx = coordenada_para_indice(x, y);
    if (idx >= 0) {
        matriz_leds[idx] = 0x000000; // preto
        atualiza_matriz();
    }
}

// Inicializa a matriz LED
static void inicializa_matriz_led(void) {
    // Inicializa PIO para WS2812
    uint offset = pio_add_program(pio, &pio_matrix_program);
    sm = pio_claim_unused_sm(pio, true);
    pio_matrix_program_init(pio, sm, offset, LED_PIN);

    // Limpa a matriz no início
    for (int i = 0; i < NUM_PIXELS; i++) {
        matriz_leds[i] = 0x000000;
    }
    atualiza_matriz();
    
    printf("Matriz LED 5x5 inicializada no pino %d\n", LED_PIN);
}

// Converte posição (ex: A1, B3) para coordenadas (x,y)
static int converte_posicao_para_coordenadas(char *posicao, int *x, int *y) {
    if (strlen(posicao) < 2) {
        *x = -1;
        *y = -1;
        return 0;
    }
    
    // Converte letra para coordenada Y (A=0, B=1, C=2, D=3)
    char letra = toupper(posicao[0]);
    *y = letra - 'A';
    
    // Converte número para coordenada X (1=0, 2=1, 3=2, 4=3, 5=4)
    *x = posicao[1] - '1';
    
    // Verifica se as coordenadas são válidas
    if (*x < 0 || *x >= MATRIZ_LARGURA || *y < 0 || *y >= MATRIZ_ALTURA) {
        *x = -1;
        *y = -1;
        return 0;
    }
    
    return 1; // Sucesso
}

// Funções do eletroímã

// Inicializa o eletroímã
static void inicializa_eletroima(void) {
    gpio_init(ELECTROMAGNET_PIN);
    gpio_set_dir(ELECTROMAGNET_PIN, GPIO_OUT);
    gpio_put(ELECTROMAGNET_PIN, 0); // Inicia desativado
    electromagnet_active = false;
    printf("Eletroímã inicializado no pino %d\n", ELECTROMAGNET_PIN);
}

// Ativa o eletroímã
static void ativar_eletroima(void) {
    gpio_put(ELECTROMAGNET_PIN, 1);
    electromagnet_active = true;
    printf("Eletroímã ativado\n");
}

// Desativa o eletroímã
static void desativar_eletroima(void) {
    gpio_put(ELECTROMAGNET_PIN, 0);
    electromagnet_active = false;
    printf("Eletroímã desativado\n");
}

// Alterna o estado do eletroímã
static void toggle_eletroima(void) {
    if (electromagnet_active) {
        desativar_eletroima();
    } else {
        ativar_eletroima();
    }
}

// Funções de controle manual

// Inicializa os botões
static void inicializa_botoes(void) {
    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);
    
    printf("Botões inicializados - A: GPIO %d, B: GPIO %d\n", BUTTON_A_PIN, BUTTON_B_PIN);
}

// Converte índice (0-24) para coordenadas (x,y)
static int indice_para_coordenadas(int indice, int *x, int *y) {
    if (indice < 0 || indice >= NUM_PIXELS) {
        *x = -1;
        *y = -1;
        return 0;
    }
    
    *y = indice / MATRIZ_LARGURA;
    *x = indice % MATRIZ_LARGURA;
    
    // Aplica o padrão zigzag da matriz
    if (*y % 2 == 1) {
        *x = MATRIZ_LARGURA - 1 - *x;
    }
    
    return 1;
}

// Atualiza o display com o estado atual
static void atualiza_display_manual(void) {
    // Limpa toda a matriz
    for (int i = 0; i < NUM_PIXELS; i++) {
        matriz_leds[i] = 0x000000;
    }
    
    // Desenha LEDs acesos
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (led_states[i]) {
            int x, y;
            if (indice_para_coordenadas(i, &x, &y)) {
                matriz_leds[i] = 0x00FF00; // Verde para LEDs acesos
            }
        }
    }
    
    // Desenha posição atual em azul
    int x, y;
    if (indice_para_coordenadas(current_position, &x, &y)) {
        matriz_leds[current_position] = 0x0000FF; // Azul para posição atual
    }
    
    atualiza_matriz();
}

// Processa os botões
static void processa_botoes(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Debounce - evita múltiplas leituras em pouco tempo
    if (current_time - last_button_time < 200) {
        return;
    }
    
    bool button_a_current = !gpio_get(BUTTON_A_PIN); // Invertido devido ao pull-up
    bool button_b_current = !gpio_get(BUTTON_B_PIN);
    
    // Botão A - navegação
    if (button_a_current && !button_a_pressed) {
        current_position = (current_position + 1) % NUM_PIXELS;
        printf("Posição atual: %d\n", current_position);
        atualiza_display_manual();
        last_button_time = current_time;
    }
    button_a_pressed = button_a_current;
    
    // Botão B - toggle LED
    if (button_b_current && !button_b_pressed) {
        led_states[current_position] = !led_states[current_position];
        printf("LED posição %d: %s\n", current_position, 
               led_states[current_position] ? "ACESO" : "APAGADO");
        atualiza_display_manual();
        last_button_time = current_time;
    }
    button_b_pressed = button_b_current;
}
