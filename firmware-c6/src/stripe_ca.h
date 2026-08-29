/*
 * Root CA for api.stripe.com: DigiCert Assured ID Root G2.
 *
 * Pinned so the device refuses to talk to anything that cannot prove it is
 * Stripe. Without this the connection is encrypted but unauthenticated:
 * anyone controlling DNS on the network the device has joined -- a router, a
 * cafe AP, a compromised machine -- could answer for api.stripe.com with
 * their own certificate, collect the restricted key out of the Authorization
 * header, and then choose what revenue figure the display shows. A silently
 * wrong number is the one failure this device must not have, since its whole
 * premise is a figure trusted at a glance without checking.
 *
 * WHICH certificate this is, and why it matters:
 *
 * Stripe's server presents a three-certificate chain ending in a
 * CROSS-SIGNED copy of Assured ID Root G2, issued by DigiCert High Assurance
 * EV Root CA and expiring 2031-11-09. This is NOT that copy. This is the
 * genuine self-signed root from the system trust store, valid to
 * 2038-01-15. Pinning the cross-signed copy would strand the device when
 * that cross-sign expires, several years before it had to.
 *
 * Verified before committing: `openssl s_client -connect api.stripe.com:443
 * -CAfile <this cert>` returns "Verify return code: 0 (ok)", so the live
 * chain validates against this root alone.
 *
 * WHEN THIS EXPIRES (2038-01-15) OR STRIPE CHANGES CA, THE DEVICE STOPS
 * FETCHING and every screen goes stale. That is the correct failure -- it
 * refuses rather than trusting an unknown issuer -- but it is a scheduled
 * outage, so the date belongs in whatever calendar outlives this code.
 * Regenerate with:
 *   security find-certificate -a -c "DigiCert Assured ID Root G2" -p \
 *     /System/Library/Keychains/SystemRootCertificates.keychain
 */
#pragma once

static const char STRIPE_ROOT_CA[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDljCCAn6gAwIBAgIQC5McOtY5Z+pnI7/Dr5r0SzANBgkqhkiG9w0BAQsFADBl\n"
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
"d3cuZGlnaWNlcnQuY29tMSQwIgYDVQQDExtEaWdpQ2VydCBBc3N1cmVkIElEIFJv\n"
"b3QgRzIwHhcNMTMwODAxMTIwMDAwWhcNMzgwMTE1MTIwMDAwWjBlMQswCQYDVQQG\n"
"EwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3d3cuZGlnaWNl\n"
"cnQuY29tMSQwIgYDVQQDExtEaWdpQ2VydCBBc3N1cmVkIElEIFJvb3QgRzIwggEi\n"
"MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDZ5ygvUj82ckmIkzTz+GoeMVSA\n"
"n61UQbVH35ao1K+ALbkKz3X9iaV9JPrjIgwrvJUXCzO/GU1BBpAAvQxNEP4Htecc\n"
"biJVMWWXvdMX0h5i89vqbFCMP4QMls+3ywPgym2hFEwbid3tALBSfK+RbLE4E9Hp\n"
"EgjAALAcKxHad3A2m67OeYfcgnDmCXRwVWmvo2ifv922ebPynXApVfSr/5Vh88lA\n"
"bx3RvpO704gqu52/clpWcTs/1PPRCv4o76Pu2ZmvA9OPYLfykqGxvYmJHzDNw6Yu\n"
"YjOuFgJ3RFrngQo8p0Quebg/BLxcoIfhG69Rjs3sLPr4/m3wOnyqi+RnlTGNAgMB\n"
"AAGjQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdDgQW\n"
"BBTOw0q5mVXyuNtgv6l+vVa1lzan1jANBgkqhkiG9w0BAQsFAAOCAQEAyqVVjOPI\n"
"QW5pJ6d1Ee88hjZv0p3GeDgdaZaikmkuOGybfQTUiaWxMTeKySHMq2zNixya1r9I\n"
"0jJmwYrA8y8678Dj1JGG0VDjA9tzd29KOVPt3ibHtX2vK0LRdWLjSisCx1BL4Gni\n"
"lmwORGYQRI+tBev4eaymG+g3NJ1TyWGqolKvSnAWhsI6yLETcDbYz+70CjTVW0z9\n"
"B5yiutkBclzzTcHdDrEcDcRjvq30FPuJ7KJBDkzMyFdA0G4Dqs0MjomZmWzwPDCv\n"
"ON9vvKO+KSAnq3T/EyJ43pdSVR6DtVQgA+6uwE9W3jfMw3+qBCe703e4YtsXfJwo\n"
"IhNzbM8m9Yop5w==\n"
"-----END CERTIFICATE-----\n";
