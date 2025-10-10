// Bibliotecas
#include "pico/stdlib.h"        // Biblioteca padrão do Pico
#include "hardware/gpio.h"      // Biblioteca de GPIO
#include "hardware/adc.h"       // Biblioteca de ADC
#include "hardware/i2c.h"       // Biblioteca de I2C
#include "hardware/pwm.h"       // Biblioteca de PWM
#include "hardware/pio.h"       // Biblioteca de PIO
#include "lib/ssd1306.h"        // Biblioteca de display OLED
#include "lib/font.h"           // Biblioteca de fontes
#include "lib/HTML.h"           // Biblioteca para geração de HTML
#include "hardware/clocks.h"

// Inclui arquivo PIO para a matriz LED
#include "pio_matrix.pio.h"

#include "pico/cyw43_arch.h"    // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/tcp.h"           // Biblioteca de LWIP para manipulação de TCP/IP

#include "FreeRTOS.h"           // Biblioteca de FreeRTOS
#include "task.h"               // Biblioteca de tasks

#include <stdio.h>              // Biblioteca de entrada e saída padrão
#include <stdlib.h>             // Biblioteca padrão
#include <string.h>             // Biblioteca de strings
#include <ctype.h>              // Biblioteca de caracteres
#include <stdarg.h>            

// Histórico de logs em memória
#define LOG_CAP 120
#define LOG_LINE_MAX 128
static char g_log[LOG_CAP][LOG_LINE_MAX];
static int g_log_head = 0;  // aponta para a próxima posição de escrita
static int g_log_count = 0; // quantos registros válidos

static void log_push(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log[g_log_head], LOG_LINE_MAX, fmt, ap);
    va_end(ap);

    g_log_head = (g_log_head + 1) % LOG_CAP;
    if (g_log_count < LOG_CAP) g_log_count++;
}

// retorna a linha i (0 = mais antiga, g_log_count-1 = mais recente)
static const char *log_get(int idx)
{
    if (idx < 0 || idx >= g_log_count) return "";
    int start = (g_log_head - g_log_count + LOG_CAP) % LOG_CAP;
    int i = (start + idx) % LOG_CAP;
    return g_log[i];
}

// simples utilitários para parsing de URL/query
static int url_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode_inplace(char *s) {
    char *w = s;
    while (*s) {
        if (*s == '+') { *w++ = ' '; s++; }
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            int hi = url_hex(s[1]);
            int lo = url_hex(s[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                s += 3;
            } else {
                *w++ = *s++; // invalid, copy raw
            }
        } else {
            *w++ = *s++;
        }
    }
    *w = '\0';
}

// procura key na query string da primeira linha (após '?') e copia o valor decodificado
static bool query_param(const char *req, const char *key, char *out, size_t outsz) {
    const char *q = strchr(req, '?');
    if (!q) return false;
    q++; // after ?
    size_t klen = strlen(key);
    while (*q && *q != '\r' && *q != '\n' && *q != ' ') {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            const char *v = q + klen + 1;
            size_t i = 0;
            while (*v && *v != '&' && *v != ' ' && *v != '\r' && *v != '\n') {
                if (i + 1 < outsz) out[i++] = *v;
                v++;
            }
            out[i] = '\0';
            url_decode_inplace(out);
            return true;
        }
        // skip to next param
        while (*q && *q != '&' && *q != ' ' && *q != '\r' && *q != '\n') q++;
        if (*q == '&') q++;
    }
    return false;
}

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

struct http_state // Struct para manter o estado da conexão HTTP
{
    const char *response_ptr;   // ponteiro para o buffer com a resposta
    char smallbuf[1024];        // usado para respostas pequenas/JSON
    size_t len;                 // tamanho total da resposta
    size_t sent;
    size_t offset; // bytes já enfileirados para envio
    bool using_smallbuf;
};

// Variáveis da matriz LED
static uint32_t matriz_leds[NUM_PIXELS];
PIO pio = pio0;
uint sm;

// Variáveis do eletroímã
static bool electromagnet_active = false;

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
    start_http_server();                            // Inicia o servidor HTTP

    // Tasks
    xTaskCreate(vPollingTask, "Polling Task", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    panic_unsupported();
}

//---------------------------------DECLARAÇÃO DAS FUNÇÕES-----------------------------

#define CHUNK_SIZE 1024

