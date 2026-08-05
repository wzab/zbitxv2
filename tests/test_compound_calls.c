#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8/constants.h"
#include "ft8/encode.h"
#include "ft8/message.h"


#define HASH_CACHE_SIZE 32

struct hash_entry
{
    char callsign[12];
    uint32_t n22;
};

static struct hash_entry hash_cache[HASH_CACHE_SIZE];
static size_t hash_cache_count;

static void hash_cache_reset(void)
{
    memset(hash_cache, 0, sizeof(hash_cache));
    hash_cache_count = 0;
}

static void hash_cache_save(const char* callsign, uint32_t n22)
{
    for (size_t i = 0; i < hash_cache_count; ++i)
    {
        if (hash_cache[i].n22 == n22 && strcmp(hash_cache[i].callsign, callsign) == 0)
            return;
    }
    if (hash_cache_count >= HASH_CACHE_SIZE)
        return;
    snprintf(hash_cache[hash_cache_count].callsign,
        sizeof(hash_cache[hash_cache_count].callsign), "%s", callsign);
    hash_cache[hash_cache_count].n22 = n22;
    ++hash_cache_count;
}

static bool hash_cache_lookup(ftx_callsign_hash_type_t type, uint32_t hash, char* callsign)
{
    const struct hash_entry* match = NULL;
    for (size_t i = 0; i < hash_cache_count; ++i)
    {
        uint32_t candidate;
        if (type == FTX_CALLSIGN_HASH_22_BITS)
            candidate = hash_cache[i].n22;
        else if (type == FTX_CALLSIGN_HASH_12_BITS)
            candidate = hash_cache[i].n22 >> 10;
        else
            candidate = hash_cache[i].n22 >> 12;

        if (candidate != hash)
            continue;
        if (match != NULL && strcmp(match->callsign, hash_cache[i].callsign) != 0)
            return false; // ambiguous shortened hash
        match = &hash_cache[i];
    }
    if (match == NULL)
        return false;
    strcpy(callsign, match->callsign);
    return true;
}

static ftx_callsign_hash_interface_t hash_interface = {
    .lookup_hash = hash_cache_lookup,
    .save_hash = hash_cache_save,
};

