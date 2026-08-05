#include "ft8_tx_message.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool ft8_is_grid_token(const char* token)
{
    size_t length = strlen(token);
    if (length != 4 && length != 6)
        return false;

    if (token[0] < 'A' || token[0] > 'R' ||
        token[1] < 'A' || token[1] > 'R' ||
        !isdigit((unsigned char)token[2]) ||
        !isdigit((unsigned char)token[3]))
        return false;

    return length == 4 ||
        ((token[4] >= 'A' && token[4] <= 'X') &&
         (token[5] >= 'A' && token[5] <= 'X'));
}

static int ft8_copy_upper_trimmed(char* destination, size_t destination_size,
    const char* source)
{
    if (destination == NULL || destination_size == 0 || source == NULL)
        return -1;

    while (isspace((unsigned char)*source))
        ++source;

    size_t length = strcspn(source, "\r\n");
    while (length > 0 && isspace((unsigned char)source[length - 1]))
        --length;

    if (length >= destination_size)
        return -1;

    for (size_t i = 0; i < length; ++i)
        destination[i] = (char)toupper((unsigned char)source[i]);
    destination[length] = '\0';
    return 0;
}

static bool ft8_is_bracketed(const char* callsign)
{
    size_t length = strlen(callsign);
    return length >= 2 && callsign[0] == '<' && callsign[length - 1] == '>';
}

static int ft8_copy_unbracketed(char* destination, size_t destination_size,
    const char* callsign)
{
    size_t length = strlen(callsign);
    size_t offset = 0;
    if (ft8_is_bracketed(callsign))
    {
        offset = 1;
        length -= 2;
    }

    if (length == 0 || length >= destination_size)
        return -1;

    memcpy(destination, callsign + offset, length);
    destination[length] = '\0';
    return 0;
}

bool ft8_callsign_requires_type4(const char* callsign)
{
    if (callsign == NULL || callsign[0] == '\0')
        return false;

    char bare_call[14];
    if (ft8_copy_unbracketed(bare_call, sizeof(bare_call), callsign) < 0)
        return false;

    char probe_text[FTX_MAX_MESSAGE_LENGTH];
    int length = snprintf(probe_text, sizeof(probe_text), "CQ %s", bare_call);
    if (length < 0 || (size_t)length >= sizeof(probe_text))
        return false;

    ftx_message_t probe;
    ftx_message_init(&probe);
    if (ftx_message_encode(&probe, NULL, probe_text) != FTX_MESSAGE_RC_OK)
        return false;

    return ftx_message_get_type(&probe) == FTX_MESSAGE_TYPE_NONSTD_CALL;
}

int ft8_format_initial_reply(char* destination, size_t destination_size,
    const char* dx_call, const char* own_call, const char* grid)
{
    if (destination == NULL || destination_size == 0 ||
        dx_call == NULL || own_call == NULL || grid == NULL)
        return -1;

    char dx_bare[14];
    char own_bare[14];
    if (ft8_copy_unbracketed(dx_bare, sizeof(dx_bare), dx_call) < 0 ||
        ft8_copy_unbracketed(own_bare, sizeof(own_bare), own_call) < 0)
        return -1;

    bool dx_nonstandard = ft8_callsign_requires_type4(dx_bare);
    bool own_nonstandard = ft8_callsign_requires_type4(own_bare);

    int length;
    if (own_nonstandard)
    {
        // Send the caller in full and hash the called station.  This also
        // gives the useful direction when both calls are nonstandard.
        length = snprintf(destination, destination_size, "<%s> %s",
            dx_bare, own_bare);
    }
    else if (dx_nonstandard)
    {
        // The nonstandard CQ caller must be sent in full; hash our standard
        // call in the second field to select the opposite type-4 direction.
        length = snprintf(destination, destination_size, "%s <%s>",
            dx_bare, own_bare);
    }
    else
    {
        length = snprintf(destination, destination_size, "%s %s %s",
            dx_bare, own_bare, grid);
    }

    return (length >= 0 && (size_t)length < destination_size) ? 0 : -1;
}

static void ft8_normalize_nonstandard_cq(char* message, size_t message_size)
{
    char parsed[FTX_MAX_MESSAGE_LENGTH];
    if (strlen(message) >= sizeof(parsed))
        return;
    strcpy(parsed, message);

    char* save = NULL;
    char* first = strtok_r(parsed, " ", &save);
    char* call = strtok_r(NULL, " ", &save);
    char* extra = strtok_r(NULL, " ", &save);
    char* fourth = strtok_r(NULL, " ", &save);

    if (first == NULL || call == NULL || extra == NULL || fourth != NULL ||
        strcmp(first, "CQ") != 0 || !ft8_is_grid_token(extra) ||
        !ft8_callsign_requires_type4(call))
        return;

    // Type 4 has no grid field.  Keep the displayed/queued text identical
    // to the payload that will actually be transmitted.
    int length = snprintf(message, message_size, "CQ %s", call);
    if (length < 0 || (size_t)length >= message_size)
        message[0] = '\0';
}

ftx_message_rc_t ft8_prepare_tx_message(ftx_message_t* packed,
    ftx_callsign_hash_interface_t* hash_if, const char* input,
    char* output, size_t output_size)
{
    if (packed == NULL || output == NULL || output_size == 0 || input == NULL)
        return FTX_MESSAGE_RC_ERROR_TYPE;

    output[0] = '\0';
    if (ft8_copy_upper_trimmed(output, output_size, input) < 0)
        return FTX_MESSAGE_RC_ERROR_TYPE;

    ft8_normalize_nonstandard_cq(output, output_size);
    if (output[0] == '\0')
        return FTX_MESSAGE_RC_ERROR_TYPE;

    ftx_message_init(packed);
    return ftx_message_encode(packed, hash_if, output);
}
