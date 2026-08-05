#ifndef FT8_TX_MESSAGE_H
#define FT8_TX_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "ft8_lib/ft8/message.h"

bool ft8_callsign_requires_type4(const char* callsign);

int ft8_format_initial_reply(char* destination, size_t destination_size,
    const char* dx_call, const char* own_call, const char* grid);

ftx_message_rc_t ft8_prepare_tx_message(ftx_message_t* packed,
    ftx_callsign_hash_interface_t* hash_if, const char* input,
    char* output, size_t output_size);

#endif