static int check_boundary_cases(void)
{
    unsigned failures = 0;
    ftx_message_t message;

    ftx_message_init(&message);
    if (ftx_message_encode_nonstd(&message, &hash_interface,
            "SP5DAA", "XY/TE5THF/R", "AB09") != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL: valid 11-character callsign was rejected\n");
        ++failures;
    }

    ftx_message_init(&message);
    if (ftx_message_encode_nonstd(&message, &hash_interface,
            "SP5DAA", "XY/TE5THF/RR", "AB09") == FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL: 12-character callsign was silently accepted\n");
        ++failures;
    }

    ftx_message_init(&message);
    if (ftx_message_encode_nonstd(&message, &hash_interface,
            "SP5DAA", "<XY/TE5THF>", "AB09") != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL: valid bracketed hashed callsign was rejected\n");
        ++failures;
    }

    ftx_message_init(&message);
    if (ftx_message_encode_nonstd(&message, &hash_interface,
            "SP5DAA", "<XY/TE5THF", "AB09") == FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL: unterminated bracketed callsign was accepted\n");
        ++failures;
    }

    hash_cache_reset();
    ftx_message_init(&message);
    if (ftx_message_encode(&message, &hash_interface,
            "SP5DAA XY/TE5THF AB09") != FTX_MESSAGE_RC_OK)
    {
        fprintf(stderr, "FAIL: cache round-trip message could not be encoded\n");
        ++failures;
    }
    else
    {
        char decoded[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        if (ftx_message_decode(&message, &hash_interface, decoded, &offsets) != FTX_MESSAGE_RC_OK
            || strstr(decoded, "SP5DAA") == NULL
            || strstr(decoded, "XY/TE5THF") == NULL
            || strstr(decoded, "<...>") != NULL)
        {
            fprintf(stderr, "FAIL: cache round trip decoded as '%s'\n", decoded);
            ++failures;
        }
    }

    if (failures == 0)
        printf("PASS boundary, bracket and hash-cache tests\n");
    return (int)failures;
}

static void trim(char* text)
{
    char* start = text;
    while (isspace((unsigned char)*start))
        ++start;
    if (start != text)
        memmove(text, start, strlen(start) + 1);

    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        text[--length] = '\0';
}

static const char* type_name(ftx_message_type_t type)
{
    switch (type)
    {
    case FTX_MESSAGE_TYPE_FREE_TEXT: return "free-text";
    case FTX_MESSAGE_TYPE_DXPEDITION: return "DXpedition";
    case FTX_MESSAGE_TYPE_EU_VHF: return "EU-VHF";
    case FTX_MESSAGE_TYPE_ARRL_FD: return "ARRL-FD";
    case FTX_MESSAGE_TYPE_TELEMETRY: return "telemetry";
    case FTX_MESSAGE_TYPE_CONTESTING: return "contesting";
    case FTX_MESSAGE_TYPE_STANDARD: return "standard";
    case FTX_MESSAGE_TYPE_ARRL_RTTY: return "ARRL-RTTY";
    case FTX_MESSAGE_TYPE_NONSTD_CALL: return "nonstandard-call";
    case FTX_MESSAGE_TYPE_WWROF: return "WWROF";
    default: return "unknown";
    }
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "compound_calls_regression.txt";
    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        perror(path);
        return 2;
    }

    char line[256];
    unsigned line_number = 0;
    unsigned tested = 0;
    unsigned failures = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        ++line_number;
        trim(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        ftx_message_t first;
        ftx_message_t second;
        ftx_message_init(&first);
        ftx_message_init(&second);

        ftx_message_rc_t rc_first = ftx_message_encode(&first, NULL, line);
        ftx_message_rc_t rc_second = ftx_message_encode(&second, NULL, line);
        ++tested;

        if (rc_first != FTX_MESSAGE_RC_OK || rc_second != FTX_MESSAGE_RC_OK)
        {
            fprintf(stderr, "FAIL line %u: encode rc=%d/%d: %s\n",
                line_number, (int)rc_first, (int)rc_second, line);
            ++failures;
            continue;
        }

        if (memcmp(first.payload, second.payload, FTX_PAYLOAD_LENGTH_BYTES) != 0)
        {
            fprintf(stderr, "FAIL line %u: non-deterministic payload: %s\n",
                line_number, line);
            ++failures;
            continue;
        }

        ftx_message_type_t type = ftx_message_get_type(&first);
        if (type == FTX_MESSAGE_TYPE_FREE_TEXT || type == FTX_MESSAGE_TYPE_UNKNOWN)
        {
            fprintf(stderr, "FAIL line %u: unexpected type %s: %s\n",
                line_number, type_name(type), line);
            ++failures;
            continue;
        }

        uint8_t tones_first[FT8_NN] = { 0 };
        uint8_t tones_second[FT8_NN] = { 0 };
        ft8_encode(first.payload, tones_first);
        ft8_encode(second.payload, tones_second);

        if (memcmp(tones_first, tones_second, sizeof(tones_first)) != 0)
        {
            fprintf(stderr, "FAIL line %u: non-deterministic tones: %s\n",
                line_number, line);
            ++failures;
            continue;
        }

        int invalid_tone = 0;
        for (unsigned i = 0; i < FT8_NN; ++i)
        {
            if (tones_first[i] > 7)
            {
                fprintf(stderr, "FAIL line %u: invalid tone %u at symbol %u: %s\n",
                    line_number, tones_first[i], i, line);
                ++failures;
                invalid_tone = 1;
                break;
            }
        }
        if (invalid_tone)
            continue;

        printf("PASS line %u: type=%s i3=%u n3=%u  %s\n",
            line_number, type_name(type),
            ftx_message_get_i3(&first), ftx_message_get_n3(&first), line);
    }

    fclose(file);
    failures += (unsigned)check_boundary_cases();
    printf("%u vectors tested, %u failures\n", tested, failures);
    return failures ? 1 : 0;
}