// Função para enviar o próximo pedaço de resposta se houver espaço na janela
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs)
{
    if (hs->offset >= hs->len)
        return;
    size_t remaining = hs->len - hs->offset;
    size_t to_send = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    if (tcp_sndbuf(tpcb) < to_send)
        return; // Aguardar janela
    err_t err = tcp_write(tpcb, hs->response_ptr + hs->offset, to_send, TCP_WRITE_FLAG_COPY);
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

    #define REQ_BUF_SZ 2048
    char reqbuf[REQ_BUF_SZ];
    size_t tocopy = p->tot_len < (REQ_BUF_SZ - 1) ? p->tot_len : (REQ_BUF_SZ - 1);
    size_t copied = 0;
    struct pbuf *q;
    for (q = p; q != NULL && copied < tocopy; q = q->next)
    {
        size_t chunk = q->len;
        if (chunk > tocopy - copied)
            chunk = tocopy - copied;
        memcpy(reqbuf + copied, q->payload, chunk);
        copied += chunk;
    }
    reqbuf[copied] = '\0';
    char *req = reqbuf;

    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;
    hs->offset = 0;
    hs->using_smallbuf = false;
    hs->response_ptr = NULL;

    // PROCESSAMENTO DAS ROTAS HTTP
    if (strstr(req, "GET /api/log?"))
    {
        char msg[256];
        if (query_param(req, "msg", msg, sizeof(msg)))
        {
            log_push("%s", msg);
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "GET /api/history"))
    {
        size_t off = 0;
        off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off,
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n[");
        for (int i = 0; i < g_log_count; i++)
        {
            const char *ln = log_get(i);
            char esc[LOG_LINE_MAX * 2];
            size_t j = 0;
            for (size_t k = 0; k < strlen(ln) && j + 2 < sizeof(esc); k++)
            {
                if (ln[k] == '"' || ln[k] == '\\')
                {
                    esc[j++] = '\\';
                    esc[j++] = ln[k];
                }
                else if ((unsigned char)ln[k] < 0x20)
                {
                    esc[j++] = ' ';
                }
                else
                {
                    esc[j++] = ln[k];
                }
            }
            esc[j] = '\0';
            off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off,
                            "%s\"%s\"", (i ? "," : ""), esc);
            if (off >= sizeof(hs->smallbuf) - 4)
                break;
        }
        off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off, "]");
        hs->len = off;
        hs->response_ptr = hs->smallbuf;
        hs->using_smallbuf = true;
    }
    else if (strstr(req, "POST /store?"))
    {
        // Processar armazenamento de pallet
        char slot[10];
        if (query_param(req, "slot", slot, sizeof(slot)))
        {
            printf("Armazenamento solicitado - Slot: %s\n", slot);
            log_push("Armazenamento na posicao %s solicitado", slot);
            
            // Acende LED da posição do slot (verde para armazenamento)
            int x, y;
            if (converte_posicao_para_coordenadas(slot, &x, &y)) {
                uint32_t cor = 0x00FF00; // Verde para slot ocupado
                acende_led_matriz(x, y, cor);
                printf("LED acendido na posição (%d,%d) - Slot: %s\n", x, y, slot);
                log_push("LED acendido na posicao (%d,%d) - Slot: %s", x, y, slot);
            }
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "POST /retrieve?"))
    {
        // Processar retirada de pallet
        char slot[10];
        if (query_param(req, "slot", slot, sizeof(slot)))
        {
            printf("Retirada solicitada - Slot: %s\n", slot);
            log_push("Retirada da posicao %s solicitada", slot);
            
            // Apaga LED da posição
            int x, y;
            if (converte_posicao_para_coordenadas(slot, &x, &y)) {
                apaga_led_matriz(x, y);
                printf("LED apagado na posição (%d,%d) - Slot: %s\n", x, y, slot);
                log_push("LED apagado na posicao (%d,%d) - Slot: %s", x, y, slot);
            }
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "POST /toggle-electromagnet"))
    {
        // Processar ativação/desativação do eletroímã
        toggle_eletroima();
        printf("Eletroímã alternado - Status: %s\n", electromagnet_active ? "Ativado" : "Desativado");
        log_push("Eletroima %s", electromagnet_active ? "ativado" : "desativado");
        
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else
    { // Rota padrão (página principal)
        preencher_html();
        hs->response_ptr = html; // apontar para buffer global
        hs->len = strlen(html);
        hs->using_smallbuf = false;
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
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
