#include "chirpmotion.h"
#include <math.h>
int cm_fft(CMComplex *values, size_t length, int inverse, CMError *error) {
    if (!values || length==0 || length>65536 || (length & (length-1)) ||
        (inverse!=0 && inverse!=1)) {
        cm_error(error,"FFT length must be a power of two at most 65536"); return -1;
    }
    for (size_t i=1,j=0; i<length; ++i) {
        size_t bit=length>>1;
        while (j & bit) { j^=bit; bit>>=1; }
        j^=bit;
        if (i<j) { CMComplex temporary=values[i]; values[i]=values[j]; values[j]=temporary; }
    }
    for (size_t width=2; width<=length; width<<=1) {
        double angle=(inverse ? 2.0 : -2.0)*CM_PI/(double)width;
        double step_real=cos(angle), step_imag=sin(angle);
        for (size_t base=0; base<length; base+=width) {
            double wr=1.0, wi=0.0;
            for (size_t j=0; j<width/2; ++j) {
                CMComplex a=values[base+j], b=values[base+j+width/2];
                double br=b.real*wr-b.imag*wi, bi=b.real*wi+b.imag*wr;
                values[base+j]=(CMComplex){a.real+br,a.imag+bi};
                values[base+j+width/2]=(CMComplex){a.real-br,a.imag-bi};
                double next=wr*step_real-wi*step_imag;
                wi=wr*step_imag+wi*step_real; wr=next;
            }
        }
    }
    if (inverse) for (size_t i=0; i<length; ++i) {
        values[i].real/=(double)length; values[i].imag/=(double)length;
    }
    return 0;
}
void cm_template(double *values, int alternating) {
    unsigned channels=alternating ? 2U : 1U;
    for (unsigned c=0; c<channels; ++c) for (size_t j=0; j<CM_SWEEP; ++j) {
        double t=(double)j/CM_RATE;
        double phase=c ? 23000*t-150000*t*t : 17000*t+150000*t*t;
        double window=0.5-0.5*cos(2*CM_PI*(double)j/(CM_SWEEP-1));
        values[c*CM_SWEEP+j]=cos(2*CM_PI*phase)*window;
    }
}
