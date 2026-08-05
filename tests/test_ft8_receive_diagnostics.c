#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8/constants.h"
#include "ft8/decode.h"
#include "ft8/message.h"
#include "fft/kiss_fftr.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 12000
#define TIME_OSR 2
#define FREQ_OSR 2
#define MIN_SCORE 10
#define MAX_CANDIDATES 140
#define LDPC_ITERATIONS 25
#define FT8_SYMBOL_BT 2.0f
#define GFSK_CONST_K 5.336446f

typedef struct {
    const char *message;
    const char *payload_bits;
    const char *tones_text;
} reference_vector_t;

static const reference_vector_t reference_vectors[] = {
    {
        "CQ OK/SP5DAA",
        "11011011001000000000000110010100100101011001010100110011001000111101010001100",
        "3140652444300015611343615431735203223140652107203231303007374044717653173140652"
    },
    {
        "CQ OK/SP5DAA/P",
        "11011011001010001110101000011011101111011001010001110110001011100011000001100",
        "3140652444352630472443526537040212343140652457134237506773737420164346203140652"
    }
};

typedef struct {
    float symbol_period;
    int block_size;
    int subblock_size;
    int nfft;
    float fft_norm;
    float *window;
    float *last_frame;
    ftx_waterfall_t wf;
    float max_mag;
    void *fft_work;
    kiss_fftr_cfg fft_cfg;
} zbitx_monitor_t;

static void payload_from_bits(const char *bits, uint8_t payload[FTX_PAYLOAD_LENGTH_BYTES])
{
    memset(payload, 0, FTX_PAYLOAD_LENGTH_BYTES);
    for (unsigned i = 0; i < 77; ++i) {
        if (bits[i] == '1')
            payload[i / 8] |= (uint8_t)(1u << (7u - (i % 8u)));
    }
}

static bool tones_from_text(const char *text, uint8_t tones[FT8_NN])
{
    if (strlen(text) != FT8_NN)
        return false;
    for (unsigned i = 0; i < FT8_NN; ++i) {
        if (text[i] < '0' || text[i] > '7')
            return false;
        tones[i] = (uint8_t)(text[i] - '0');
    }
    return true;
}

static float hann_i(int i, int n)
{
    float x = sinf((float)M_PI * i / n);
    return x * x;
}

static bool zbitx_monitor_init(zbitx_monitor_t *mon, float initial_fill)
{
    memset(mon, 0, sizeof(*mon));
    mon->symbol_period = FT8_SYMBOL_PERIOD;
    mon->block_size = (int)(SAMPLE_RATE * FT8_SYMBOL_PERIOD);
    mon->subblock_size = mon->block_size / TIME_OSR;
    mon->nfft = mon->block_size * FREQ_OSR;
    mon->fft_norm = 2.0f / mon->nfft;

    mon->window = malloc((size_t)mon->nfft * sizeof(*mon->window));
    mon->last_frame = malloc((size_t)mon->nfft * sizeof(*mon->last_frame));
    if (mon->window == NULL || mon->last_frame == NULL)
        return false;

    for (int i = 0; i < mon->nfft; ++i) {
        mon->window[i] = hann_i(i, mon->nfft);
        mon->last_frame[i] = initial_fill;
    }

    size_t fft_work_size = 0;
    kiss_fftr_alloc(mon->nfft, 0, NULL, &fft_work_size);
    mon->fft_work = malloc(fft_work_size);
    if (mon->fft_work == NULL)
        return false;
    mon->fft_cfg = kiss_fftr_alloc(mon->nfft, 0, mon->fft_work, &fft_work_size);
    if (mon->fft_cfg == NULL)
        return false;

    mon->wf.max_blocks = (int)(FT8_SLOT_TIME / FT8_SYMBOL_PERIOD);
    mon->wf.num_blocks = 0;
    mon->wf.num_bins = (int)(SAMPLE_RATE * FT8_SYMBOL_PERIOD / 2);
    mon->wf.time_osr = TIME_OSR;
    mon->wf.freq_osr = FREQ_OSR;
    mon->wf.block_stride = TIME_OSR * FREQ_OSR * mon->wf.num_bins;
    mon->wf.protocol = FTX_PROTOCOL_FT8;
    size_t mag_size = (size_t)mon->wf.max_blocks * mon->wf.block_stride * sizeof(mon->wf.mag[0]);
    mon->wf.mag = malloc(mag_size);
    if (mon->wf.mag == NULL)
        return false;
    memset(mon->wf.mag, 0, mag_size);
    mon->max_mag = -120.0f;
    return true;
}

