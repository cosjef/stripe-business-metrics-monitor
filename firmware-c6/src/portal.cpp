/*
 * Captive portal on WebServer + DNSServer.
 *
 * The pages are copied verbatim from firmware/main/portal.c -- same markup,
 * same palette, same copy. They were written for a customer holding a phone,
 * and nothing about the framework change is a reason to redesign them.
 *
 * What did change: Arduino's WebServer parses form fields itself, so the
 * hand-rolled percent-decoder and form-field scanner are gone. Less code that
 * can be wrong about a password containing '+' or '%'.
 */
#include "portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

/* The address the AP hands out and answers on. Any hostname resolves here,
 * which is what triggers the phone's "sign in to network" sheet. */
static const IPAddress PORTAL_IP(192, 168, 4, 1);
static const byte DNS_PORT = 53;

static DNSServer s_dns;
static WebServer s_http(80);

static portal_creds_cb_t s_on_creds;
static portal_key_cb_t   s_on_key;
static bool s_key_phase;
static bool s_running;
static bool s_ap_up;     /* the softAP survives a phase change */
static char s_ssid[33];

/* ---- pages (verbatim from the ESP-IDF build) ---- */

/*
 * The password reveal is a checkbox, not an eye icon: there is no image to
 * load on a captive portal with no route to the internet, and a labelled
 * checkbox reads correctly to a screen reader.
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
".rev{display:flex;align-items:center;justify-content:flex-end;gap:6px;"
"margin-top:8px;color:#8E8C84;font-size:14px}"
".rev input{width:auto;margin:0}"
".rev label{margin:0;color:#8E8C84;font-size:14px}"
"</style></head><body>"
"<h1>Set up your display</h1>"
"<p class=sub>Choose the WiFi network the display should join.</p>";

static const char PAGE_HTML_TAIL[] =
"<form method=POST action=/save>"
"<label for=s>Network name</label>"
"<input id=s name=ssid autocapitalize=none autocorrect=off required>"
"<label for=p>Password</label>"
"<input id=p name=pass type=password autocapitalize=none autocorrect=off>"
"<div class=rev>"
"<input type=checkbox id=show onclick=\"document.getElementById('p').type="
"this.checked?'text':'password'\">"
"<label for=show>Show password</label>"
"</div>"
"<button type=submit>Connect</button>"
"</form></body></html>";

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

/*
 * The scopes named here are the ones the firmware actually calls:
 * /v1/subscriptions and /v1/invoices. It previously asked for Events and
 * Customers as well, inherited from a plan to use the events feed that was
 * measured and dropped -- so it was requesting access it never used, on a
 * device whose whole argument is that it can be trusted with a key.
 */
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
"<li>Grant <b>Read</b> on Subscriptions and Invoices</li>"
"<li>Leave everything else set to None</li>"
"</ul>"
"</body></html>";

/*
 * Shown after the WiFi form is submitted, while the device joins.
 *
 * The meta refresh matters: joining takes ~10-30s, during which the device is
 * not serving. Without it this page is terminal -- the owner sits on
 * "Connecting..." with no indication that the key form is the next step, which
 * reads as a hang. Refreshing back to "/" lands on the key form as soon as the
 * portal is serving again. The interval is comfortably longer than a typical
 * join so the phone is not hammering a socket that is not listening yet.
 */
static const char PAGE_OK[] =
"<!DOCTYPE html><html><head>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<meta http-equiv=refresh content='8;url=/'>"
"<title>Connecting</title><style>"
"body{font:16px/1.5 -apple-system,system-ui,sans-serif;background:#121211;"
"color:#F4F2EC;margin:0;padding:24px;max-width:420px;margin:0 auto;"
"text-align:center}"
"h1{font-size:22px;margin-top:48px}"
"p{color:#8E8C84}"
"</style></head><body>"
"<h1>Connecting&hellip;</h1>"
"<p>The display is joining your network. This page will move on by itself "
"in a few seconds &mdash; stay connected to the setup network.</p>"
"</body></html>";

static const char PAGE_DONE[] =
"<!DOCTYPE html><html><head>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Done</title><style>"
"body{font:16px/1.5 -apple-system,system-ui,sans-serif;background:#121211;"
"color:#F4F2EC;margin:0;padding:24px;max-width:420px;margin:0 auto;"
"text-align:center}"
"h1{font-size:22px;margin-top:48px}"
"p{color:#8E8C84}"
"</style></head><body>"
"<h1>All set</h1>"
"<p>The display is connected and will show your revenue shortly.</p>"
"</body></html>";

/* ---- handlers ---- */

/*
 * The error banner is concatenated rather than printf'd: the inline CSS
 * contains '%' characters (widths, opacity) that a format string would read
 * as specifiers. This bit the ESP-IDF version first.
 */
static void serve_form(const char *error)
{
    String page(PAGE_HTML_HEAD);
    if (error) {
        page += "<div class=err>";
        page += error;
        page += "</div>";
    }
    page += PAGE_HTML_TAIL;
    s_http.send(200, "text/html", page);
}

static void serve_key_page(const char *banner, bool banner_is_error)
{
    String page(PAGE_KEY_HEAD);
    if (banner) {
        page += banner_is_error ? "<div class=err>" : "<div class=ok>";
        page += banner;
        page += "</div>";
    }
    page += PAGE_KEY_TAIL;
    s_http.send(200, "text/html", page);
}

static void handle_root(void)
{
    if (s_key_phase) {
        serve_key_page(NULL, false);
    } else {
        serve_form(NULL);
    }
}

