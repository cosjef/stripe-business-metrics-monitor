#include "portal.h"
#include "provision.h"
#include "stripe_key.h"

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <string.h>

static const char *TAG = "portal";

/* The AP's own address, which esp_netif assigns by default. */
#define PORTAL_IP_STR "192.168.4.1"
#define PORTAL_IP_A 192
#define PORTAL_IP_B 168
#define PORTAL_IP_C 4
#define PORTAL_IP_D 1

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static httpd_handle_t s_server = NULL;
static TaskHandle_t s_dns_task = NULL;
static int s_dns_sock = -1;
static portal_creds_cb_t s_on_creds = NULL;
static portal_key_cb_t s_on_key = NULL;

/*
 * The setup page.
 *
 * Deliberately one self-contained page with no external assets: the phone is
 * joined to an AP with no internet, so anything fetched from a CDN would hang
 * and leave the customer looking at a half-rendered form. Spec 6.2 calls this
 * the screen with 100% customer exposure -- it is worth the inline CSS.
 */
static const char PAGE_HTML_HEAD[] =
"<!DOCTYPE html><html><head>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Set up your revenue display</title><style>"
"*{box-sizing:border-box}"
"body{font:16px/1.5 -apple-system,system-ui,sans-serif;background:#121211;"
"color:#F4F2EC;margin:0;padding:24px;max-width:420px;margin:0 auto}"
"h1{font-size:22px;margin:8px 0 4px}"
"p.sub{color:#8E8C84;margin:0 0 24px}"
"label{display:block;margin:16px 0 6px;color:#8E8C84;font-size:14px}"
"input{width:100%;padding:12px;font-size:16px;border-radius:8px;"
"border:1px solid #3A3A37;background:#1E1E1C;color:#F4F2EC}"
"button{width:100%;margin-top:24px;padding:14px;font-size:16px;font-weight:600;"
"border:0;border-radius:8px;background:#5DCAA5;color:#0A0A09}"
"button:active{opacity:.8}"
".err{background:#EF9F27;color:#0A0A09;padding:10px 12px;border-radius:8px;"
"margin-bottom:16px}"
"</style></head><body>"
"<h1>Set up your display</h1>"
"<p class=sub>Choose the WiFi network the display should join.</p>";

/* Second half, emitted after the optional error banner. Kept separate so the
 * page never passes through printf: the inline CSS contains '%' characters
 * (widths, opacity) that would be read as format specifiers. */
static const char PAGE_HTML_TAIL[] =
"<form method=POST action=/save>"
"<label for=s>Network name</label>"
"<input id=s name=ssid autocapitalize=none autocorrect=off required>"
"<label for=p>Password</label>"
"<input id=p name=pass type=password autocapitalize=none autocorrect=off>"
"<button type=submit>Connect</button>"
"</form></body></html>";

/*
 * Stripe key entry. Reached after WiFi is confirmed, so the key can be
 * validated against the live API the moment it is submitted (spec 9.1 step 4)
 * rather than stored untested.
 */
static const char PAGE_KEY_HEAD[] =
"<!DOCTYPE html><html><head>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Connect Stripe</title><style>"
"*{box-sizing:border-box}"
"body{font:16px/1.5 -apple-system,system-ui,sans-serif;background:#121211;"
"color:#F4F2EC;margin:0;padding:24px;max-width:420px;margin:0 auto}"
"h1{font-size:22px;margin:8px 0 4px}"
"p.sub{color:#8E8C84;margin:0 0 20px}"
"label{display:block;margin:16px 0 6px;color:#8E8C84;font-size:14px}"
"input{width:100%;padding:12px;font-size:16px;border-radius:8px;"
"border:1px solid #3A3A37;background:#1E1E1C;color:#F4F2EC;"
"font-family:ui-monospace,monospace}"
"button{width:100%;margin-top:20px;padding:14px;font-size:16px;font-weight:600;"
"border:0;border-radius:8px;background:#5DCAA5;color:#0A0A09}"
".err{background:#EF9F27;color:#0A0A09;padding:10px 12px;border-radius:8px;"
"margin-bottom:16px}"
".ok{background:#5DCAA5;color:#0A0A09;padding:12px;border-radius:8px;"
"margin-bottom:16px;font-weight:600}"
"ul{color:#8E8C84;font-size:14px;padding-left:20px;margin:8px 0 0}"
"</style></head><body>"
"<h1>Connect Stripe</h1>"
"<p class=sub>The display needs a restricted key that can read your "
"subscriptions.</p>";

