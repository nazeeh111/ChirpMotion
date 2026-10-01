/* Original ChirpMotion native implementation, MIT. */
#ifndef CHIRPMOTION_H
#define CHIRPMOTION_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CM_RATE 48000U
#define CM_SWEEP 961U
#define CM_INPUT_LIMIT UINT64_C(1073741824)
#define CM_OUTPUT_LIMIT UINT64_C(536870912)
#define CM_PI 3.14159265358979323846264338327950288

typedef struct { char message[256]; } CMError;
typedef struct { size_t live, peak; } CMHeap;
typedef struct { double real, imag; } CMComplex;
typedef struct {
    FILE *file;
    uint64_t data_offset, data_bytes, frames, position;
    unsigned channels, channel;
    int wave;
} CMAudio;
typedef struct {
    size_t fft_length;
    unsigned channel;
    int alternating, spectra;
    uint64_t start_sample, max_output_bytes;
} CMOptions;
typedef struct {
    uint64_t input_frames, start_sample, prefix_samples, suffix_samples, cycles;
    unsigned input_channels, selected_channel, sweep_channels;
    int wave;
    double alignment_score;
    size_t peak_heap_bytes;
} CMReport;

void cm_error(CMError *error, const char *format, ...);
void *cm_alloc(CMHeap *heap, size_t count, size_t size, CMError *error);
void cm_free(void *pointer);
int cm_fft(CMComplex *values, size_t length, int inverse, CMError *error);
void cm_template(double *values, int alternating);
int cm_audio_open(CMAudio *audio, const char *path, unsigned channel, CMError *error);
int cm_audio_seek(CMAudio *audio, uint64_t frame, CMError *error);
int cm_audio_read(CMAudio *audio, double *value, CMError *error);
int cm_audio_close(CMAudio *audio, CMError *error);
CMOptions cm_default_options(void);
int cm_analyze(const char *input, const char *output, const CMOptions *options,
               CMReport *report, CMError *error);
#endif