static void zbitx_monitor_free(zbitx_monitor_t *mon)
{
    free(mon->wf.mag);
    free(mon->fft_work);
    free(mon->last_frame);
    free(mon->window);
}

static void zbitx_monitor_process(zbitx_monitor_t *mon, const float *frame)
{
    if (mon->wf.num_blocks >= mon->wf.max_blocks)
        return;

    int offset = mon->wf.num_blocks * mon->wf.block_stride;
    int frame_pos = 0;
    for (int time_sub = 0; time_sub < mon->wf.time_osr; ++time_sub) {
        kiss_fft_scalar *timedata = malloc((size_t)mon->nfft * sizeof(*timedata));
        kiss_fft_cpx *freqdata = malloc((size_t)(mon->nfft / 2 + 1) * sizeof(*freqdata));
        if (timedata == NULL || freqdata == NULL) {
            fprintf(stderr, "allocation failure in monitor_process\n");
            exit(2);
        }

        memmove(mon->last_frame, mon->last_frame + mon->subblock_size,
            (size_t)(mon->nfft - mon->subblock_size) * sizeof(mon->last_frame[0]));
        for (int pos = mon->nfft - mon->subblock_size; pos < mon->nfft; ++pos)
            mon->last_frame[pos] = frame[frame_pos++];

        for (int pos = 0; pos < mon->nfft; ++pos)
            timedata[pos] = mon->fft_norm * mon->window[pos] * mon->last_frame[pos];

        kiss_fftr(mon->fft_cfg, timedata, freqdata);
        for (int freq_sub = 0; freq_sub < mon->wf.freq_osr; ++freq_sub) {
            for (int bin = 0; bin < mon->wf.num_bins; ++bin) {
                int src_bin = bin * mon->wf.freq_osr + freq_sub;
                float mag2 = freqdata[src_bin].i * freqdata[src_bin].i
                    + freqdata[src_bin].r * freqdata[src_bin].r;
                float db = 10.0f * log10f(1E-12f + mag2);
                int scaled = (int)(2 * db + 240);
                mon->wf.mag[offset++] = (scaled < 0) ? 0 : ((scaled > 255) ? 255 : (uint8_t)scaled);
                if (db > mon->max_mag)
                    mon->max_mag = db;
            }
        }
        free(freqdata);
        free(timedata);
    }
    ++mon->wf.num_blocks;
}

static void gfsk_pulse(int samples_per_symbol, float *pulse)
{
    for (int i = 0; i < 3 * samples_per_symbol; ++i) {
        float t = i / (float)samples_per_symbol - 1.5f;
        float arg1 = GFSK_CONST_K * FT8_SYMBOL_BT * (t + 0.5f);
        float arg2 = GFSK_CONST_K * FT8_SYMBOL_BT * (t - 0.5f);
        pulse[i] = (erff(arg1) - erff(arg2)) / 2;
    }
}

