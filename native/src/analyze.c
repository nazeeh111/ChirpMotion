#define _POSIX_C_SOURCE 200809L
#include "chirpmotion.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CORRELATION_FFT 8192U
#define ALIGN_TOLERANCE 1e-12

typedef struct { FILE *file; uint64_t *total; uint64_t limit; } Writer;
CMOptions cm_default_options(void) {
    return (CMOptions){16384,0,0,0,0,CM_OUTPUT_LIMIT};
}
static int row(Writer *writer, CMError *error, const char *format, ...) {
    char buffer[2048]; va_list args; va_start(args,format);
    int size=vsnprintf(buffer,sizeof buffer,format,args); va_end(args);
    if (size<0 || (size_t)size>=sizeof buffer || (uint64_t)size>writer->limit-*writer->total) {
        cm_error(error,"output byte limit reached or row cannot be formatted"); return -1;
    }
    if (fwrite(buffer,1,(size_t)size,writer->file)!=(size_t)size) {
        cm_error(error,"failed writing output"); return -1;
    }
    *writer->total+=(uint64_t)size; return 0;
}
static const char *number(double value,char buffer[32]) {
    if (isfinite(value)) (void)snprintf(buffer,32,"%.17g",value);
    else (void)snprintf(buffer,32,"null");
    return buffer;
}
static int alignment(CMAudio *audio,const double *model,size_t period,CMHeap *heap,
                     uint64_t *first,double *best,CMError *error) {
    CMComplex *block=cm_alloc(heap,CORRELATION_FFT,sizeof *block,error);
    CMComplex *kernel=cm_alloc(heap,CORRELATION_FFT,sizeof *kernel,error);
    double *history=cm_alloc(heap,period-1,sizeof *history,error);
    double *ring=cm_alloc(heap,period,sizeof *ring,error);
    size_t capacity=CORRELATION_FFT-period+1;
    double *samples=cm_alloc(heap,capacity,sizeof *samples,error);
    int result=-1; double model_energy=0;
    if (!block || !kernel || !history || !ring || !samples) goto done;
    for (size_t i=0;i<period;++i) {
        kernel[i].real=model[period-1-i]; model_energy+=model[i]*model[i];
    }
    if (cm_fft(kernel,CORRELATION_FFT,0,error)!=0) goto done;
    *best=0; *first=0;
    /* Two full-input scans retain the global-maximum/earliest-99% rule. */
    for (unsigned pass=0;pass<2;++pass) {
        if (cm_audio_seek(audio,0,error)!=0) goto done;
        memset(history,0,(period-1)*sizeof *history);
        memset(ring,0,period*sizeof *ring);
        uint64_t seen=0; double energy=0;
        while (audio->position<audio->frames) {
            memset(block,0,CORRELATION_FFT*sizeof *block);
            for (size_t i=0;i<period-1;++i) block[i].real=history[i];
            size_t filled=0;
            while (filled<capacity && audio->position<audio->frames) {
                if (cm_audio_read(audio,&samples[filled],error)!=1) goto done;
                block[period-1+filled].real=samples[filled]; ++filled;
            }
            for (size_t i=0;i<period-1;++i) history[i]=block[filled+i].real;
            if (cm_fft(block,CORRELATION_FFT,0,error)!=0) goto done;
            for (size_t i=0;i<CORRELATION_FFT;++i) {
                double real=block[i].real*kernel[i].real-block[i].imag*kernel[i].imag;
                double imag=block[i].real*kernel[i].imag+block[i].imag*kernel[i].real;
                block[i]=(CMComplex){real,imag};
            }
            if (cm_fft(block,CORRELATION_FFT,1,error)!=0) goto done;
            for (size_t i=0;i<filled;++i) {
                size_t slot=(size_t)(seen%period);
                double old=ring[slot],sample=samples[i]; ring[slot]=sample;
                energy+=sample*sample-old*old; ++seen;
                if (seen<period) continue;
                /* Recompute near zero to avoid subtraction residue masquerading as signal. */
                if (energy<1e-12*model_energy) {
                    energy=0;
                    for (size_t j=0;j<period;++j) energy+=ring[j]*ring[j];
                }
                double score=energy>0 ? fabs(block[period-1+i].real)/sqrt(energy*model_energy) : 0;
                if (!isfinite(score) || score>1.00000001) {
                    cm_error(error,"correlation normalization failed"); goto done;
                }
                if (score>1) score=1;
                if (!pass) { if (score>*best) *best=score; }
                else if (score>=0.99*(*best)-ALIGN_TOLERANCE) {
                    *first=seen-period+1; result=0; goto done;
                }
            }
        }
        if (!pass && *best<0.1) {
            cm_error(error,"global waveform match is below 0.1; supply correct mode/channel/timing"); goto done;
        }
    }
    cm_error(error,"no earliest qualifying waveform match found");
done:
    cm_free(samples); cm_free(ring); cm_free(history); cm_free(kernel); cm_free(block);
    return result;
}
typedef struct {
    const char *status;
    double competing, noise_rms, peak_to_noise, resolved_peak, resolved_valley;
} CMQuality;
/* Heuristic screening, not a calibrated detection probability or physical accuracy. */
static CMQuality spectrum_quality(const double *spectrum,size_t bins,size_t peak,
                                  double amplitude,size_t fft_length) {
    CMQuality quality={"no_signal",0,0,NAN,0,NAN};
    if (amplitude==0) return quality;
    size_t guard=(150U*fft_length+CM_RATE-1)/CM_RATE, count=0;
    double sum=0;
    for (size_t k=0;k<bins;++k) {
        size_t distance=k>peak ? k-peak : peak-k;
        if (distance<=guard) continue;
        double normalized=spectrum[k]/amplitude;
        sum+=normalized*normalized; ++count;
        if (spectrum[k]>quality.competing) quality.competing=spectrum[k];
    }
    double noise_relative=count ? sqrt(sum/count) : 0;
    quality.noise_rms=noise_relative*amplitude;
    if (noise_relative>0) quality.peak_to_noise=1/noise_relative;
    /* Scan both sides from the dominant peak; a shoulder has no intervening valley. */
    for (int direction=-1;direction<=1;direction+=2) {
        size_t k=peak;
        double valley=amplitude;
        while ((direction<0 && k>0) || (direction>0 && k+1<bins)) {
            k=direction<0 ? k-1 : k+1;
            double value=spectrum[k];
            if (value<valley) valley=value;
            int local_max = k==0 ? value>=spectrum[1] :
                k==bins-1 ? value>=spectrum[bins-2] :
                value>=spectrum[k-1] && value>=spectrum[k+1] &&
                (value>spectrum[k-1] || value>spectrum[k+1]);
            if (local_max && value>=0.65*amplitude && valley<=0.5*value &&
                value>quality.resolved_peak) {
                quality.resolved_peak=value;
                quality.resolved_valley=valley;
            }
        }
    }
    double hz=(double)peak*CM_RATE/fft_length;
    quality.status=(hz<=150 || hz>=1350 || !count || quality.resolved_peak>0 ||
        quality.competing>=0.65*amplitude || noise_relative>0.25) ? "ambiguous" : "usable";
    return quality;
}
static int path_for(char path[1024],const char *directory,const char *name,CMError *error) {
    int size=snprintf(path,1024,"%s/%s",directory,name);
    if (size<0 || size>=1024) { cm_error(error,"output path is too long"); return -1; }
    return 0;
}
static int close_writer(Writer *writer,CMError *error) {
    if (!writer->file) return 0;
    FILE *file=writer->file; writer->file=NULL;
    if (fclose(file)!=0) { cm_error(error,"failed flushing or closing output"); return -1; }
    return 0;
}
int cm_analyze_with_outcomes(const char *input,const char *output,const CMOptions *options,
                            CMReport *report,CMOutcomeCounts *outcomes,CMError *error) {
    if (outcomes) memset(outcomes,0,sizeof *outcomes);
    if (!input || !output || !options || !report || !*output || strlen(output)>900 ||
        options->fft_length<1024 || options->fft_length>65536 ||
        (options->fft_length&(options->fft_length-1)) || options->channel>2 ||
        (options->alternating!=0 && options->alternating!=1) ||
        (options->spectra!=0 && options->spectra!=1) || !options->max_output_bytes ||
        options->max_output_bytes>CM_OUTPUT_LIMIT) {
        cm_error(error,"invalid native options or output path"); return -1;
    }
    memset(report,0,sizeof *report); report->alignment_score=NAN;
    struct stat existing;
    if (lstat(output,&existing)==0 || errno!=ENOENT) {
        cm_error(error,"output destination must not exist"); return -1;
    }
    CMAudio audio; if (cm_audio_open(&audio,input,options->channel,error)!=0) return -1;
    CMHeap heap={0,0}; size_t channels=options->alternating ? 2 : 1,period=CM_SWEEP*channels;
    double *model=cm_alloc(&heap,period,sizeof *model,error);
    CMComplex *transform=NULL; double *previous=NULL,*spectrum=NULL;
    int result=-1,created=0; uint64_t total_output=0;
    Writer cycles={NULL,&total_output,options->max_output_bytes};
    Writer spectra={NULL,&total_output,options->max_output_bytes};
    Writer summary={NULL,&total_output,options->max_output_bytes};
    char cycle_path[1024],spectra_path[1024],summary_path[1024],final_path[1024];
    if (!model) goto done;
    cm_template(model,options->alternating);
    if (audio.frames<period) { cm_error(error,"input has no complete waveform cycle"); goto done; }
    report->start_sample=options->start_sample;
    if (!report->start_sample && alignment(&audio,model,period,&heap,&report->start_sample,
                                         &report->alignment_score,error)!=0) goto done;
    if (!report->start_sample || report->start_sample>audio.frames ||
        audio.frames-(report->start_sample-1)<period) {
        cm_error(error,"start sample leaves no complete waveform cycle"); goto done;
    }
    report->input_frames=audio.frames; report->input_channels=audio.channels;
    report->selected_channel=audio.channel; report->sweep_channels=(unsigned)channels; report->wave=audio.wave;
    report->prefix_samples=report->start_sample-1;
    report->cycles=(audio.frames-report->prefix_samples)/period;
    report->suffix_samples=audio.frames-report->prefix_samples-report->cycles*period;
    size_t bins=1500*options->fft_length/CM_RATE+1;
    transform=cm_alloc(&heap,options->fft_length,sizeof *transform,error);
    previous=cm_alloc(&heap,bins*channels,sizeof *previous,error);
    spectrum=cm_alloc(&heap,bins,sizeof *spectrum,error);
    if (!transform || !previous || !spectrum) goto done;
    if (cm_audio_seek(&audio,report->prefix_samples,error)!=0) goto done;
    if (mkdir(output,0700)!=0) { cm_error(error,"cannot create new output directory: %s",strerror(errno)); goto done; }
    created=1;
    if (path_for(cycle_path,output,"cycles.csv.partial",error)!=0 ||
        path_for(spectra_path,output,"spectra.csv.partial",error)!=0 ||
        path_for(summary_path,output,"summary.json.partial",error)!=0) goto done;
    cycles.file=fopen(cycle_path,"wx");
    if (!cycles.file) { cm_error(error,"cannot create cycle output"); goto done; }
    if (options->spectra) {
        spectra.file=fopen(spectra_path,"wx");
        if (!spectra.file) { cm_error(error,"cannot create spectrum output"); goto done; }
        if (row(&spectra,error,"cycle,sweep,frequency_hz,magnitude\n")!=0) goto done;
    }
    if (row(&cycles,error,"cycle,sweep,start_seconds,peak_hz,relative_delay_seconds,relative_path_meters,relative_spectral_change,quality,peak_magnitude,competing_magnitude,noise_rms_magnitude,peak_to_noise_rms,resolved_competing_magnitude,resolved_valley_magnitude\n")!=0) goto done;
    for (uint64_t cycle=0;cycle<report->cycles;++cycle) for (size_t c=0;c<channels;++c) {
        memset(transform,0,options->fft_length*sizeof *transform);
        for (size_t j=0;j<CM_SWEEP;++j) {
            double sample;
            if (cm_audio_read(&audio,&sample,error)!=1) goto done;
            transform[j].real=sample*model[c*CM_SWEEP+j];
        }
        if (cm_fft(transform,options->fft_length,0,error)!=0) goto done;
        double amplitude=0,norm=0,delta=0; size_t peak=0;
        for (size_t k=0;k<bins;++k) {
            double magnitude=hypot(transform[k].real,transform[k].imag);
            if (!isfinite(magnitude)) { cm_error(error,"nonfinite spectrum"); goto done; }
            spectrum[k]=magnitude;
            if (magnitude>amplitude) { amplitude=magnitude; peak=k; }
            double old=previous[c*bins+k],difference=magnitude-old;
            norm+=old*old; delta+=difference*difference;
            if (options->spectra && row(&spectra,error,"%llu,%zu,%.17g,%.17g\n",
                (unsigned long long)cycle,c,(double)k*CM_RATE/options->fft_length,magnitude)!=0) goto done;
        }
        double change=cycle && norm>0 ? sqrt(delta/norm) : NAN;
        double peak_hz=amplitude>0 ? (double)peak*CM_RATE/options->fft_length : NAN;
        CMQuality quality=spectrum_quality(spectrum,bins,peak,amplitude,options->fft_length);
        double delay=strcmp(quality.status,"usable")==0 ? peak_hz/300000 : NAN;
        double path=delay*340;
        double time=(double)(report->prefix_samples+cycle*period+c*CM_SWEEP)/CM_RATE;
        char a[32],b[32],d[32],e[32],f[32],g[32],h[32],i[32],j[32],l[32];
        if (row(&cycles,error,"%llu,%zu,%.17g,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",(unsigned long long)cycle,c,time,
                number(peak_hz,a),number(delay,b),number(path,d),number(change,e),quality.status,
                number(amplitude,f),number(quality.competing,g),number(quality.noise_rms,h),number(quality.peak_to_noise,i),
                number(quality.resolved_peak,j),number(quality.resolved_valley,l))!=0) goto done;
        if (outcomes) {
            if (strcmp(quality.status,"usable")==0) ++outcomes->usable;
            else if (strcmp(quality.status,"ambiguous")==0) ++outcomes->ambiguous;
            else ++outcomes->no_signal;
        }
        memcpy(previous+c*bins,spectrum,bins*sizeof *spectrum);
    }
    report->peak_heap_bytes=heap.peak;
    if (cm_audio_close(&audio,error)!=0 || close_writer(&cycles,error)!=0 ||
        close_writer(&spectra,error)!=0) goto done;
    summary.file=fopen(summary_path,"wx");
    if (!summary.file) { cm_error(error,"cannot create summary output"); goto done; }
    char score[32];
    if (row(&summary,error,
        "{\n  \"format\": \"chirpmotion.native/v1\",\n  \"mode\": \"%s\",\n"
        "  \"input_format\": \"%s\",\n  \"amplitude_scale\": \"%s\",\n"
        "  \"sample_rate_hz\": 48000,\n  \"sweep_samples\": 961,\n"
        "  \"input_frames\": %llu,\n  \"input_channels\": %u,\n  \"selected_channel\": %u,\n"
        "  \"start_sample\": %llu,\n  \"alignment_score\": %s,\n"
        "  \"alignment_absolute_tolerance\": 1e-12,\n  \"cycles\": %llu,\n"
        "  \"discarded_prefix_samples\": %llu,\n  \"discarded_suffix_samples\": %llu,\n"
        "  \"fft_length\": %zu,\n  \"bin_spacing_hz\": %.17g,\n"
        "  \"max_beat_hz\": 1500,\n  \"quality_policy\": \"heuristic-v2\",\n"
        "  \"quality_neighborhood_hz\": 150,\n  \"quality_competing_peak_ratio\": 0.65,\n"
        "  \"quality_min_peak_to_rms\": 4,\n  \"quality_resolved_peak_ratio\": 0.65,\n"
        "  \"quality_resolved_valley_ratio\": 0.5,\n  \"sweep_slope_hz_per_second\": 300000,\n"
        "  \"sound_speed_meters_per_second\": 340,\n  \"full_spectra\": %s,\n"
        "  \"max_output_bytes\": %llu,\n"
        "  \"peak_processing_heap_bytes\": %zu,\n"
        "  \"interpretation\": \"relative acoustic path, not calibrated object distance or gesture labels\"\n}\n",
        options->alternating ? "alternating" : "up",report->wave ? "wave_pcm16" : "raw_pcm16le",
        report->wave ? "sample/32768" : "int16 units",(unsigned long long)report->input_frames,
        report->input_channels,report->selected_channel,(unsigned long long)report->start_sample,
        number(report->alignment_score,score),(unsigned long long)report->cycles,
        (unsigned long long)report->prefix_samples,(unsigned long long)report->suffix_samples,
        options->fft_length,(double)CM_RATE/options->fft_length,options->spectra ? "true" : "false",
        (unsigned long long)options->max_output_bytes,heap.peak)!=0 ||
        close_writer(&summary,error)!=0) goto done;
    if (path_for(final_path,output,"cycles.csv",error)!=0 || rename(cycle_path,final_path)!=0) {
        cm_error(error,"cannot finalize cycle output"); goto done;
    }
    if (options->spectra && (path_for(final_path,output,"spectra.csv",error)!=0 ||
                            rename(spectra_path,final_path)!=0)) {
        cm_error(error,"cannot finalize spectrum output"); goto done;
    }
    if (path_for(final_path,output,"summary.json",error)!=0 || rename(summary_path,final_path)!=0) {
        cm_error(error,"cannot finalize summary output"); goto done;
    }
    result=0;
done:
    /* Retain only this run's incomplete directory; never remove a user's destination. */
    (void)cm_audio_close(&audio,result ? NULL : error);
    (void)close_writer(&cycles,NULL); (void)close_writer(&spectra,NULL); (void)close_writer(&summary,NULL);
    if (result && created && path_for(final_path,output,"INCOMPLETE.txt",NULL)==0) {
        FILE *marker=fopen(final_path,"wx");
        if (marker) { (void)fputs("Analysis failed. Files here may contain partial rows. No completed summary was published.\n",marker); (void)fclose(marker); }
    }
    cm_free(spectrum); cm_free(previous); cm_free(transform); cm_free(model);
    return result;
}

int cm_analyze(const char *input,const char *output,const CMOptions *options,
               CMReport *report,CMError *error) {
    return cm_analyze_with_outcomes(input,output,options,report,NULL,error);
}
