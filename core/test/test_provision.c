/*
 * Host tests for setup-mode provisioning logic.
 *
 *   cd firmware/test && make && ./test_provision
 *
 * This covers the screen with 100% customer exposure (spec 6.2, State C), so
 * the failure modes here are the ones that generate support contact: a
 * rejected-but-valid password, or an SSID on screen that does not match the
 * one in the phone's WiFi list.
 */
#include <stdio.h>
#include <string.h>

#include "../include/provision.h"

static int failures = 0;
static int checks = 0;

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void check_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
    }
}

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/* Build a passphrase of exactly `n` characters. */
static void make_pass(char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)('a' + (i % 26));
    }
    buf[n] = '\0';
}

/* ---- credential validation ---- */

static void test_accepts_normal_credentials(void)
{
    printf("normal credentials are accepted\n");

    check_int("typical home network",
              wifi_creds_validate("MyHomeWiFi", "correcthorsebattery"), WIFI_CRED_OK);
    check_int("8-char password (minimum)",
              wifi_creds_validate("net", "12345678"), WIFI_CRED_OK);
    check_int("password with spaces",
              wifi_creds_validate("net", "my pass phrase"), WIFI_CRED_OK);
    check_int("password with symbols",
              wifi_creds_validate("net", "p@ssw0rd!#$%"), WIFI_CRED_OK);
    check_int("SSID with spaces",
              wifi_creds_validate("My Home Network", "password1"), WIFI_CRED_OK);
}

/*
 * Open networks have no passphrase. Rejecting them would strand anyone whose
 * network is genuinely open, and there is no way for them to recover from the
 * portal.
 */
static void test_accepts_open_network(void)
{
    printf("open networks (no passphrase) are accepted\n");

    check_int("empty password", wifi_creds_validate("OpenNet", ""), WIFI_CRED_OK);
    check_int("NULL password", wifi_creds_validate("OpenNet", NULL), WIFI_CRED_OK);
}

static void test_rejects_bad_ssid(void)
{
    printf("invalid SSIDs are rejected\n");

    check_int("empty SSID", wifi_creds_validate("", "password1"),
              WIFI_CRED_SSID_EMPTY);
    check_int("NULL SSID", wifi_creds_validate(NULL, "password1"),
              WIFI_CRED_SSID_EMPTY);

    char ssid[WIFI_SSID_MAX_LEN + 10];
    memset(ssid, 'a', sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    check_int("over-long SSID", wifi_creds_validate(ssid, "password1"),
              WIFI_CRED_SSID_TOO_LONG);

    /* Exactly at the limit must be accepted. */
    ssid[WIFI_SSID_MAX_LEN] = '\0';
    check_int("SSID at 32 chars", wifi_creds_validate(ssid, "password1"),
              WIFI_CRED_OK);
}

static void test_rejects_bad_password(void)
{
    printf("invalid passphrases are rejected\n");

    check_int("7-char password", wifi_creds_validate("net", "1234567"),
              WIFI_CRED_PASS_TOO_SHORT);
    check_int("1-char password", wifi_creds_validate("net", "x"),
              WIFI_CRED_PASS_TOO_SHORT);

    char pass[WIFI_PASS_MAX_LEN + 10];
    make_pass(pass, WIFI_PASS_MAX_LEN + 1);
    check_int("64-char password", wifi_creds_validate("net", pass),
              WIFI_CRED_PASS_TOO_LONG);

    /* Exactly at the limit must be accepted. */
    make_pass(pass, WIFI_PASS_MAX_LEN);
    check_int("63-char password", wifi_creds_validate("net", pass),
              WIFI_CRED_OK);
}

/*
 * WPA2 passphrases are ASCII. A non-ASCII byte usually means the phone
 * substituted a smart quote or the customer pasted from a document -- worth a
 * clear message rather than a silent connection failure later.
 */
static void test_rejects_non_ascii_password(void)
{
    printf("non-ASCII passphrases are rejected with a clear reason\n");

    check_int("high byte in password",
              wifi_creds_validate("net", "pass\xC2\xA0word"),
              WIFI_CRED_PASS_NOT_ASCII);
    check_int("control character",
              wifi_creds_validate("net", "pass\x01word"),
              WIFI_CRED_PASS_NOT_ASCII);
}

static void test_every_result_has_a_message(void)
{
    printf("every failure reason has a message\n");

    const wifi_cred_result_t all[] = {
        WIFI_CRED_OK, WIFI_CRED_SSID_EMPTY, WIFI_CRED_SSID_TOO_LONG,
        WIFI_CRED_PASS_TOO_SHORT, WIFI_CRED_PASS_TOO_LONG,
        WIFI_CRED_PASS_NOT_ASCII,
    };

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *msg = wifi_cred_result_str(all[i]);
        char what[64];
        snprintf(what, sizeof(what), "result %d has a message", (int)all[i]);
        check_true(what, msg != NULL && msg[0] != '\0');
    }
}

