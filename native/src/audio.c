#define _POSIX_C_SOURCE 200809L
#include "chirpmotion.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static uint16_t word(const unsigned char *bytes) {
    return (uint16_t)((uint16_t)bytes[0]|((uint16_t)bytes[1]<<8));
}
static uint32_t dword(const unsigned char *bytes) {
    return (uint32_t)word(bytes)|((uint32_t)word(bytes+2)<<16);
}
static int read_exact(FILE *file, void *buffer, size_t bytes, CMError *error) {
    if (fread(buffer,1,bytes,file)!=bytes) {
        cm_error(error,"truncated audio data or failed read"); return -1;
    }
    return 0;
}
static int seek_byte(FILE *file, uint64_t offset, CMError *error) {
    if (offset>CM_INPUT_LIMIT || fseeko(file,(off_t)offset,SEEK_SET)!=0) {
        cm_error(error,"audio seek failed"); return -1;
    }
    return 0;
}
int cm_audio_open(CMAudio *audio,const char *path,unsigned channel,CMError *error) {
    if (!audio || !path || channel>2) { cm_error(error,"invalid audio request"); return -1; }
    memset(audio,0,sizeof *audio);
    FILE *file=fopen(path,"rb");
    if (!file) { cm_error(error,"cannot open input: %s",strerror(errno)); return -1; }
    struct stat info;
    if (fstat(fileno(file),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<=0 ||
        (uint64_t)info.st_size>CM_INPUT_LIMIT) {
        cm_error(error,"input must be a nonempty seekable regular file at most 1 GiB"); goto fail;
    }
    uint64_t length=(uint64_t)info.st_size;
    unsigned char header[12]={0}; size_t count=(size_t)(length<12 ? length : 12);
    if (read_exact(file,header,count,error)!=0) goto fail;
    int riff=count>=4 && (memcmp(header,"RIFF",4)==0 || memcmp(header,"RIFX",4)==0 ||
                        memcmp(header,"RF64",4)==0);
    if (riff) {
        if (count!=12 || memcmp(header,"RIFF",4)!=0 || memcmp(header+8,"WAVE",4)!=0 ||
            (uint64_t)dword(header+4)+8!=length) {
            cm_error(error,"unsupported or inconsistent RIFF/WAVE header"); goto fail;
        }
        audio->wave=1;
        uint64_t offset=12; unsigned chunks=0; int have_fmt=0,have_data=0;
        while (offset<length) {
            if (++chunks>128 || length-offset<8 || seek_byte(file,offset,error)!=0 ||
                read_exact(file,header,8,error)!=0) {
                cm_error(error,"invalid or excessive RIFF chunks"); goto fail;
            }
            uint64_t size=dword(header+4),payload=offset+8;
            if (size>length-payload || (size&1U)>length-payload-size) {
                cm_error(error,"RIFF chunk extends beyond input"); goto fail;
            }
            if (memcmp(header,"fmt ",4)==0) {
                unsigned char format[18]={0};
                if (have_fmt || (size!=16 && size!=18) ||
                    read_exact(file,format,(size_t)size,error)!=0) {
                    cm_error(error,"unsupported or duplicate WAVE format chunk"); goto fail;
                }
                unsigned channels=word(format+2),align=word(format+12);
                if (word(format)!=1 || (channels!=1 && channels!=2) ||
                    dword(format+4)!=CM_RATE || align!=channels*2 ||
                    dword(format+8)!=CM_RATE*align || word(format+14)!=16 ||
                    (size==18 && word(format+16)!=0)) {
                    cm_error(error,"WAVE must be classic PCM16, 48000 Hz, one or two channels"); goto fail;
                }
                audio->channels=channels; have_fmt=1;
            } else if (memcmp(header,"data",4)==0) {
                if (have_data) { cm_error(error,"duplicate WAVE data chunk"); goto fail; }
                audio->data_offset=payload; audio->data_bytes=size; have_data=1;
            }
            offset=payload+size+(size&1U);
        }
        if (!have_fmt || !have_data || !audio->data_bytes ||
            audio->data_bytes%(audio->channels*2)) {
            cm_error(error,"WAVE needs complete format and aligned nonempty data"); goto fail;
        }
    } else {
        if (length%2 || channel>1) { cm_error(error,"raw PCM is mono int16 with an even byte count"); goto fail; }
        audio->channels=1; audio->data_bytes=length;
    }
    if ((audio->channels>1 && !channel) || channel>audio->channels) {
        cm_error(error,"multichannel WAVE needs an explicit valid --channel"); goto fail;
    }
    audio->channel=channel ? channel : 1;
    audio->frames=audio->data_bytes/(audio->channels*2);
    audio->file=file;
    if (cm_audio_seek(audio,0,error)!=0) { audio->file=NULL; goto fail; }
    return 0;
fail:
    (void)fclose(file); memset(audio,0,sizeof *audio); return -1;
}
int cm_audio_seek(CMAudio *audio,uint64_t frame,CMError *error) {
    if (!audio || !audio->file || frame>audio->frames) {
        cm_error(error,"sample position is outside input"); return -1;
    }
    uint64_t offset=audio->data_offset+frame*audio->channels*2;
    if (seek_byte(audio->file,offset,error)!=0) return -1;
    clearerr(audio->file); audio->position=frame; return 0;
}
int cm_audio_read(CMAudio *audio,double *value,CMError *error) {
    if (!audio || !audio->file || !value) { cm_error(error,"invalid audio read"); return -1; }
    if (audio->position==audio->frames) return 0;
    unsigned char bytes[4];
    if (read_exact(audio->file,bytes,audio->channels*2,error)!=0) return -1;
    uint16_t encoded=word(bytes+2*(audio->channel-1));
    int32_t sample=encoded>=32768 ? (int32_t)encoded-65536 : (int32_t)encoded;
    *value=audio->wave ? (double)sample/32768.0 : (double)sample;
    ++audio->position; return 1;
}
int cm_audio_close(CMAudio *audio,CMError *error) {
    if (audio && audio->file) {
        FILE *file=audio->file; audio->file=NULL;
        if (fclose(file)!=0) { cm_error(error,"failed closing audio input"); return -1; }
    }
    return 0;
}
