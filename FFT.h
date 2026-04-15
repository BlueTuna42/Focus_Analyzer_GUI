#ifndef FFT_H 
#define FFT_H

#include "struct.h"
#include <memory>
#include <vector>

// Encapsulate Fourier transform logic in a separate class
class FFTProcessor {
private:
    struct PlanNode {
        int width;
        int height;
        int direction; // FFTW_FORWARD or FFTW_BACKWARD
        fftwf_plan plan;
        PlanNode* next;
    };

    PlanNode* plan_cache;
    fftwf_plan get_plan(int width, int height, fftwf_complex *in, fftwf_complex *out, int direction);
    double energyRatioChannel(fftwf_complex *in, int width, int height);
    void swap_complex(fftwf_complex *a, fftwf_complex *b);
    void fftShiftChannel(fftwf_complex *data, int width, int height);
    void computeInverseFFTChannel(fftwf_complex *inData, fftwf_complex *outData, int width, int height);

public:
    static const int SQthreshold = 10;
    static const int LNthreshold = 500;

    FFTProcessor();
    ~FFTProcessor(); // Automatically clears the plans cache

    std::unique_ptr<ComplexRGB> forwardFFT(const RGBImage& img);
    void shift(ComplexRGB& data);
    std::unique_ptr<ComplexRGB> inverseFFT(const ComplexRGB& fft);
    double energyRatio(ComplexRGB& fftShifted);

    // Grayscale processing methods
    std::unique_ptr<ComplexGrayscale> forwardFFT(const GrayscaleImage& img);
    void shift(ComplexGrayscale& data);
    double energyRatio(ComplexGrayscale& fftShifted);
};

#endif