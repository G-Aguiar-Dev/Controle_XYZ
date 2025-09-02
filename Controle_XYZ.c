// Bibliotecas
#include "pico/stdlib.h"        // Biblioteca padrão do Pico
#include "hardware/gpio.h"      // Biblioteca de GPIO
#include "hardware/adc.h"       // Biblioteca de ADC
#include "hardware/i2c.h"       // Biblioteca de I2C
#include "hardware/pwm.h"       // Biblioteca de PWM

#include "lib/ssd1306.h"        // Biblioteca de display OLED
#include "lib/font.h"           // Biblioteca de fontes
#include "lib/HTML.h"           // Biblioteca para geração de HTML

#include "pico/cyw43_arch.h"    // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/tcp.h"           // Biblioteca de LWIP para manipulação de TCP/IP

#include "FreeRTOS.h"           // Biblioteca de FreeRTOS
#include "task.h"               // Biblioteca de tasks

#include <stdio.h>              // Biblioteca de entrada e saída padrão

//-----------------------------------------HTML----------------------------------------

//----------------------------------VÁRIAVEIS GLOBAIS----------------------------------

#define WIFI_SSID "XXXXXXXX"                    // Nome da rede Wi-Fi
#define WIFI_PASS "XXXXXXXXX"                   // Senha da rede Wi-Fi

struct http_state                               // Struct para manter o estado da conexão HTTP
{
    char response[4096];
    size_t len;
    size_t sent;
    size_t offset; // bytes já enfileirados para envio
};

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

    start_http_server();                            // Inicia o servidor HTTP

    // Tasks
    xTaskCreate(vPollingTask, "Polling Task", 256, NULL, 1, NULL);
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