/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_log.h>
#include <tcpip_adapter.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <nvs_flash.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>


/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

#define PORT 3333
#define USE_AP_MODE 1

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

static const int IPV4_GOTIP_BIT = BIT0;
static const int IPV6_GOTIP_BIT = BIT1;
static const int AP_STARTED_BIT = BIT2;

static const char *TAG_SERVER = "server";
static const char *TAG_MAIN = "main";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG_MAIN, "SYSTEM_EVENT_STA_START");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        /* enable ipv6 */
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ESP_LOGI(TAG_MAIN, "SYSTEM_EVENT_STA_GOT_IP");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
        xEventGroupClearBits(wifi_event_group, IPV6_GOTIP_BIT);
        break;
    case SYSTEM_EVENT_AP_STA_GOT_IP6:
        xEventGroupSetBits(wifi_event_group, IPV6_GOTIP_BIT);
        ESP_LOGI(TAG_MAIN, "SYSTEM_EVENT_STA_GOT_IP6");

        char *ip6 = ip6addr_ntoa(&event->event_info.got_ip6.ip6_info.ip);
        ESP_LOGI(TAG_MAIN, "IPv6: %s", ip6);
    case SYSTEM_EVENT_AP_START:
        xEventGroupSetBits(wifi_event_group, AP_STARTED_BIT);
        break;
    case SYSTEM_EVENT_AP_STOP:
        xEventGroupClearBits(wifi_event_group, AP_STARTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

#if USE_AP_MODE
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "CRASH-AP",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
#else
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG_MAIN, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
#endif
}

static void wait_for_ip(void)
{
#if USE_AP_MODE
    uint32_t bits = AP_STARTED_BIT ;
#else
    uint32_t bits = IPV4_GOTIP_BIT | IPV6_GOTIP_BIT ;
#endif

    ESP_LOGI(TAG_MAIN, "Waiting for AP connection...");
    xEventGroupWaitBits(wifi_event_group, bits, false, true, portMAX_DELAY);
    ESP_LOGI(TAG_MAIN, "Connected to AP");
}

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    int err;
    int v = 1;

    struct sockaddr_in6 destAddr;
    bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
    destAddr.sin6_family = AF_INET6;
    destAddr.sin6_port = htons(PORT);
    destAddr.sin6_addr = in6addr_any;

    int listen_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG_SERVER, "Unable to create socket: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG_SERVER, "Socket created");

    err = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
    if (err != 0) {
        ESP_LOGE(TAG_SERVER, "can't enable SO_REUSEADDR: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG_SERVER, "SO_REUSEADDR enabled");

    err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
    if (err != 0) {
        ESP_LOGE(TAG_SERVER, "Socket unable to bind: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG_SERVER, "Socket bound");

    err = listen(listen_sock, 64);
    if (err != 0) {
        ESP_LOGE(TAG_SERVER, "Error occured during listen: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG_SERVER, "Socket listening");

    while (1) {

        struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
        uint addrLen = sizeof(sourceAddr);
        int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
        if (sock < 0) {
            ESP_LOGE(TAG_SERVER, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_SERVER, "Socket accepted");

        int wait_for_close = 0;
        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occured during receiving
            if (len < 0) {
                ESP_LOGE(TAG_SERVER, "recv failed: errno %d", errno);
                break;
            }
            // Connection closed
            else if (len == 0) {
                ESP_LOGI(TAG_SERVER, "Connection closed");
                break;
            }
            else if (wait_for_close) {
                ESP_LOGI(TAG_SERVER, "ignore data after shutdown-WR");
            }
            // Data received
            else {
                ESP_LOGI(TAG_SERVER, "Received %d bytes", len);

                int err = send(sock, rx_buffer, len, 0);
                if (err < 0) {
                    ESP_LOGE(TAG_SERVER, "Error occured during sending: errno %d", errno);
                    break;
                }

                err = shutdown(sock, SHUT_WR);
                if (err < 0) {
                    ESP_LOGE(TAG_SERVER, "Error occured during shutdown-WR: errno %d", errno);
                    break;
                }
                wait_for_close = 1;
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG_SERVER, "Shutting down socket and restarting...");
            shutdown(sock, SHUT_RDWR);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    wait_for_ip();

    xTaskCreate(tcp_server_task, "tcp_server", 8096, NULL, ESP_TASK_MAIN_PRIO, NULL);
}