static const char PAGE_KEY_TAIL[] =
"<form method=POST action=/key>"
"<label for=k>Restricted API key</label>"
"<input id=k name=key placeholder='rk_live_...' autocapitalize=none "
"autocorrect=off spellcheck=false required>"
"<button type=submit>Verify and finish</button>"
"</form>"
"<ul>"
"<li>Create it in Stripe under Developers &rarr; API keys &rarr; "
"Restricted keys</li>"
"<li>Grant <b>Read</b> on Subscriptions, Events and Customers</li>"
"<li>Leave everything else set to None</li>"
"</ul>"
"</body></html>";

static const char PAGE_OK[] =
"<!DOCTYPE html><html><head>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Connecting</title><style>"
"body{font:16px/1.5 -apple-system,system-ui,sans-serif;background:#121211;"
"color:#F4F2EC;margin:0;padding:24px;max-width:420px;margin:0 auto;"
"text-align:center}"
"h1{font-size:22px;margin-top:48px}"
"p{color:#8E8C84}"
"</style></head><body>"
"<h1>Connecting&hellip;</h1>"
"<p>The display is joining your network. You can close this page and "
"reconnect your phone to your usual WiFi.</p>"
"</body></html>";

/* Percent-decode `src` into `dst` in place of the usual form encoding. */
static void url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t o = 0;

    for (size_t i = 0; src[i] && o + 1 < dst_len; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            dst[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[o++] = ' ';
        } else {
            dst[o++] = src[i];
        }
    }

    dst[o] = '\0';
}

/* Pull one field out of an application/x-www-form-urlencoded body. */
static bool form_field(const char *body, const char *name,
                       char *out, size_t out_len)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", name);

    const char *p = strstr(body, needle);
    if (!p) {
        return false;
    }
    p += strlen(needle);

    const char *end = strchr(p, '&');
    const size_t len = end ? (size_t)(end - p) : strlen(p);

    char raw[256];
    const size_t n = len < sizeof(raw) - 1 ? len : sizeof(raw) - 1;
    memcpy(raw, p, n);
    raw[n] = '\0';

    url_decode(out, out_len, raw);
    return true;
}

static esp_err_t serve_form(httpd_req_t *req, const char *error)
{
    httpd_resp_set_type(req, "text/html");

    /* Chunked, so the page never has to be assembled in one buffer and the
     * error text is inserted without a format string. */
    httpd_resp_sendstr_chunk(req, PAGE_HTML_HEAD);

    if (error) {
        httpd_resp_sendstr_chunk(req, "<div class=err>");
        httpd_resp_sendstr_chunk(req, error);
        httpd_resp_sendstr_chunk(req, "</div>");
    }

    httpd_resp_sendstr_chunk(req, PAGE_HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t get_handler(httpd_req_t *req)
{
    return serve_form(req, NULL);
}

/* Render the Stripe key page, with an optional banner above the form. */
static esp_err_t serve_key_page(httpd_req_t *req, const char *banner,
                                bool banner_is_error)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_KEY_HEAD);

    if (banner) {
        httpd_resp_sendstr_chunk(req, banner_is_error ? "<div class=err>"
                                                      : "<div class=ok>");
        httpd_resp_sendstr_chunk(req, banner);
        httpd_resp_sendstr_chunk(req, "</div>");
    }

    httpd_resp_sendstr_chunk(req, PAGE_KEY_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t key_get_handler(httpd_req_t *req)
{
    return serve_key_page(req, NULL, false);
}

static esp_err_t key_post_handler(httpd_req_t *req)
{
    char body[512];
    const int len = req->content_len < (int)sizeof(body) - 1
                        ? req->content_len : (int)sizeof(body) - 1;

    const int got = httpd_req_recv(req, body, len);
    if (got <= 0) {
        return httpd_resp_send_500(req);
    }
    body[got] = '\0';

    char key[STRIPE_KEY_MAX_LEN + 1] = "";
    form_field(body, "key", key, sizeof(key));

    /* Shape check first: it is instant and catches the common paste mistakes
     * without a round trip to Stripe. */
    const stripe_key_result_t shape = stripe_key_validate(key);
    if (shape != STRIPE_KEY_OK) {
        ESP_LOGW(TAG, "key rejected: %s", stripe_key_result_str(shape));
        return serve_key_page(req, stripe_key_result_str(shape), true);
    }

    /* Then the live call, which is the only thing that can actually confirm
     * the key works and has the right scope (spec 9.1 step 4). */
    char msg[192] = "";
    const bool accepted = s_on_key ? s_on_key(key, msg, sizeof(msg)) : false;

    if (!accepted) {
        return serve_key_page(req, msg[0] ? msg : "Could not verify that key",
                              true);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_KEY_HEAD);
    httpd_resp_sendstr_chunk(req, "<div class=ok>");
    httpd_resp_sendstr_chunk(req, msg);
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req,
        "<p class=sub>Setup is complete. The display will start showing your "
        "revenue shortly.</p></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[512];
    const int len = req->content_len < (int)sizeof(body) - 1
                        ? req->content_len : (int)sizeof(body) - 1;

    const int got = httpd_req_recv(req, body, len);
    if (got <= 0) {
        return httpd_resp_send_500(req);
    }
    body[got] = '\0';

    char ssid[WIFI_SSID_MAX_LEN + 1] = "";
    char pass[WIFI_PASS_MAX_LEN + 1] = "";
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));

    /* Validate before storing, and re-render the form with the reason rather
     * than accepting credentials that cannot work. */
    const wifi_cred_result_t v = wifi_creds_validate(ssid, pass);
    if (v != WIFI_CRED_OK) {
        ESP_LOGW(TAG, "rejected credentials: %s", wifi_cred_result_str(v));
        return serve_form(req, wifi_cred_result_str(v));
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, PAGE_OK);

    /* Hand off after responding, so the customer sees confirmation before the
     * AP goes away underneath them. */
    if (s_on_creds) {
        s_on_creds(ssid, pass);
    }

    return ESP_OK;
}

