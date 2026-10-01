/* Original ChirpMotion native CLI, MIT. */
#include "chirpmotion.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *file) {
    fputs("Usage: chirpmotion-native --input FILE --output NEW_DIRECTORY [options]\n"
          "  --mode up|alternating     successive 961-sample sweeps (default up)\n"
          "  --channel 1|2            required for stereo PCM16 WAVE\n"
          "  --start-sample N         one-based known start; otherwise global alignment\n"
          "  --fft-length N           power of two, 1024..65536 (default 16384)\n"
          "  --spectra-csv            also stream full 0..1500 Hz spectra\n"
          "  --max-output-bytes N     total CSV/JSON cap, 1..536870912\n"
          "Input: seekable mono PCM16LE or classic PCM16 WAVE, 48000 Hz, <=1 GiB.\n"
          "Results describe spectra and relative acoustic paths, not calibrated object distances.\n",file);
}
static int integer(const char *text,uint64_t *value) {
    if (!text || !*text) return -1;
    for (const char *p=text;*p;++p) if (*p<'0' || *p>'9') return -1;
    errno=0; char *end; unsigned long long parsed=strtoull(text,&end,10);
    if (errno==ERANGE || *end || parsed>UINT64_MAX) return -1;
    *value=(uint64_t)parsed; return 0;
}
int main(int argc,char **argv) {
    CMOptions options=cm_default_options(); const char *input=NULL,*output=NULL;
    unsigned seen=0;
    for (int i=1;i<argc;++i) {
        const char *flag=argv[i]; unsigned bit=0;
        if (strcmp(flag,"--help")==0) { if (argc!=2) goto invalid; usage(stdout); return 0; }
        if (strcmp(flag,"--input")==0) bit=1;
        else if (strcmp(flag,"--output")==0) bit=2;
        else if (strcmp(flag,"--mode")==0) bit=4;
        else if (strcmp(flag,"--channel")==0) bit=8;
        else if (strcmp(flag,"--start-sample")==0) bit=16;
        else if (strcmp(flag,"--fft-length")==0) bit=32;
        else if (strcmp(flag,"--spectra-csv")==0) bit=64;
        else if (strcmp(flag,"--max-output-bytes")==0) bit=128;
        else goto invalid;
        if (seen & bit) goto invalid;
        seen|=bit;
        if (bit==64) { options.spectra=1; continue; }
        if (++i==argc) goto invalid;
        const char *value=argv[i]; uint64_t number=0;
        if (bit==1) input=value;
        else if (bit==2) output=value;
        else if (bit==4) {
            if (strcmp(value,"up")==0) options.alternating=0;
            else if (strcmp(value,"alternating")==0) options.alternating=1;
            else goto invalid;
        } else {
            if (integer(value,&number)!=0) goto invalid;
            if (bit==8) { if (number<1 || number>2) goto invalid; options.channel=(unsigned)number; }
            else if (bit==16) { if (!number) goto invalid; options.start_sample=number; }
            else if (bit==32) { if (number>65536) goto invalid; options.fft_length=(size_t)number; }
            else options.max_output_bytes=number;
        }
    }
    if (!input || !*input || !output || !*output) goto invalid;
    CMError error={{0}}; CMReport report;
    if (cm_analyze(input,output,&options,&report,&error)!=0) {
        fprintf(stderr,"error: %s\n",error.message); return 1;
    }
    if (printf("Analyzed %llu cycles; start sample %llu; processing heap peak %zu bytes.\n",
               (unsigned long long)report.cycles,(unsigned long long)report.start_sample,
               report.peak_heap_bytes)<0 || fflush(stdout)!=0) return 1;
    return 0;
invalid:
    fputs("error: missing, repeated or invalid option\n",stderr); usage(stderr); return 2;
}
