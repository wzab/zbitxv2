#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ft8_tx_message.h"

static int failures;

#define HASH_CACHE_SIZE 8

typedef struct
{
    char callsign[12];
    uint32_t hash22;
} hash_entry_t;

static hash_entry_t hash_cache[HASH_CACHE_SIZE];
static size_t hash_cache_size;

static void clear_hash_cache(void)
{
    memset(hash_cache, 0, sizeof(hash_cache));
    hash_cache_size = 0;
}

static void save_hash(const char* callsign, uint32_t hash22)
{
    for (size_t i = 0; i < hash_cache_size; ++i)
    {
        if (hash_cache[i].hash22 == hash22 &&
            strcmp(hash_cache[i].callsign, callsign) == 0)
            return;
    }

    if (hash_cache_size >= HASH_CACHE_SIZE)
        return;

    snprintf(hash_cache[hash_cache_size].callsign,
        sizeof(hash_cache[hash_cache_size].callsign), "%s", callsign);
    hash_cache[hash_cache_size].hash22 = hash22;
    ++hash_cache_size;
}

static bool lookup_hash(ftx_callsign_hash_type_t hash_type, uint32_t hash,
    char* callsign)
{
    unsigned int shift = 0;
    if (hash_type == FTX_CALLSIGN_HASH_12_BITS)
        shift = 10;
    else if (hash_type == FTX_CALLSIGN_HASH_10_BITS)
        shift = 12;

    for (size_t i = 0; i < hash_cache_size; ++i)
    {
        if ((hash_cache[i].hash22 >> shift) == hash)
        {
            strcpy(callsign, hash_cache[i].callsign);
            return true;
        }
    }
    callsign[0] = '\0';
    return false;
}

static ftx_callsign_hash_interface_t hash_if = {
    .lookup_hash = lookup_hash,
    .save_hash = save_hash,
};

static void expect_prepare(const char* input, const char* expected, int expected_i3)
{
    clear_hash_cache();
    ftx_message_t message;
    char prepared[FTX_MAX_MESSAGE_LENGTH];
    ftx_message_rc_t rc = ft8_prepare_tx_message(&message, &hash_if, input,
        prepared, sizeof(prepared));

    if (rc != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL prepare '%s': rc=%d\n", input, (int)rc);
        ++failures;
        return;
    }
    if (strcmp(prepared, expected) != 0)
    {
        fprintf(stderr, "FAIL prepare '%s': got '%s', expected '%s'\n",
            input, prepared, expected);
        ++failures;
        return;
    }
    if (ftx_message_get_i3(&message) != expected_i3)
    {
        fprintf(stderr, "FAIL prepare '%s': i3=%d, expected %d\n",
            input, ftx_message_get_i3(&message), expected_i3);
        ++failures;
        return;
    }

    printf("PASS prepare: %s -> %s (i3=%d)\n", input, prepared, expected_i3);
}

static void expect_reply(const char* dx_call, const char* own_call,
    const char* grid, const char* expected, int expected_i3)
{
    char reply[FTX_MAX_MESSAGE_LENGTH];
    if (ft8_format_initial_reply(reply, sizeof(reply), dx_call, own_call, grid) < 0)
    {
        fprintf(stderr, "FAIL reply %s / %s: formatting failed\n",
            dx_call, own_call);
        ++failures;
        return;
    }

    if (strcmp(reply, expected) != 0)
    {
        fprintf(stderr, "FAIL reply %s / %s: got '%s', expected '%s'\n",
            dx_call, own_call, reply, expected);
        ++failures;
        return;
    }

    clear_hash_cache();
    ftx_message_t message;
    ftx_message_init(&message);
    ftx_message_rc_t rc = ftx_message_encode(&message, &hash_if, reply);
    if (rc != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL reply '%s': rc=%d\n", reply, (int)rc);
        ++failures;
        return;
    }
    if (ftx_message_get_i3(&message) != expected_i3)
    {
        fprintf(stderr, "FAIL reply '%s': i3=%d, expected %d\n",
            reply, ftx_message_get_i3(&message), expected_i3);
        ++failures;
        return;
    }

    char decoded[FTX_MAX_MESSAGE_LENGTH];
    ftx_message_offsets_t offsets;
    rc = ftx_message_decode(&message, &hash_if, decoded, &offsets);
    if (rc != FTX_MESSAGE_RC_OK || strcmp(decoded, expected) != 0)
    {
        fprintf(stderr, "FAIL reply '%s': decoded rc=%d text='%s'\n",
            reply, (int)rc, rc == FTX_MESSAGE_RC_OK ? decoded : "");
        ++failures;
        return;
    }

    printf("PASS initial reply: %s (i3=%d)\n", reply, expected_i3);
}

static void expect_failure(const char* input)
{
    clear_hash_cache();
    ftx_message_t message;
    char prepared[FTX_MAX_MESSAGE_LENGTH];
    ftx_message_rc_t rc = ft8_prepare_tx_message(&message, &hash_if, input,
        prepared, sizeof(prepared));
    if (rc == FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL expected rejection: '%s' encoded as '%s'\n",
            input, prepared);
        ++failures;
        return;
    }
    printf("PASS rejected before scheduling: %s (rc=%d)\n", input, (int)rc);
}

int main(void)
{
    expect_prepare("CQ SP5DAA KO02", "CQ SP5DAA KO02", 1);
    expect_prepare("CQ DL/RT3REW AB09", "CQ DL/RT3REW", 4);
    expect_prepare("cq dl/rt3rew/p ab09", "CQ DL/RT3REW/P", 4);
    expect_prepare("<DL/RT3REW> SP5DAA +11",
        "<DL/RT3REW> SP5DAA +11", 1);

    expect_reply("SP5DAA", "RT3REW", "KO02",
        "SP5DAA RT3REW KO02", 1);
    expect_reply("SP5DAA", "DL/RT3REW", "AB09",
        "<SP5DAA> DL/RT3REW", 4);
    expect_reply("OK/SP5DAA", "RT3REW", "KO02",
        "OK/SP5DAA <RT3REW>", 4);
    expect_reply("OK/SP5DAA", "DL/RT3REW/P", "AB09",
        "<OK/SP5DAA> DL/RT3REW/P", 4);

    expect_failure("THIS MESSAGE CANNOT BE ENCODED AS AN FT8 MESSAGE");

    ftx_message_t raw_cq;
    ftx_message_init(&raw_cq);
    if (ftx_message_encode(&raw_cq, &hash_if,
            "CQ DL/RT3REW AB09") == FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL type 4 silently discarded a CQ locator\n");
        ++failures;
    }
    else
    {
        printf("PASS type 4 rejects an unnormalized CQ locator\n");
    }

    ftx_message_t invalid_type4;
    ftx_message_init(&invalid_type4);
    if (ftx_message_encode_nonstd(&invalid_type4, NULL,
            "SP5DAA", "DL/RT3REW", "+11") == FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL type 4 silently discarded a numeric report\n");
        ++failures;
    }
    else
    {
        printf("PASS type 4 rejects unsupported numeric reports\n");
    }

    printf("FT8 TX message regression: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
