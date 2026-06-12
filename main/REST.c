#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_http_server.h"

#include "esp_sntp.h"
#include "esp_netif_sntp.h"

#define WIFI_SSID      "11-4"
#define WIFI_PASSWORD  "********"

static const char *TAG = "REST_TIME_SERVER";

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

/*--------------------------------------------------
                    WIFI EVENT
--------------------------------------------------*/

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG,
                 "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT);
    }
}

/*--------------------------------------------------
                    WIFI INIT
--------------------------------------------------*/

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode =
                WIFI_AUTH_WPA2_PSK,
        },
    };

    strcpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID);

    strcpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD);

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config));

    ESP_ERROR_CHECK(
        esp_wifi_start());

    ESP_LOGI(TAG, "Connecting WiFi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);
}

/*--------------------------------------------------
                    SNTP
--------------------------------------------------*/

static void obtain_time(void)
{
    ESP_LOGI(TAG, "Starting SNTP...");

    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(
            "pool.ntp.org");

    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(
            pdMS_TO_TICKS(15000))
        == ESP_OK)
    {
        ESP_LOGI(TAG, "Time synchronized");
		
		// 🎯 關鍵加入：設定台北時區 (GMT+8)
        setenv("TZ", "CST-8", 1);
        tzset();
        ESP_LOGI(TAG, "Timezone set to Taipei (CST-8)");
		
    }
    else
    {
        ESP_LOGW(TAG, "SNTP timeout");
    }
}

/*--------------------------------------------------
                REST POST /time
--------------------------------------------------*/

static esp_err_t time_post_handler(
    httpd_req_t *req)
{
    time_t now;
    struct tm timeinfo;

    time(&now);

    localtime_r(
        &now,
        &timeinfo);

    char time_string[64];

    strftime(
        time_string,
        sizeof(time_string),
        "%Y-%m-%d %H:%M:%S",
        &timeinfo);

    char response[128];

    snprintf(
        response,
        sizeof(response),
        "{\"time\":\"%s\"}",
        time_string);

    httpd_resp_set_type(
        req,
        "application/json");

    httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(
        TAG,
        "POST /time");

    return ESP_OK;
}

/*--------------------------------------------------
                HTTP SERVER
--------------------------------------------------*/

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    if (httpd_start(
            &server,
            &config)
        == ESP_OK)
    {
        httpd_uri_t time_uri = {
            .uri = "/time",
            .method = HTTP_POST,
            .handler = time_post_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(
            server,
            &time_uri);

        ESP_LOGI(
            TAG,
            "HTTP Server Started");
    }

    return server;
}

/*--------------------------------------------------
                    MAIN
--------------------------------------------------*/

void app_main(void)
{
    esp_err_t ret =
        nvs_flash_init();

    if (ret ==
            ESP_ERR_NVS_NO_FREE_PAGES ||
        ret ==
            ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase());

        ret =
            nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    obtain_time();

    start_webserver();

    while (1)
    {
        vTaskDelay(
            pdMS_TO_TICKS(10000));
    }
}