/* ---- setup SSID ---- */

/*
 * The SSID on the screen must match the one in the phone's WiFi list exactly.
 * A mismatch here is a support call from a customer who cannot find the
 * network.
 */
static void test_setup_ssid_format(void)
{
    printf("setup SSID formatting\n");

    char ssid[SETUP_SSID_LEN];

    const uint8_t mac1[6] = {0x24, 0x6F, 0x28, 0xAA, 0x4C, 0x21};
    setup_ssid_from_mac(mac1, ssid, sizeof(ssid));
    check_str("uses low two MAC bytes, uppercase", ssid, "Setup-4C21");

    /* Leading zeros must be preserved, or the name is a digit short. */
    const uint8_t mac2[6] = {0x00, 0x11, 0x22, 0x33, 0x00, 0x0F};
    setup_ssid_from_mac(mac2, ssid, sizeof(ssid));
    check_str("preserves leading zeros", ssid, "Setup-000F");

    const uint8_t mac3[6] = {0, 0, 0, 0, 0xFF, 0xFF};
    setup_ssid_from_mac(mac3, ssid, sizeof(ssid));
    check_str("all-ones", ssid, "Setup-FFFF");

    const uint8_t mac4[6] = {0, 0, 0, 0, 0x00, 0x00};
    setup_ssid_from_mac(mac4, ssid, sizeof(ssid));
    check_str("all-zeros", ssid, "Setup-0000");
}

static void test_setup_ssid_fits_802_11(void)
{
    printf("setup SSID is a legal 802.11 SSID\n");

    char ssid[SETUP_SSID_LEN];
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAA, 0x4C, 0x21};
    setup_ssid_from_mac(mac, ssid, sizeof(ssid));

    check_true("at most 32 bytes", strlen(ssid) <= WIFI_SSID_MAX_LEN);
    check_true("non-empty", strlen(ssid) > 0);
    check_int("exactly 10 chars", (int)strlen(ssid), 10);

    /* It must also validate as an SSID a customer could connect to. */
    check_int("passes SSID validation",
              wifi_creds_validate(ssid, "12345678"), WIFI_CRED_OK);
}

/*
 * Two devices must not advertise the same name, or a customer with two of
 * them cannot tell which is which.
 */
static void test_setup_ssid_differs_by_mac(void)
{
    printf("different MACs give different SSIDs\n");

    char a[SETUP_SSID_LEN], b[SETUP_SSID_LEN];
    const uint8_t mac_a[6] = {0x24, 0x6F, 0x28, 0xAA, 0x4C, 0x21};
    const uint8_t mac_b[6] = {0x24, 0x6F, 0x28, 0xAA, 0x4C, 0x22};

    setup_ssid_from_mac(mac_a, a, sizeof(a));
    setup_ssid_from_mac(mac_b, b, sizeof(b));

    check_true("SSIDs differ", strcmp(a, b) != 0);
}

/* A short buffer must truncate safely rather than overflow. */
static void test_setup_ssid_respects_buffer(void)
{
    printf("setup SSID respects the output buffer\n");

    char small[4];
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAA, 0x4C, 0x21};
    setup_ssid_from_mac(mac, small, sizeof(small));

    check_true("stays within the buffer", strlen(small) < sizeof(small));
}

int main(void)
{
    printf("provisioning tests (spec 6.2 State C, 9.1)\n\n");

    test_accepts_normal_credentials();
    test_accepts_open_network();
    test_rejects_bad_ssid();
    test_rejects_bad_password();
    test_rejects_non_ascii_password();
    test_every_result_has_a_message();
    test_setup_ssid_format();
    test_setup_ssid_fits_802_11();
    test_setup_ssid_differs_by_mac();
    test_setup_ssid_respects_buffer();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
