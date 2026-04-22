#ifndef FFT_H 
#define FFT_H

#include "struct.h"
#include <memory>
#include <mutex>

class FFTProcessor {
private:
    struct PlanNode {
        int width;
        int height;
        int direction;
        fftwf_plan plan;
        PlanNode* next;
    };

    PlanNode* plan_cache;
    std::mutex plan_mutex; // Mutex to ensure thread-safe plan creation

    fftwf_plan get_plan_r2c(int width, int height, float *in, fftwf_complex *out);

public:
    static const int LNthreshold = 500;

    FFTProcessor();
    ~FFTProcessor();

    // Grayscale processing methods
    std::unique_ptr<ComplexGrayscale> forwardFFT(const GrayscaleImage& img);
    
    double energyRatio(ComplexGrayscale& fftShifted);
};

#endif