/*
 * Answer every other path with a redirect to the form.
 *
 * Phones probe a known URL to decide whether a network has internet; anything
 * other than the expected 204 makes them show the portal. Redirecting all
 * unknown paths is what makes the page open by itself.
 */
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" PORTAL_IP_STR "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/*
 * Minimal DNS responder: answer every A query with our own address.
 *
 * Enough of the protocol to satisfy a captive portal check -- echo the query
 * back with an answer appended, using a compression pointer to the name
 * already present in the question section.
 */
static void dns_task(void *arg)
{
    (void)arg;

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(DNS_PORT),
    };

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }

    if (bind(s_dns_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(s_dns_sock);
        s_dns_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "dns responder listening");

    uint8_t buf[DNS_MAX_LEN];

    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);

        const int n = recvfrom(s_dns_sock, buf, sizeof(buf), 0,
                               (struct sockaddr *)&client, &clen);
        if (n < 12) {
            if (n < 0) {
                break;  /* socket closed by portal_stop */
            }
            continue;
        }

        /* Flags: response, recursion available. Answer count 1. */
        buf[2] = 0x81;
        buf[3] = 0x80;
        buf[6] = 0x00;
        buf[7] = 0x01;
        buf[8] = buf[9] = buf[10] = buf[11] = 0x00;

        int o = n;
        if (o + 16 > (int)sizeof(buf)) {
            continue;
        }

        buf[o++] = 0xC0; buf[o++] = 0x0C;          /* name: pointer to offset 12 */
        buf[o++] = 0x00; buf[o++] = 0x01;          /* type A */
        buf[o++] = 0x00; buf[o++] = 0x01;          /* class IN */
        buf[o++] = 0x00; buf[o++] = 0x00;
        buf[o++] = 0x00; buf[o++] = 0x3C;          /* TTL 60s */
        buf[o++] = 0x00; buf[o++] = 0x04;          /* rdlength 4 */
        buf[o++] = PORTAL_IP_A; buf[o++] = PORTAL_IP_B;
        buf[o++] = PORTAL_IP_C; buf[o++] = PORTAL_IP_D;

        sendto(s_dns_sock, buf, o, 0, (struct sockaddr *)&client, clen);
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    vTaskDelete(NULL);
}

esp_err_t portal_start(portal_creds_cb_t on_creds, portal_key_cb_t on_key)
{
    s_on_creds = on_creds;
    s_on_key = on_key;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    /* Needed for the catch-all redirect, which is what makes phones open the
     * page without the customer typing an address. */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "http start failed");

    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = get_handler,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_handler,
    };
    const httpd_uri_t key_get = {
        .uri = "/key", .method = HTTP_GET, .handler = key_get_handler,
    };
    const httpd_uri_t key_post = {
        .uri = "/key", .method = HTTP_POST, .handler = key_post_handler,
    };
    /* Registered last: the wildcard would otherwise shadow the routes above. */
    const httpd_uri_t any = {
        .uri = "/*", .method = HTTP_GET, .handler = redirect_handler,
    };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);
    httpd_register_uri_handler(s_server, &key_get);
    httpd_register_uri_handler(s_server, &key_post);
    httpd_register_uri_handler(s_server, &any);

    xTaskCreate(dns_task, "dns", 4096, NULL, 5, &s_dns_task);

    ESP_LOGI(TAG, "portal at http://%s/", PORTAL_IP_STR);
    return ESP_OK;
}

void portal_stop(void)
{
    if (s_dns_sock >= 0) {
        /* Closing the socket unblocks recvfrom so the task can exit. */
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    s_dns_task = NULL;

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    ESP_LOGI(TAG, "portal stopped");
}
