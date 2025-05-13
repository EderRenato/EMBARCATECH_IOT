#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/bh1750.h"
#include "lib/dht22.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#define WIFI_SSID "S@NTOS$28"
#define WIFI_PASSWORD "p&1xe$82"

// Pin configurations
#define I2C_OLED_PORT i2c1
#define I2C_OLED_SDA 15
#define I2C_OLED_SCL 14

#define I2C_BH1750_PORT i2c0
#define I2C_BH1750_SDA 8
#define I2C_BH1750_SCL 9

#define DHT22_PIN 16
#define LED_PIN CYW43_WL_GPIO_LED_PIN

// Device instances
ssd1306_t oled;
bh1750_t light_sensor;
float temperature = 0;
float humidity = 0;
float luminosity = 0;

// Function prototypes
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void init_all_devices();
void update_display(float lux, float temp, float hum);

void init_all_devices() {
    // Initialize I2C for OLED
    i2c_init(I2C_OLED_PORT, 400000);
    gpio_set_function(I2C_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_OLED_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_OLED_SDA);
    gpio_pull_up(I2C_OLED_SCL);

    // Initialize I2C for BH1750
    i2c_init(I2C_BH1750_PORT, 400000);
    gpio_set_function(I2C_BH1750_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_BH1750_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_BH1750_SDA);
    gpio_pull_up(I2C_BH1750_SCL);

    // Initialize OLED
    ssd1306_init(&oled, 128, 64, false, 0x3C, I2C_OLED_PORT);
    ssd1306_config(&oled);
    ssd1306_fill(&oled, false);
    ssd1306_send_data(&oled);

    // Initialize BH1750
    bh1750_init(&light_sensor, I2C_BH1750_PORT, BH1750_ADDR_LOW, BH1750_CONT_HIGH_RES_MODE);
}

void update_display(float lux, float temp, float hum) {
    char buffer[32];
    ssd1306_fill(&oled, false);
    
    snprintf(buffer, sizeof(buffer), "Luz: %.2f lux", lux);
    ssd1306_draw_string(&oled, buffer, 1, 10);
    
    snprintf(buffer, sizeof(buffer), "Temp: %.1fC", temp);
    ssd1306_draw_string(&oled, buffer, 1, 30);
    
    snprintf(buffer, sizeof(buffer), "Umidade: %.1f%%", hum);
    ssd1306_draw_string(&oled, buffer, 1, 50);
    
    ssd1306_send_data(&oled);
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    char *data = (char *)p->payload;

    // Log da requisição recebida (muito útil!)
    printf("Requisicao recebida:\n%s\n", data);

    char resposta[2048];

    if (strstr(data, "GET /dados") != NULL) {
        snprintf(resposta, sizeof(resposta),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"temperatura\":%.2f,\"umidade\":%.2f,\"luminosidade\":%.2f}\r\n",
            temperature, humidity, luminosity);
    } else {
        snprintf(resposta, sizeof(resposta),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\n"
            "<html lang=\"pt-BR\">\n"
            "<head>\n"
            "  <meta charset=\"utf-8\">\n"
            "  <title>Monitor Meteorológico</title>\n"
            "  <style>\n"
            "    body { font-family: Arial, sans-serif; background-color: #f4f4f4; color: #333; text-align: center; padding: 20px; }\n"
            "    h1 { color: #005b96; }\n"
            "    .card { background-color: #fff; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); display: inline-block; padding: 20px; margin-top: 20px; }\n"
            "    .metric { font-size: 18px; margin: 10px 0; }\n"
            "    button { padding: 10px 20px; font-size: 16px; border: none; border-radius: 5px; background-color: #005b96; color: white; cursor: pointer; }\n"
            "    button:hover { background-color: #003f6f; }\n"
            "  </style>\n"
            "</head>\n"
            "<body>\n"
            "  <h1>Monitor Meteorológico</h1>\n"
            "  <div class=\"card\">\n"
            "    <div class=\"metric\">🌡️ Temperatura: %.2f °C</div>\n"
            "    <div class=\"metric\">💧 Umidade: %.2f %%</div>\n"
            "    <div class=\"metric\">💡 Luminosidade: %.2f lux</div>\n"
            "    <br><button onclick=\"location.reload()\">Atualizar</button>\n"
            "  </div>\n"
            "</body>\n"
            "</html>\n",
            temperature, humidity, luminosity);

    }

    // Envia a resposta corretamente
    tcp_write(tpcb, resposta, strlen(resposta), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}



int main() {
    stdio_init_all();
    printf("Starting monitoring system...\n");

    init_all_devices();

    // Initialize WiFi
    if (cyw43_arch_init()) {
        printf("Failed to initialize WiFi\n");
        return 1;
    }
    cyw43_arch_gpio_put(LED_PIN, 1); // Turn on LED

    // Connect to WiFi
    printf("Connecting to WiFi...\n");
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Failed to connect\n");
        return 1;
    }
    printf("Connected to WiFi\n");

    // Print IP address
    printf("IP Address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    // Setup TCP server
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Failed to create PCB\n");
        return 1;
    }

    if (tcp_bind(pcb, IP_ANY_TYPE, 80) != ERR_OK) {
        printf("Failed to bind\n");
        return 1;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("Failed to listen\n");
        return 1;
    }

    tcp_accept(pcb, tcp_server_accept);
    printf("HTTP server started\n");

    absolute_time_t last_bh1750_read = get_absolute_time();
    absolute_time_t last_dht22_read = get_absolute_time();

    while (true) {
        // Read BH1750 every 2 seconds
        if (absolute_time_diff_us(last_bh1750_read, get_absolute_time()) > 2000000) {
            bh1750_start_measurement(&light_sensor);
            sleep_ms(180);
            if (bh1750_read_result(&light_sensor, &luminosity)) {
                printf("Luminosity: %.2f lux\n", luminosity);
            } else {
                printf("BH1750 read failed\n");
                luminosity = -1;
            }
            last_bh1750_read = get_absolute_time();
        }

        // Read DHT22 every 3 seconds
        if (absolute_time_diff_us(last_dht22_read, get_absolute_time()) > 3000000) {
            if (read_dht22(DHT22_PIN, &temperature, &humidity)) {
                printf("Temperature: %.1fC, Humidity: %.1f%%\n", temperature, humidity);
            } else {
                printf("DHT22 read failed\n");
                temperature = -99;
                humidity = -1;
            }
            last_dht22_read = get_absolute_time();
        }

        update_display(luminosity, temperature, humidity);
        cyw43_arch_poll();
        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}