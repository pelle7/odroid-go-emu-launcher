#include "goemu_wifi.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include <esp_http_server.h>
#include <esp_system.h>
//#include <nvs_flash.h>
#include <sys/param.h>
#include "../components/odroid/odroid_display.h"
#include <dirent.h>
#include "goemu_data.h"

#define FILE_WLAN_CONFIG "/sd/odroid/etc/.wlan.txt"

#if CONFIG_WIFI_ALL_CHANNEL_SCAN
#define DEFAULT_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#elif CONFIG_WIFI_FAST_SCAN
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#else
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#endif /*CONFIG_SCAN_METHOD*/

#if CONFIG_WIFI_CONNECT_AP_BY_SIGNAL
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_WIFI_CONNECT_AP_BY_SECURITY
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#else
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#endif /*CONFIG_SORT_METHOD*/

#if CONFIG_FAST_SCAN_THRESHOLD
#define DEFAULT_RSSI CONFIG_FAST_SCAN_MINIMUM_SIGNAL
#if CONFIG_EXAMPLE_OPEN
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WEP
#define DEFAULT_AUTHMODE WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WPA
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WPA2
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA2_PSK
#else
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif
#else
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif /*CONFIG_FAST_SCAN_THRESHOLD*/

static const char *TAG="APP";

static httpd_handle_t server = NULL;

void my_httpd_resp_send_msg(httpd_req_t *req, const char *msg)
{
    httpd_resp_send(req, msg, strlen(msg));
}

void my_httpd_resp_send_msg2(httpd_req_t *req, const char *msg, const char *msg2)
{
    char *tmp = (char*)malloc(strlen(msg) + strlen(msg2) + 8);
    strcpy(tmp, msg);
    strcat(tmp, ": ");
    strcat(tmp, msg2);
    strcat(tmp, "\n");
    httpd_resp_send(req, tmp, strlen(tmp));
    free(tmp);
}

void my_read_dir(httpd_req_t *req, const char *path)
{
    char buf[128];
    odroid_display_lock();
    DIR* dir = opendir(path);
    if (!dir)
    {
        my_httpd_resp_send_msg2(req, "Path not found", path);
        odroid_display_unlock();
        return;
    }
    int max = 512;
    int offset = 0, length = 0;
    char *buffer = (char*)malloc(max);
    sprintf(buf, "<!-- %s -->\n", path);
    length = strlen(buf);
    strcpy(buffer, buf);
    offset+=length;
    
    ESP_LOGI(TAG, "Path: %s", path);
    struct dirent* in_file;
    while ((in_file = readdir(dir)))
    {
        // ESP_LOGI(TAG, "FILE: %s", in_file->d_name);
        sprintf(buf, "%s\n", in_file->d_name);
        length = strlen(buf);
        if (offset + length >= max)
        {
            httpd_resp_send_chunk(req, buffer, offset);
            offset = 0;
        }
        strcpy(&buffer[offset], buf);
        offset += length;
    }
    if (offset>0)
    {
        httpd_resp_send_chunk(req, buffer, offset);
        offset = 0;
    }
    closedir(dir);
    odroid_display_unlock();
    free(buffer);
    httpd_resp_send_chunk(req, buffer, 0);
}

void my_get_file(httpd_req_t *req, const char *path)
{
    odroid_display_lock();
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        my_httpd_resp_send_msg2(req, "Path not found", path);
        odroid_display_unlock();
        return;
    }
    httpd_resp_set_type(req, HTTPD_TYPE_OCTET);
    int buf_size = 4096;
    char *buffer = (char*)malloc(buf_size);
    odroid_display_unlock();
    while (true)
    {
        if (!odroid_display_lock_ext())
        {
            usleep(20*1000UL);
            continue;
        }
        int count = fread(buffer, 1, buf_size, file);
        odroid_display_unlock();
        if (count > 0)
        {
            if (httpd_resp_send_chunk(req, buffer, count) != ESP_OK)
            {
                ESP_LOGI(TAG, "FILE: %s (client abort)", path);
                break;
            }
        }
        if (count != buf_size)
        {
            break;
        }
    }
    httpd_resp_send_chunk(req, buffer, 0);
    odroid_display_lock();
    fclose(file);
    free(buffer);
    odroid_display_unlock();
}