static void handle_save(void)
{
    const String ssid = s_http.arg("ssid");
    const String pass = s_http.arg("pass");

    if (ssid.length() == 0) {
        serve_form("Enter a network name.");
        return;
    }
    if (ssid.length() > 32) {
        serve_form("That network name is too long.");
        return;
    }
    /* WPA2 PSKs are 8-63 characters; an open network sends nothing. Reject
     * the in-between case here rather than letting the join fail later with
     * nothing on screen to explain it. */
    if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) {
        serve_form("That password is not a valid WiFi password.");
        return;
    }

    /* Answer before acting: the phone loses the AP the moment we switch to
     * station mode, so a response sent afterwards never arrives. */
    s_http.send(200, "text/html", PAGE_OK);
    delay(200);

    if (s_on_creds) {
        s_on_creds(ssid.c_str(), pass.c_str());
    }
}

static void handle_key_get(void)
{
    serve_key_page(NULL, false);
}

static void handle_key_post(void)
{
    const String key = s_http.arg("key");

    if (key.length() == 0) {
        serve_key_page("Enter a key.", true);
        return;
    }

    char msg[128] = "";
    const bool ok = s_on_key ? s_on_key(key.c_str(), msg, sizeof(msg)) : false;

    if (!ok) {
        serve_key_page(msg[0] ? msg : "That key did not work.", true);
        return;
    }

    /*
     * Send the confirmation, then let the callback's mode change take effect.
     *
     * The success path tears the AP down once setup completes, and if that
     * happens before this response is flushed the owner's browser just loses
     * the connection -- the page closes with no confirmation at all, which
     * reads as a failure even though the key was accepted and saved. The
     * delay gives the socket time to drain before the radio goes away.
     */
    s_http.send(200, "text/html", PAGE_DONE);
    s_http.client().flush();
    delay(600);
}

/*
 * Everything else redirects to "/".
 *
 * This is what makes it a *captive* portal: phones probe a known URL and, on
 * getting a redirect instead of their expected response, open the sign-in
 * sheet automatically. Without it the owner has to type an IP address.
 */
static void handle_not_found(void)
{
    s_http.sendHeader("Location", String("http://") + PORTAL_IP.toString() + "/");
    s_http.send(302, "text/plain", "");
}

/* ---- lifecycle ---- */

bool portal_start(portal_creds_cb_t on_creds, portal_key_cb_t on_key,
                  const char *ap_ssid, bool key_phase)
{
    s_on_creds = on_creds;
    s_on_key = on_key;
    s_key_phase = key_phase;
    snprintf(s_ssid, sizeof(s_ssid), "%s", ap_ssid ? ap_ssid : "Setup");

    /*
     * WIFI_AP_STA for both phases, and the AP is never torn down between
     * them.
     *
     * The key phase needs the station link, because the key is validated
     * against the live API before it is stored. An earlier version used
     * WIFI_AP for the wifi phase and switched to WIFI_AP_STA for the key
     * phase, restarting the softAP in between -- which dropped the owner's
     * phone off the network mid-setup, right before the key form they were
     * about to fill in. Bringing the AP up once, in a mode that serves both
     * phases, keeps the phone associated the whole way through.
     */
    /*
     * The setup AP is OPEN, and the key form is served over plain HTTP.
     *
     * For the few minutes of provisioning, anyone in radio range can see the
     * WiFi password and the Stripe key as they are submitted. That is a real
     * exposure and it is deliberate: a WPA2 AP needs a passphrase the owner
     * has no way to learn before connecting, and HTTPS needs a certificate
     * for an IP address that every phone would reject with a warning worse
     * than the problem it solves.
     *
     * What limits it: the window is minutes rather than always, the key is
     * meant to be a read-only restricted key, and the alternative designs
     * either cannot be used by a non-technical owner or teach them to click
     * through certificate warnings.
     *
     * If this device ever ships to people who are not the person who built
     * it, this is the first thing to revisit.
     */
    if (!s_ap_up) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(PORTAL_IP, PORTAL_IP, IPAddress(255, 255, 255, 0));

        if (!WiFi.softAP(s_ssid)) {
            Serial.println("portal: softAP failed");
            return false;
        }
        s_ap_up = true;
    }

    /* "*" catches every lookup, which is what redirects the phone to us. */
    s_dns.start(DNS_PORT, "*", PORTAL_IP);

    s_http.on("/", HTTP_GET, handle_root);
    s_http.on("/save", HTTP_POST, handle_save);
    s_http.on("/key", HTTP_GET, handle_key_get);
    s_http.on("/key", HTTP_POST, handle_key_post);
    s_http.onNotFound(handle_not_found);
    s_http.begin();

    s_running = true;
    Serial.printf("portal: http://%s/ as \"%s\" (%s phase)\n",
                  PORTAL_IP.toString().c_str(), s_ssid,
                  key_phase ? "key" : "wifi");
    return true;
}

/*
 * Stop serving, but leave the AP up.
 *
 * Used between the wifi and key phases, where tearing the AP down would
 * disconnect the phone that is halfway through setup.
 */
void portal_set_phase(bool key_phase)
{
    s_key_phase = key_phase;
    Serial.printf("portal: now serving %s phase\n", key_phase ? "key" : "wifi");
}

void portal_pause(void)
{
    if (!s_running) {
        return;
    }
    s_http.stop();
    s_dns.stop();
    s_running = false;
}

void portal_stop(void)
{
    if (s_running) {
        s_http.stop();
        s_dns.stop();
        s_running = false;
    }
    if (s_ap_up) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        s_ap_up = false;
    }
    Serial.println("portal: stopped");
}

void portal_tick(void)
{
    if (!s_running) {
        return;
    }
    s_dns.processNextRequest();
    s_http.handleClient();
}

const char *portal_ssid(void)
{
    return s_ssid;
}