static void synth_gfsk(const uint8_t tones[FT8_NN], float frequency, float *signal)
{
    const int samples_per_symbol = (int)(0.5f + SAMPLE_RATE * FT8_SYMBOL_PERIOD);
    const int wave_samples = FT8_NN * samples_per_symbol;
    const int phase_samples = wave_samples + 2 * samples_per_symbol;
    float *dphi = malloc((size_t)phase_samples * sizeof(*dphi));
    float *pulse = malloc((size_t)(3 * samples_per_symbol) * sizeof(*pulse));
    if (dphi == NULL || pulse == NULL) {
        fprintf(stderr, "allocation failure in synth_gfsk\n");
        exit(2);
    }

    float dphi_peak = 2 * (float)M_PI / samples_per_symbol;
    for (int i = 0; i < phase_samples; ++i)
        dphi[i] = 2 * (float)M_PI * frequency / SAMPLE_RATE;

    gfsk_pulse(samples_per_symbol, pulse);
    for (int i = 0; i < FT8_NN; ++i) {
        int base = i * samples_per_symbol;
        for (int j = 0; j < 3 * samples_per_symbol; ++j)
            dphi[base + j] += dphi_peak * tones[i] * pulse[j];
    }
    for (int j = 0; j < 2 * samples_per_symbol; ++j) {
        dphi[j] += dphi_peak * pulse[j + samples_per_symbol] * tones[0];
        dphi[j + FT8_NN * samples_per_symbol] += dphi_peak * pulse[j] * tones[FT8_NN - 1];
    }

    float phase = 0;
    for (int i = 0; i < wave_samples; ++i) {
        signal[i] = sinf(phase);
        phase = fmodf(phase + dphi[i + samples_per_symbol], 2 * (float)M_PI);
    }
    int ramp = samples_per_symbol / 8;
    for (int i = 0; i < ramp; ++i) {
        float envelope = (1 - cosf(2 * (float)M_PI * i / (2 * ramp))) / 2;
        signal[i] *= envelope;
        signal[wave_samples - 1 - i] *= envelope;
    }

    free(pulse);
    free(dphi);
}

static int test_payload_decode(const reference_vector_t *vector)
{
    ftx_message_t message;
    ftx_message_init(&message);
    payload_from_bits(vector->payload_bits, message.payload);

    char text[FTX_MAX_MESSAGE_LENGTH];
    ftx_message_offsets_t offsets;
    ftx_message_rc_t rc = ftx_message_decode(&message, NULL, text, &offsets);
    if (rc != FTX_MESSAGE_RC_OK) {
        fprintf(stderr, "FAIL payload decode: %s rc=%d\n", vector->message, (int)rc);
        return 1;
    }
    if (ftx_message_get_type(&message) != FTX_MESSAGE_TYPE_NONSTD_CALL) {
        fprintf(stderr, "FAIL payload type: %s type=%d\n", vector->message,
            (int)ftx_message_get_type(&message));
        return 1;
    }
    if (strcmp(text, vector->message) != 0) {
        fprintf(stderr, "FAIL payload text: expected '%s', got '%s'\n", vector->message, text);
        return 1;
    }
    printf("PASS payload decode: %s\n", vector->message);
    return 0;
}

static int decode_reference_audio(const reference_vector_t *vector, float start_shift,
    float initial_fill, char decoded_text[FTX_MAX_MESSAGE_LENGTH])
{
    uint8_t tones[FT8_NN];
    if (!tones_from_text(vector->tones_text, tones))
        return -1;

    const int slot_samples = 14 * SAMPLE_RATE;
    const int wave_samples = (int)(0.5f + FT8_NN * FT8_SYMBOL_PERIOD * SAMPLE_RATE);
    const float nominal_start = (FT8_SLOT_TIME * SAMPLE_RATE - wave_samples) / (2.0f * SAMPLE_RATE);
    int start_sample = (int)lrintf((nominal_start + start_shift) * SAMPLE_RATE);
    if (start_sample < 0 || start_sample + wave_samples > slot_samples)
        return -2;

    float *audio = calloc((size_t)slot_samples, sizeof(*audio));
    if (audio == NULL)
        return -3;
    synth_gfsk(tones, 1500.0f, audio + start_sample);

    zbitx_monitor_t monitor;
    if (!zbitx_monitor_init(&monitor, initial_fill)) {
        free(audio);
        return -4;
    }
    for (int pos = 0; pos + monitor.block_size <= slot_samples; pos += monitor.block_size)
        zbitx_monitor_process(&monitor, audio + pos);

    ftx_candidate_t candidates[MAX_CANDIDATES];
    int candidate_count = ftx_find_candidates(&monitor.wf, MAX_CANDIDATES, candidates, MIN_SCORE);
    int channel_ok = 0;
    int unpack_failures = 0;
    int best_score = 0;
    bool found = false;
    for (int i = 0; i < candidate_count; ++i) {
        if (candidates[i].score > best_score)
            best_score = candidates[i].score;
        ftx_message_t message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(&monitor.wf, &candidates[i], LDPC_ITERATIONS, &message, &status))
            continue;
        ++channel_ok;
        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        ftx_message_rc_t rc = ftx_message_decode(&message, NULL, text, &offsets);
        if (rc != FTX_MESSAGE_RC_OK) {
            ++unpack_failures;
            continue;
        }
        if (strcmp(text, vector->message) == 0) {
            strcpy(decoded_text, text);
            found = true;
            break;
        }
    }

    printf("DIAG audio: message='%s' shift=%+.3fs initial=%+.3f blocks=%d max=%.1fdB candidates=%d best=%d channel_ok=%d unpack_fail=%d\n",
        vector->message, start_shift, initial_fill, monitor.wf.num_blocks, monitor.max_mag,
        candidate_count, best_score, channel_ok, unpack_failures);

    zbitx_monitor_free(&monitor);
    free(audio);
    return found ? 0 : 1;
}