char *goemu_wifi_get_dest_path(httpd_req_t *req)
{
    char filename[128];
    char emuname[128];
    strcpy(filename, "");
    strcpy(emuname, "");
    
    int buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => file=%s", filename);
            }
            if (httpd_query_key_value(buf, "emu", emuname, sizeof(emuname)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => emu=%s", emuname);
            }
        }
        free(buf);
    }
    if (strcmp(filename, "")==0)
    {
        my_httpd_resp_send_msg(req, "Filename not specified");
        return NULL;
    }
    
    char *path_dest = NULL;
    if (filename[0] == '/')
    {
        path_dest = (char*)malloc(strlen(filename)+1);
        strcpy(path_dest, filename);
        return path_dest;
    }
    char *file_ext = strrchr(filename, '.');
    if (!file_ext && emuname[0] == '\0')
    {
        my_httpd_resp_send_msg(req, "Filename has no file extension");
        return NULL;
    }
    file_ext++;
    ESP_LOGI(TAG, "File-Ext: %s", file_ext);
    
    if (strcmp("fw", file_ext)==0)
    {
        path_dest = (char*)malloc(128);
        strcpy(path_dest, "/sd/odroid/firmware");
        strcat(path_dest, "/");
        strcat(path_dest, filename);
    }
    else
    {
        goemu_emu_data_entry *emu = NULL;
        if (emuname[0] != '\0')
        {
            emu = goemu_data_get(emuname);
        }
        else
        {
            emu = goemu_data_get(file_ext);
        }
        if (!emu)
        {
            my_httpd_resp_send_msg(req, "Could not evaluate emulator");
            return NULL;
        }
        path_dest = (char*)malloc(strlen(emu->path)+strlen(filename)+2+9);
        strcpy(path_dest, "/sd/roms/");
        strcat(path_dest, emu->path);
        strcat(path_dest, "/");
        strcat(path_dest, filename);
    }
    return path_dest;
}


/* An HTTP GET handler */
esp_err_t sd_handler_get(httpd_req_t *req)
{
    char filename[128];
    char emuname[128];
    strcpy(filename, "");
    strcpy(emuname, "");
    char*  buf = NULL;
    size_t buf_len;
    ESP_LOGI(TAG, "sd_get_handler");

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }
    
    bool processed = false;

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "dir", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => dir=%s", param);
                my_read_dir(req, param);
                processed = true;
            }
            if (httpd_query_key_value(buf, "file", filename, sizeof(filename)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => file=%s", filename);
            }
            if (httpd_query_key_value(buf, "emu", emuname, sizeof(emuname)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => emu=%s", emuname);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }
    
    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    if (!processed)
    {
       char *path_dest = goemu_wifi_get_dest_path(req);
        if (!path_dest)
        {
            return ESP_FAIL;
        }
       my_get_file(req, path_dest);
    }

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}


httpd_uri_t goemu_uri_sdcard_get = {
    .uri       = "/sd",
    .method    = HTTP_GET,
    .handler   = sd_handler_get,
    .user_ctx  = NULL
};

/* An HTTP POST handler */
esp_err_t sd_handler_post(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
    char *path_dest = goemu_wifi_get_dest_path(req);
    if (!path_dest)
    {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File-Dest: %s", path_dest);
    
    odroid_display_lock();
    FILE *file = fopen(path_dest, "rb");
    if (file)
    {
        fclose(file);
        odroid_display_unlock();
        my_httpd_resp_send_msg2(req, "File already exists", path_dest);
        return ESP_FAIL;
    }
    file = fopen(path_dest, "wb");
    odroid_display_unlock();
    
    int buf_size = 4096;
    char *buf = (char*)malloc(buf_size);
    int ret, remaining = req->content_len;
    int offset = 0;
    int total = remaining;

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, buf_size))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            fclose(file);
            free(path_dest);
            odroid_display_unlock();
            return ESP_FAIL;
        }
        
        while (!odroid_display_lock_ext())
        {
            usleep(20*1000UL);
            continue;
        }
        
        int count = fwrite(buf, 1, ret, file);
        if (count != ret)
        {
            // write error?
            fclose(file);
            free(path_dest);
            odroid_display_unlock();
            return ESP_FAIL;
        }
        offset+=ret;
        ESP_LOGI(TAG, "File-Dest: %s (%d/%d)", path_dest, offset, total);
        
        // httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;
        if (remaining <=0)
        {
            fclose(file);
        }
        odroid_display_unlock();
    }
    ESP_LOGI(TAG, "File-Dest: %s (READY)", path_dest);
    free(buf);
    free(path_dest);

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t goemu_uri_sdcard_post = {
    .uri       = "/sd",
    .method    = HTTP_POST,
    .handler   = sd_handler_post,
    .user_ctx  = NULL
};

