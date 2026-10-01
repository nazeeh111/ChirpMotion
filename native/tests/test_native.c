#define _POSIX_C_SOURCE 200809L
#include "chirpmotion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void require(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL %s\n", message); exit(1); }
}
static void test_fft(void) {
    CMComplex input[16], actual[16]; CMError error = {{0}};
    for (size_t i=0; i<16; ++i) {
        input[i].real = (double)(i%5)-2.0;
        input[i].imag = (double)(i%3)*0.25;
        actual[i]=input[i];
    }
    require(cm_fft(actual,16,0,&error)==0,"FFT accepts radix-two input");
    for (size_t k=0; k<16; ++k) {
        double real=0, imag=0;
        for (size_t j=0; j<16; ++j) {
            double angle=-2*CM_PI*(double)j*(double)k/16;
            real+=input[j].real*cos(angle)-input[j].imag*sin(angle);
            imag+=input[j].real*sin(angle)+input[j].imag*cos(angle);
        }
        require(fabs(real-actual[k].real)<1e-10 && fabs(imag-actual[k].imag)<1e-10,
                "FFT agrees with independent direct DFT");
    }
    require(cm_fft(actual,16,1,&error)==0,"inverse FFT accepts input");
    for (size_t k=0; k<16; ++k)
        require(fabs(actual[k].real-input[k].real)<1e-12 &&
                fabs(actual[k].imag-input[k].imag)<1e-12,"FFT round trip");
    require(cm_fft(actual,15,0,&error)!=0,"non-radix-two FFT rejected");
    puts("PASS FFT direct-DFT and inverse checks");
}
static void le16(FILE *file, unsigned value) {
    require(fputc((int)(value&255),file)!=EOF && fputc((int)((value>>8)&255),file)!=EOF,"write fixture word");
}
static void le32(FILE *file, uint32_t value) {
    le16(file,value&65535); le16(file,value>>16);
}
static void wave_header(FILE *file, uint32_t frames, unsigned channels) {
    require(fwrite("RIFF",1,4,file)==4,"write RIFF"); le32(file,36+frames*channels*2);
    require(fwrite("WAVEfmt ",1,8,file)==8,"write WAVE fmt"); le32(file,16);
    le16(file,1); le16(file,channels); le32(file,CM_RATE);
    le32(file,CM_RATE*channels*2); le16(file,channels*2); le16(file,16);
    require(fwrite("data",1,4,file)==4,"write data"); le32(file,frames*channels*2);
}
static void test_audio(void) {
    const char *name="build/test-audio.pcm"; CMError error={{0}}; CMAudio audio;
    FILE *file=fopen(name,"wb"); require(file!=NULL,"create wave fixture");
    wave_header(file,3,2);
    for (unsigned i=0;i<3;++i) { le16(file,0); le16(file,i==0 ? 32768 : (i==1 ? 32767 : 0)); }
    require(fclose(file)==0,"close fixture");
    require(cm_audio_open(&audio,name,0,&error)!=0,"stereo requires explicit channel");
    require(cm_audio_open(&audio,name,2,&error)==0,"WAVE detected regardless of extension");
    double value=0; require(audio.wave && audio.frames==3 && audio.channels==2,"WAVE metadata");
    require(cm_audio_read(&audio,&value,&error)==1 && value==-1,"negative full-scale WAVE");
    require(cm_audio_read(&audio,&value,&error)==1 && value==32767.0/32768,"positive WAVE scaling");
    require(cm_audio_seek(&audio,0,&error)==0,"rewind audio");
    require(cm_audio_read(&audio,&value,&error)==1 && value==-1,"rewound channel");
    require(cm_audio_close(&audio,&error)==0,"close audio");
    file=fopen(name,"wb"); require(file!=NULL,"create raw fixture");
    le16(file,32768); le16(file,32767); require(fclose(file)==0,"close raw fixture");
    require(cm_audio_open(&audio,name,1,&error)==0 && !audio.wave,"raw opens");
    require(cm_audio_read(&audio,&value,&error)==1 && value==-32768,"raw integer scale");
    require(cm_audio_close(&audio,&error)==0,"close raw");
    file=fopen(name,"ab"); require(file!=NULL,"open raw append"); fputc(1,file); fclose(file);
    require(cm_audio_open(&audio,name,1,&error)!=0,"odd raw byte count refused");
    file=fopen(name,"wb"); require(file!=NULL,"create truncated wave"); wave_header(file,20,1); le16(file,1); fclose(file);
    require(cm_audio_open(&audio,name,1,&error)!=0,"truncated RIFF data refused");
    file=fopen(name,"wb"); require(file!=NULL,"create odd padded chunk");
    fwrite("RIFF",1,4,file); le32(file,48); fwrite("WAVE",1,4,file);
    fwrite("JUNK",1,4,file); le32(file,3); fwrite("abc",1,3,file); fputc(0,file);
    fwrite("data",1,4,file); le32(file,0);
    fwrite("fmt ",1,4,file); le32(file,16); le16(file,1); le16(file,1); le32(file,CM_RATE);
    le32(file,CM_RATE*2); le16(file,2); le16(file,16); fclose(file);
    require(cm_audio_open(&audio,name,0,&error)!=0,"empty data rejected with padded unknown chunk");
    /* Same chunk ordering with nonempty data before fmt, including required padding. */
    file=fopen(name,"wb"); require(file!=NULL,"create data-before-fmt");
    fwrite("RIFF",1,4,file); le32(file,50); fwrite("WAVE",1,4,file);
    fwrite("JUNK",1,4,file); le32(file,3); fwrite("abc",1,3,file); fputc(0,file);
    fwrite("data",1,4,file); le32(file,2); le16(file,32767);
    fwrite("fmt ",1,4,file); le32(file,16); le16(file,1); le16(file,1); le32(file,CM_RATE);
    le32(file,CM_RATE*2); le16(file,2); le16(file,16); fclose(file);
    require(cm_audio_open(&audio,name,0,&error)==0,"unknown padded chunk and data-before-fmt accepted");
    require(cm_audio_read(&audio,&value,&error)==1 && value==32767.0/32768,"data-before-fmt sample");
    require(cm_audio_close(&audio,&error)==0,"close reordered WAVE");
    require(unlink(name)==0,"remove owned fixture");
    puts("PASS PCM/WAVE selection, scale, seek and truncation checks");
}
static void signal_file(const char *name, int alternating, int delayed, int wave,
                        uint64_t bytes, unsigned prefix, unsigned tail) {
    FILE *file=fopen(name,"wb"); require(file!=NULL,"create generated signal");
    uint64_t frames=bytes ? bytes/2 : prefix+(uint64_t)CM_SWEEP*(alternating ? 2 : 1)*6+tail;
    require(frames<UINT32_MAX/4,"fixture bounded");
    if (wave) wave_header(file,(uint32_t)frames,2);
    for (uint64_t i=0;i<frames;++i) {
        int32_t sample=0;
        if (i>=prefix && i+tail<frames) {
            uint64_t relative=i-prefix; unsigned c=alternating ? (unsigned)((relative/CM_SWEEP)%2) : 0;
            unsigned j=(unsigned)(relative%CM_SWEEP); double t=(double)j/CM_RATE;
            double u=t-(delayed ? 0.001 : 0);
            double phase=c ? 23000*u-150000*u*u : 17000*u+150000*u*u;
            double window=0.5-0.5*cos(2*CM_PI*(double)j/(CM_SWEEP-1));
            sample=(int32_t)lrint(12000*cos(2*CM_PI*phase)*window);
        }
        if (wave) le16(file,0);
        le16(file,(unsigned)(sample<0 ? sample+65536 : sample));
    }
    require(fclose(file)==0,"close generated signal");
}
static void remove_output(const char *directory, int spectra) {
    char path[256];
    const char *names[]={"cycles.csv","summary.json","spectra.csv"};
    for (size_t i=0;i<(spectra ? 3U : 2U);++i) {
        require(snprintf(path,sizeof path,"%s/%s",directory,names[i])>0,"owned output path");
        require(unlink(path)==0,"remove owned completed output");
    }
    require(rmdir(directory)==0,"remove owned output directory");
}
static void test_analysis(void) {
    const char *name="build/test-signal.wav",*out="build/test-analysis";
    CMError error={{0}}; CMReport report; CMOptions options=cm_default_options();
    signal_file(name,1,1,1,0,137,19);
    options.alternating=1; options.channel=2; options.start_sample=138; options.spectra=1;
    require(cm_analyze(name,out,&options,&report,&error)==0,"complete generated signal analysis");
    require(report.cycles==6 && report.prefix_samples==137 && report.suffix_samples==19 &&
            report.start_sample==138,"exact generated timing counts");
    FILE *file=fopen("build/test-analysis/cycles.csv","rb"); require(file!=NULL,"actual cycle output");
    char line[256]; require(fgets(line,sizeof line,file)!=NULL,"cycle header");
    unsigned row=0;
    while (fgets(line,sizeof line,file)) {
        unsigned long long cycle; unsigned c; double time,peak,delay,path;
        require(sscanf(line,"%llu,%u,%lf,%lf,%lf,%lf",&cycle,&c,&time,&peak,&delay,&path)==6,"numeric cycle row");
        double expected=((137.0+(row/2)*1922)+((row%2)*961))/48000;
        require(cycle==row/2 && c==row%2 && fabs(time-expected)<1e-12,"sweep timing and indices");
        require(fabs(peak-300)<=48000.0/16384,"analytic 300 Hz beat in both directions");
        require(fabs(delay-peak/300000)<1e-15 && fabs(path-delay*340)<1e-12,"relative acoustic path convention");
        if (row>=2) require(strstr(line,",0\n")!=NULL,"identical cycles have zero change");
        ++row;
    }
    require(row==12 && fclose(file)==0,"all generated sweeps output");
    require(cm_analyze(name,out,&options,&report,&error)!=0,"existing output refused");
    remove_output(out,1);
    signal_file(name,1,0,1,0,137,19); options.start_sample=0; options.spectra=0;
    require(cm_analyze(name,out,&options,&report,&error)==0,"automatic global alignment");
    require(report.start_sample==138 && report.cycles==6 && report.suffix_samples==19 &&
            report.alignment_score>0.999,"earliest near-global match and tail");
    require(report.peak_heap_bytes<=4*1024*1024,"default processing heap bounded");
    remove_output(out,0); require(unlink(name)==0,"remove generated signal");
    puts("PASS generated beat, timing, alignment, output and heap checks");
}
static void patch_word(const char *name,long offset,unsigned value) {
    FILE *file=fopen(name,"r+b"); require(file!=NULL,"open malformed fixture");
    require(fseek(file,offset,SEEK_SET)==0,"seek malformed fixture");
    le16(file,value); require(fclose(file)==0,"close malformed fixture");
}
static void test_refusals(void) {
    const char *name="build/test-refusal.wav",*out="build/test-refusal";
    CMError error={{0}}; CMAudio audio; CMReport report; CMOptions options=cm_default_options();
    const long offsets[]={20,22,24,28,32,34};
    const unsigned values[]={3,3,44100,1,1,8};
    for (size_t i=0;i<sizeof offsets/sizeof offsets[0];++i) {
        signal_file(name,0,0,1,0,0,0); patch_word(name,offsets[i],values[i]);
        require(cm_audio_open(&audio,name,2,&error)!=0,"unsupported WAVE field rejected");
    }
    signal_file(name,0,0,1,0,0,0);
    const size_t bad_fft[]={0,512,1023,2047,65537};
    for (size_t i=0;i<sizeof bad_fft/sizeof bad_fft[0];++i) {
        options.fft_length=bad_fft[i];
        require(cm_analyze(name,out,&options,&report,&error)!=0,"unsupported FFT option rejected");
    }
    options=cm_default_options(); options.channel=2; options.start_sample=UINT64_MAX;
    require(cm_analyze(name,out,&options,&report,&error)!=0,"out-of-bounds start rejected");
    options.start_sample=1; options.max_output_bytes=100;
    require(cm_analyze(name,out,&options,&report,&error)!=0,"output quota fails safely");
    char path[256]; const char *partial[]={"cycles.csv.partial","INCOMPLETE.txt"};
    require(access("build/test-refusal/summary.json",F_OK)!=0,"no completed summary after failure");
    for (size_t i=0;i<2;++i) {
        snprintf(path,sizeof path,"%s/%s",out,partial[i]); require(unlink(path)==0,"remove owned failed output");
    }
    require(rmdir(out)==0,"remove owned failed directory");
    /* Silence is valid at a known origin but cannot supply an automatic match. */
    FILE *silent=fopen(name,"wb"); require(silent!=NULL,"create silence");
    for (unsigned i=0;i<CM_SWEEP*2;++i) le16(silent,0);
    require(fclose(silent)==0,"close silence");
    options=cm_default_options();
    require(cm_analyze(name,out,&options,&report,&error)!=0,"silent automatic match rejected");
    options.start_sample=1;
    require(cm_analyze(name,out,&options,&report,&error)==0,"known-start silence analyzed");
    FILE *rows=fopen("build/test-refusal/cycles.csv","rb"); require(rows!=NULL,"silent rows");
    char line[256]; require(fgets(line,sizeof line,rows)!=NULL,"silent header");
    require(fgets(line,sizeof line,rows)!=NULL && strstr(line,",null,null,null,null\n")!=NULL,"undefined silence measurements explicit");
    require(fclose(rows)==0,"close silent rows"); remove_output(out,0);
    /* Short deterministic parser corpus runs under sanitizers as well. */
    uint32_t state=UINT32_C(0x29c717);
    for (unsigned trial=0;trial<512;++trial) {
        unsigned char data[96]; size_t size=trial%sizeof data+1;
        for (size_t j=0;j<size;++j) { state^=state<<13; state^=state>>17; state^=state<<5; data[j]=(unsigned char)state; }
        if (trial%2==0 && size>=12) memcpy(data,"RIFF",4);
        FILE *file=fopen(name,"wb"); require(file!=NULL,"create parser corpus");
        require(fwrite(data,1,size,file)==size && fclose(file)==0,"write parser corpus");
        if (cm_audio_open(&audio,name,1,&error)==0) {
            double value;
            while (cm_audio_read(&audio,&value,&error)==1) require(isfinite(value),"corpus conversion finite");
            require(cm_audio_close(&audio,&error)==0,"close corpus input");
        }
    }
    require(unlink(name)==0,"remove corpus fixture");
    puts("PASS invalid WAVE/options/start, output failure and 512 parser corpus cases");
}
static void test_block_boundaries(void) {
    const char *name="build/test-boundary.pcm",*out="build/test-boundary";
    CMError error={{0}}; CMReport report; CMOptions options=cm_default_options();
    const unsigned prefixes[]={6270,6271,8191,8192};
    options.alternating=1;
    for (size_t i=0;i<sizeof prefixes/sizeof prefixes[0];++i) {
        signal_file(name,1,0,0,0,prefixes[i],19);
        require(cm_analyze(name,out,&options,&report,&error)==0,"alignment at block boundary");
        require(report.start_sample==(uint64_t)prefixes[i]+1 && report.cycles==6 && report.suffix_samples==19,
                "no overlap-save boundary lag error");
        remove_output(out,0);
    }
    require(unlink(name)==0,"remove boundary fixture");
    puts("PASS correlation block-boundary prefixes");
}
static void test_fft_endpoints(void) {
    const char *name="build/test-endpoint.pcm",*out="build/test-endpoint";
    CMError error={{0}}; CMReport report; CMOptions options=cm_default_options();
    options.start_sample=1;
    const size_t lengths[]={1024,65536};
    signal_file(name,0,1,0,0,0,0);
    for (size_t i=0;i<2;++i) {
        options.fft_length=lengths[i];
        require(cm_analyze(name,out,&options,&report,&error)==0,"supported FFT endpoint executes");
        require(report.peak_heap_bytes<2*1024*1024,"maximum FFT processing heap below 2 MiB");
        FILE *file=fopen("build/test-endpoint/cycles.csv","rb"); require(file!=NULL,"endpoint rows");
        char line[256]; unsigned long long cycle;unsigned sweep;double time,peak;
        require(fgets(line,sizeof line,file)!=NULL && fgets(line,sizeof line,file)!=NULL,"endpoint first row");
        require(sscanf(line,"%llu,%u,%lf,%lf",&cycle,&sweep,&time,&peak)==4 &&
                fabs(peak-300)<=48000.0/lengths[i],"endpoint analytic beat within one bin");
        require(fclose(file)==0,"close endpoint output"); remove_output(out,0);
    }
    require(unlink(name)==0,"remove endpoint fixture");
    puts("PASS 1024/65536 FFT endpoints and heap bound");
}
static void memory_checks(void) {
    const char *name="build/test-memory.pcm",*out="build/test-memory";
    size_t expected=0; CMError error={{0}}; CMReport report; CMOptions options=cm_default_options();
    options.start_sample=1;
    const uint64_t sizes[]={UINT64_C(1048576),UINT64_C(67108864)};
    for (size_t i=0;i<2;++i) {
        signal_file(name,0,1,0,sizes[i],0,0);
        require(cm_analyze(name,out,&options,&report,&error)==0,"long generated input analysis");
        require(report.peak_heap_bytes<=4*1024*1024,"long input stays below 4 MiB tracked heap");
        if (!i) expected=report.peak_heap_bytes;
        require(report.peak_heap_bytes==expected,"processing heap independent of input length");
        printf("MEMORY input_bytes=%llu peak_processing_heap_bytes=%zu cycles=%llu\n",
               (unsigned long long)sizes[i],report.peak_heap_bytes,(unsigned long long)report.cycles);
        remove_output(out,0);
    }
    require(unlink(name)==0,"remove long fixture");
}
int main(int argc,char **argv) {
    if (argc==3 && strcmp(argv[1],"--demo")==0) {
        char path[1024];
        require(snprintf(path,sizeof path,"%s/delay.wav",argv[2])>0,"demo path");
        signal_file(path,1,1,1,0,137,19);
        require(snprintf(path,sizeof path,"%s/align.wav",argv[2])>0,"demo path");
        signal_file(path,1,0,1,0,137,19); return 0;
    }
    if (argc==2 && strcmp(argv[1],"--memory")==0) { memory_checks(); return 0; }
    require(argc==1,"test arguments");
    test_fft(); test_audio(); test_analysis(); test_refusals(); test_block_boundaries(); test_fft_endpoints(); return 0;
}