static int test_audio_decode(const reference_vector_t *vector)
{
    static const float shifts[] = { -0.10f, 0.0f, 0.10f };
    unsigned failures = 0;
    for (size_t i = 0; i < sizeof(shifts) / sizeof(shifts[0]); ++i) {
        char text[FTX_MAX_MESSAGE_LENGTH] = { 0 };
        int rc = decode_reference_audio(vector, shifts[i], 0.0f, text);
        if (rc != 0) {
            fprintf(stderr, "FAIL audio decode: '%s' shift=%+.3fs rc=%d\n",
                vector->message, shifts[i], rc);
            ++failures;
        }
        else {
            printf("PASS audio decode: %s shift=%+.3fs\n", text, shifts[i]);
        }
    }

    /* The application currently allocates last_frame with malloc(). This deterministic
       non-zero fill checks whether startup garbage can affect decoding. */
    char text[FTX_MAX_MESSAGE_LENGTH] = { 0 };
    int rc = decode_reference_audio(vector, 0.0f, 0.25f, text);
    if (rc != 0) {
        fprintf(stderr, "WARN startup-frame sensitivity: '%s' failed with non-zero initial frame\n",
            vector->message);
    }
    else {
        printf("PASS non-zero startup frame: %s\n", text);
    }
    return (int)failures;
}

static void print_payload(const ftx_message_t *message)
{
    for (int i = 0; i < FTX_PAYLOAD_LENGTH_BYTES; ++i)
        printf("%02X", message->payload[i]);
}