esp_err_t sd_handler_delete(httpd_req_t *req)
{
    char *path_dest = goemu_wifi_get_dest_path(req);
    if (!path_dest)
    {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File-Dest: %s", path_dest);
    
    odroid_display_lock();
    FILE *file = fopen(path_dest, "rb");
    if (!file)
    {
        odroid_display_unlock();
        my_httpd_resp_send_msg2(req, "File doesn't exist", path_dest);
        return ESP_FAIL;
    }
    fclose(file);
    remove(path_dest);
    odroid_display_unlock();
    free(path_dest);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t goemu_uri_sdcard_delete = {
    .uri       = "/sd",
    .method    = HTTP_DELETE,
    .handler   = sd_handler_delete,
    .user_ctx  = NULL
};


httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 16;
    
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &goemu_uri_sdcard_get);
        httpd_register_uri_handler(server, &goemu_uri_sdcard_post);
        httpd_register_uri_handler(server, &goemu_uri_sdcard_delete);
        ESP_LOGI(TAG, "Registering URI handlers: ready");
        ESP_LOGI(TAG, "---- UP AND RUNNING ----");
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}



















static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *) ctx;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

        /* Start the web server */
        if (*server == NULL) {
            *server = start_webserver();
        }
        ESP_LOGI(TAG, "start_webserver: ready");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_ERROR_CHECK(esp_wifi_connect());

        /* Stop the web server */
        if (*server) {
            stop_webserver(*server);
            *server = NULL;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

bool wifi_prepare(wifi_config_t *wifi_config)
{
    odroid_display_lock();
    FILE *f = fopen(FILE_WLAN_CONFIG, "r");
    if (!f)
    {
        ESP_LOGI(TAG, "WLAN-Config missing file: %s", FILE_WLAN_CONFIG);
        odroid_display_unlock();
        return false;
    }
    char *ssid = NULL;
    char *password = NULL;
    int selected = -1;
    int nr = -1;
    int buf_size = 4096;
    char *buffer = (char*)malloc(buf_size);
    while (fgets(buffer, buf_size, f) != NULL)
    {
        while (true)
        {
            int len = strlen(buffer);
            if (len==0) break;
            if (buffer[len-1] =='\n' || buffer[len-1] == '\r') buffer[len-1] = '\0';
            else break;
        }
        if (buffer[0] == '\0')
        {
            continue;
        }
        if (buffer[0] == '#')
        {
            continue;
        }
        char *ptr = strchr(buffer, '=');
        if (!ptr)
        {
            continue;
        }
        ptr[0] = '\0';
        char *key = buffer;
        char *value = (ptr+1);
        
        if (strcmp("active", key)==0)
        {
            if (sscanf(value, "%d", &selected) == 1) 
            {
                selected--;
                ESP_LOGI(TAG, "WLAN-Config: selected: %d", selected);
            }
        } else if (strcmp("name", key)==0)
        {
            nr++;
            if (nr>selected) break;
        } else if (strcmp("ssid", key)==0)
        {
            if (nr==selected)
            {
                ssid = (char*)malloc(strlen(value) + 1);
                strcpy(ssid, value);
            }
        } else if (strcmp("password", key)==0)
        {
            if (nr==selected)
            {
                password = (char*)malloc(strlen(value) + 1);
                strcpy(password, value);
            }
        }
    }
    fclose(f);
    free(buffer);
    odroid_display_unlock();
    bool rc = false;
    if (ssid && password)
    {
        printf("WLAN-SSID: %s\n", ssid);
        memcpy(wifi_config->sta.ssid, ssid, strlen(ssid) + 1);
        memcpy(wifi_config->sta.password, password, strlen(password) + 1);
        rc = true;
    }
    if (ssid) free(ssid);
    if (password) free(password);
    return rc;
}

/* Initialize Wi-Fi as sta and set scan method */
static void wifi_scan(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "SSID",
            .password = "PASSWORD",
            .scan_method = DEFAULT_SCAN_METHOD,
            .sort_method = DEFAULT_SORT_METHOD,
            .threshold.rssi = DEFAULT_RSSI,
            .threshold.authmode = DEFAULT_AUTHMODE,
        },
    };
    if (!wifi_prepare(&wifi_config))
    {
        ESP_LOGI(TAG, "WLAN-Config missing or none selected");
        return;
    }

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, &server));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void test_time()
{
  time_t rawtime;
  struct tm * timeinfo;

  time ( &rawtime );
  timeinfo = localtime ( &rawtime );
  printf ( "Current local time and date: %s\n", asctime (timeinfo) );
}


void goemu_wifi_start()
{
    wifi_scan();
    // test_time();
}
