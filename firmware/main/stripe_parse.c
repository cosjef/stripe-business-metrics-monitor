/*
 * Parse Stripe subscription list responses. See stripe_parse.h.
 */
#include "stripe_parse.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

/* Read an integer that Stripe may omit or send as null. */
static int64_t json_int(const cJSON *obj, const char *key, int64_t fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v)) {
        return fallback;
    }
    return (int64_t)v->valuedouble;
}

static const char *json_str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/*
 * Spec 7.2 considers only active and trialing subscriptions. Canceled,
 * past_due and incomplete still appear in list responses and would inflate
 * the total if counted.
 */
static bool status_counts(const char *status, bool *is_trialing)
{
    if (status == NULL) {
        return false;
    }
    if (strcmp(status, "active") == 0) {
        *is_trialing = false;
        return true;
    }
    if (strcmp(status, "trialing") == 0) {
        *is_trialing = true;
        return true;
    }
    return false;
}

static void parse_discount(const cJSON *sub, mrr_discount_t *out)
{
    memset(out, 0, sizeof(*out));

    const cJSON *discount = cJSON_GetObjectItemCaseSensitive(sub, "discount");
    if (!cJSON_IsObject(discount)) {
        return;
    }

    const cJSON *coupon = cJSON_GetObjectItemCaseSensitive(discount, "coupon");
    if (!cJSON_IsObject(coupon)) {
        return;
    }

    const cJSON *pct = cJSON_GetObjectItemCaseSensitive(coupon, "percent_off");
    if (cJSON_IsNumber(pct)) {
        /* Stripe sends a float like 33.33. Scale to hundredths of a percent so
         * fractional coupons survive as integers. Round to nearest to avoid
         * losing a hundredth to truncation. */
        out->percent_off_x100 = (int32_t)(pct->valuedouble * 100.0 + 0.5);
        out->present = true;
    }

    const cJSON *amt = cJSON_GetObjectItemCaseSensitive(coupon, "amount_off");
    if (cJSON_IsNumber(amt)) {
        out->amount_off = (int64_t)amt->valuedouble;
        out->present = true;
    }
}

static bool parse_item(const cJSON *item_json, mrr_item_t *out)
{
    memset(out, 0, sizeof(*out));

    const cJSON *price = cJSON_GetObjectItemCaseSensitive(item_json, "price");
    if (!cJSON_IsObject(price)) {
        return false;
    }

    /* Absent quantity means 1, which is what Stripe implies for single-seat
     * subscriptions. */
    out->quantity = json_int(item_json, "quantity", 1);
    out->unit_amount = json_int(price, "unit_amount", 0);

    const char *currency = json_str(price, "currency");
    if (currency) {
        strncpy(out->currency, currency, MRR_CURRENCY_LEN - 1);
    }

    /* Tiered prices have no usable unit_amount; flag rather than guess
     * (spec 7.2 step 3). */
    const char *scheme = json_str(price, "billing_scheme");
    out->tiered = scheme != NULL && strcmp(scheme, "tiered") == 0;

    /* A one-time price has no recurring object at all. */
    const cJSON *rec = cJSON_GetObjectItemCaseSensitive(price, "recurring");
    if (cJSON_IsObject(rec)) {
        out->recurring = true;
        out->interval = mrr_interval_from_str(json_str(rec, "interval"));
        out->interval_count = (int32_t)json_int(rec, "interval_count", 1);
    } else {
        out->recurring = false;
        out->interval_count = 1;
    }

    return true;
}

bool stripe_parse_subscriptions(const char *json, stripe_subs_t *out)
{
    if (json == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }

    const cJSON *has_more = cJSON_GetObjectItemCaseSensitive(root, "has_more");
    out->has_more = cJSON_IsTrue(has_more);

    /* A Stripe error response is valid JSON with no data array, so this also
     * rejects error bodies rather than reporting an empty account. */
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *sub_json = NULL;
    cJSON_ArrayForEach(sub_json, data) {
        if (out->sub_count >= STRIPE_MAX_SUBS) {
            out->truncated = true;
            break;
        }

        /*
         * Record the cursor before the status filter, so a page ending in a
         * cancelled subscription still advances. Rejected rather than
         * truncated if it somehow will not fit -- see STRIPE_ID_LEN.
         */
        const char *id = json_str(sub_json, "id");
        if (id != NULL && strlen(id) < sizeof(out->last_id)) {
            snprintf(out->last_id, sizeof(out->last_id), "%s", id);
        }

        bool trialing = false;
        if (!status_counts(json_str(sub_json, "status"), &trialing)) {
            continue;
        }

        mrr_subscription_t *sub = &out->subs[out->sub_count];
        memset(sub, 0, sizeof(*sub));
        sub->trialing = trialing;
        parse_discount(sub_json, &sub->discount);

        /* Items live at items.data, not items directly. */
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(sub_json, "items");
        const cJSON *items_data =
            cJSON_IsObject(items)
                ? cJSON_GetObjectItemCaseSensitive(items, "data")
                : NULL;

        sub->items = &out->items[out->item_count];
        sub->item_count = 0;

        if (cJSON_IsArray(items_data)) {
            const cJSON *item_json = NULL;
            cJSON_ArrayForEach(item_json, items_data) {
                if (out->item_count >= STRIPE_MAX_ITEMS) {
                    out->truncated = true;
                    break;
                }
                if (parse_item(item_json, &out->items[out->item_count])) {
                    out->item_count++;
                    sub->item_count++;
                }
            }
        }

        out->sub_count++;
    }

    cJSON_Delete(root);
    return true;
}