static int decode_f32_file(const char *path)
{
    FILE *input = fopen(path, "rb");
    if (input == NULL) {
        perror(path);
        return 1;
    }
    if (fseek(input, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(input);
        return 1;
    }
    long byte_count = ftell(input);
    if (byte_count <= 0 || byte_count % (long)sizeof(float) != 0) {
        fprintf(stderr, "Invalid float32 file size for %s: %ld bytes\n", path, byte_count);
        fclose(input);
        return 1;
    }
    rewind(input);

    size_t sample_count = (size_t)byte_count / sizeof(float);
    float *audio = malloc(sample_count * sizeof(*audio));
    if (audio == NULL) {
        fprintf(stderr, "Cannot allocate %zu samples\n", sample_count);
        fclose(input);
        return 1;
    }
    if (fread(audio, sizeof(*audio), sample_count, input) != sample_count) {
        fprintf(stderr, "Short read from %s\n", path);
        free(audio);
        fclose(input);
        return 1;
    }
    fclose(input);

    double sum = 0.0;
    double sum_squares = 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < sample_count; ++i) {
        float value = audio[i];
        float absolute = fabsf(value);
        if (absolute > peak)
            peak = absolute;
        sum += value;
        sum_squares += (double)value * value;
    }
    double dc = sum / sample_count;
    double rms = sqrt(sum_squares / sample_count);

    zbitx_monitor_t monitor;
    if (!zbitx_monitor_init(&monitor, 0.0f)) {
        fprintf(stderr, "Cannot initialize monitor\n");
        free(audio);
        return 1;
    }
    for (size_t pos = 0; pos + (size_t)monitor.block_size <= sample_count;
         pos += (size_t)monitor.block_size)
        zbitx_monitor_process(&monitor, audio + pos);

    ftx_candidate_t candidates[MAX_CANDIDATES];
    int candidate_count = ftx_find_candidates(&monitor.wf, MAX_CANDIDATES, candidates, MIN_SCORE);
    int channel_ok = 0;
    int unpack_failures = 0;
    int decoded_count = 0;
    ftx_message_t decoded[64];
    int decoded_unique = 0;

    printf("F32 input: %s samples=%zu duration=%.3fs peak=%.6f rms=%.6f dc=%+.6f blocks=%d candidates=%d max=%.1fdB\n",
        path, sample_count, (double)sample_count / SAMPLE_RATE, peak, rms, dc,
        monitor.wf.num_blocks, candidate_count, monitor.max_mag);

    for (int i = 0; i < candidate_count; ++i) {
        ftx_message_t message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(&monitor.wf, &candidates[i], LDPC_ITERATIONS,
                &message, &status))
            continue;
        ++channel_ok;

        bool duplicate = false;
        for (int j = 0; j < decoded_unique; ++j) {
            if (memcmp(decoded[j].payload, message.payload, sizeof(message.payload)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (decoded_unique < (int)(sizeof(decoded) / sizeof(decoded[0])))
            decoded[decoded_unique++] = message;

        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        ftx_message_rc_t rc = ftx_message_decode(&message, NULL, text, &offsets);
        if (rc != FTX_MESSAGE_RC_OK) {
            ++unpack_failures;
            printf("F32 unpack failure: score=%d snr=%d rc=%d type=%d payload=",
                candidates[i].score, candidates[i].snr, (int)rc,
                (int)ftx_message_get_type(&message));
            print_payload(&message);
            putchar('\n');
            continue;
        }

        float frequency = (candidates[i].freq_offset
            + (float)candidates[i].freq_sub / monitor.wf.freq_osr) / FT8_SYMBOL_PERIOD;
        float time_offset = (candidates[i].time_offset
            + (float)candidates[i].time_sub / monitor.wf.time_osr) * FT8_SYMBOL_PERIOD;
        printf("F32 decoded: score=%d snr=%d time=%+.3fs freq=%.1fHz type=%d i3=%u n3=%u payload=",
            candidates[i].score, candidates[i].snr, time_offset, frequency,
            (int)ftx_message_get_type(&message),
            (unsigned)ftx_message_get_i3(&message),
            (unsigned)ftx_message_get_n3(&message));
        print_payload(&message);
        printf(" text='%s'\n", text);
        ++decoded_count;
    }

    printf("F32 summary: candidates=%d channel_ok=%d unpack_fail=%d decoded=%d\n",
        candidate_count, channel_ok, unpack_failures, decoded_count);

    zbitx_monitor_free(&monitor);
    free(audio);
    return decoded_count > 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        unsigned failures = 0;
        for (int i = 1; i < argc; ++i)
            failures += (unsigned)decode_f32_file(argv[i]);
        return failures ? 1 : 0;
    }

    unsigned failures = 0;
    for (size_t i = 0; i < sizeof(reference_vectors) / sizeof(reference_vectors[0]); ++i) {
        failures += (unsigned)test_payload_decode(&reference_vectors[i]);
        failures += (unsigned)test_audio_decode(&reference_vectors[i]);
    }
    printf("FT8 receive diagnostics: %zu WSJT-X type-4 vectors, %u failures\n",
        sizeof(reference_vectors) / sizeof(reference_vectors[0]), failures);
    return failures ? 1 : 0;
}
