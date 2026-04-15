#include "FFT.h"
#include <cmath>

FFTProcessor::FFTProcessor() : plan_cache(nullptr) {}

FFTProcessor::~FFTProcessor() {
    // Clean up all created Fourier plans
    PlanNode *node = plan_cache;
    while (node) {
        fftwf_destroy_plan(node->plan);                    
        PlanNode *next = node->next;
        delete node;
        node = next;
    }
}

fftwf_plan FFTProcessor::get_plan(int width, int height, fftwf_complex *in, fftwf_complex *out, int direction) {
    for (PlanNode *node = plan_cache; node; node = node->next) {
        if (node->width == width && node->height == height && node->direction == direction) {
            return node->plan;                              
        }
    }

    // Use FFTW_ESTIMATE to avoid overwriting input arrays during plan creation
    fftwf_plan plan = fftwf_plan_dft_2d(height, width, in, out, direction, FFTW_ESTIMATE);

    PlanNode *new_node = new PlanNode();
    new_node->width  = width;
    new_node->height = height;
    new_node->direction = direction;
    new_node->plan   = plan;
    new_node->next   = plan_cache;
    plan_cache       = new_node;
    return plan;
}

double FFTProcessor::energyRatioChannel(fftwf_complex *in, int width, int height) {
    double mag_low = 0;
    double mag_high = 0;
    int nHigh = 0;
    int nLow = 0;
    
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int x = j - width / 2;  
            int y = i - height / 2;
            int k = i * width + j;
            
            // Calculate magnitude std::hypot(real, imag)
            float magnitude = std::hypot(in[k][0], in[k][1]);
            
            if (std::abs(x) < width / LNthreshold || std::abs(y) < height / LNthreshold) {
                mag_low += magnitude;
                nLow++;
                // Zero out low frequencies
                in[k][0] = 0;
                in[k][1] = 0;
            } else {
                mag_high += magnitude;
                nHigh++;
            }
        }
    }
    
    if (nLow == 0 || nHigh == 0 || mag_low == 0) return 0.0;
    return (mag_high / nHigh) / (mag_low / nLow);
}

void FFTProcessor::swap_complex(fftwf_complex *a, fftwf_complex *b) {
    float tmp_r = (*a)[0];
    float tmp_i = (*a)[1];
    (*a)[0] = (*b)[0];
    (*a)[1] = (*b)[1];
    (*b)[0] = tmp_r;
    (*b)[1] = tmp_i;
}

void FFTProcessor::fftShiftChannel(fftwf_complex *data, int width, int height) {
    int half_rows = height / 2;
    int half_cols = width / 2;

    for (int i = 0; i < height; i++) {
        int ii = (i + half_rows) % height;
        for (int j = 0; j < width; j++) {
            int jj = (j + half_cols) % width;
            if (i < ii || (i == ii && j < jj)) {
                swap_complex(&data[i * width + j], &data[ii * width + jj]);
            }
        }
    }
}

std::unique_ptr<ComplexRGB> FFTProcessor::forwardFFT(const RGBImage& img) {
    // Use smart pointers to transfer ownership
    auto fft = std::make_unique<ComplexRGB>(img.width, img.height);

    fftwf_execute_dft(get_plan(img.width, img.height, img.red, fft->red, FFTW_FORWARD), img.red, fft->red);
    fftwf_execute_dft(get_plan(img.width, img.height, img.green, fft->green, FFTW_FORWARD), img.green, fft->green);
    fftwf_execute_dft(get_plan(img.width, img.height, img.blue, fft->blue, FFTW_FORWARD), img.blue, fft->blue);

    return fft;
}

void FFTProcessor::shift(ComplexRGB& data) {
    fftShiftChannel(data.red, data.width, data.height);
    fftShiftChannel(data.green, data.width, data.height);
    fftShiftChannel(data.blue, data.width, data.height); 
}

void FFTProcessor::computeInverseFFTChannel(fftwf_complex *inData, fftwf_complex *outData, int width, int height) {
    int total = width * height;
    fftwf_complex *inCopy = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    
    for (int i = 0; i < total; i++) {
        inCopy[i][0] = inData[i][0];
        inCopy[i][1] = inData[i][1];
    }
    
    fftwf_plan plan = fftwf_plan_dft_2d(height, width, inCopy, outData, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftwf_execute(plan);
    fftwf_destroy_plan(plan);
    fftwf_free(inCopy);
    
    for (int i = 0; i < total; i++) {
        outData[i][0] /= total;
        outData[i][1] /= total;
    }
}

std::unique_ptr<ComplexRGB> FFTProcessor::inverseFFT(const ComplexRGB& fft) {
    auto out = std::make_unique<ComplexRGB>(fft.width, fft.height);

    computeInverseFFTChannel(fft.red, out->red, fft.width, fft.height);
    computeInverseFFTChannel(fft.green, out->green, fft.width, fft.height);
    computeInverseFFTChannel(fft.blue, out->blue, fft.width, fft.height);

    return out;
}

double FFTProcessor::energyRatio(ComplexRGB& fftShifted) {
    double erR = energyRatioChannel(fftShifted.red, fftShifted.width, fftShifted.height);
    double erG = energyRatioChannel(fftShifted.green, fftShifted.width, fftShifted.height);
    double erB = energyRatioChannel(fftShifted.blue, fftShifted.width, fftShifted.height);
    
    return std::sqrt(std::pow(erR, 2) + std::pow(erG, 2) + std::pow(erB, 2));
}

std::unique_ptr<ComplexGrayscale> FFTProcessor::forwardFFT(const GrayscaleImage& img) {
    auto fft = std::make_unique<ComplexGrayscale>(img.width, img.height);
    fftwf_execute_dft(get_plan(img.width, img.height, img.gray, fft->gray, FFTW_FORWARD), img.gray, fft->gray);
    return fft;
}

void FFTProcessor::shift(ComplexGrayscale& data) {
    fftShiftChannel(data.gray, data.width, data.height);
}

double FFTProcessor::energyRatio(ComplexGrayscale& fftShifted) {
    return energyRatioChannel(fftShifted.gray, fftShifted.width, fftShifted.height);
}