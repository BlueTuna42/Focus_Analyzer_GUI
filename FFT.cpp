#include "FFT.h"
#include <cmath>
#include <future>

FFTProcessor::FFTProcessor() : plan_cache(nullptr) {}

FFTProcessor::~FFTProcessor() {
    PlanNode *node = plan_cache;
    while (node) {
        fftwf_destroy_plan(node->plan);                    
        PlanNode *next = node->next;
        delete node;
        node = next;
    }
}

fftwf_plan FFTProcessor::get_plan_r2c(int width, int height, float *in, fftwf_complex *out) {
    std::lock_guard<std::mutex> lock(plan_mutex);
    for (PlanNode *node = plan_cache; node; node = node->next) {
        if (node->width == width && node->height == height) {
            return node->plan;                              
        }
    }
    fftwf_plan plan = fftwf_plan_dft_r2c_2d(height, width, in, out, FFTW_ESTIMATE);
    PlanNode *new_node = new PlanNode{width, height, 0, plan, plan_cache};
    plan_cache = new_node;
    return plan;
}

double FFTProcessor::energyRatio(ComplexGrayscale& fft) {
    int h = fft.height;
    int cw = fft.complex_w; // width/2 + 1
    int w = fft.width;

    double mag_low1 = 0, mag_high1 = 0;
    int nLow1 = 0, nHigh1 = 0;
    double mag_low2 = 0, mag_high2 = 0;
    int nLow2 = 0, nHigh2 = 0;

    auto calc_chunk = [&](int start_row, int end_row, double& m_low, double& m_high, int& n_l, int& n_h) {
        for (int i = start_row; i < end_row; i++) {
            // Vertical frequency index (mirrored around height/2)
            int freq_y = (i <= h / 2) ? i : (h - i);
            
            for (int j = 0; j < cw; j++) {
                // Horizontal frequency index (already halved)
                int freq_x = j;
                
                int k = i * cw + j;
                float magnitude = std::hypot(fft.data[k][0], fft.data[k][1]);
                
                // Frequency boundaries
                if (std::abs(freq_x) < w / LNthreshold && std::abs(freq_y) < h / LNthreshold) {
                    m_low += magnitude;
                    n_l++;
                } else {
                    m_high += magnitude;
                    n_h++;
                }
            }
        }
    };

    int mid = h / 2;
    auto f1 = std::async(std::launch::async, calc_chunk, 0, mid, std::ref(mag_low1), std::ref(mag_high1), std::ref(nLow1), std::ref(nHigh1));
    calc_chunk(mid, h, mag_low2, mag_high2, nLow2, nHigh2);
    f1.wait();

    double mag_low = mag_low1 + mag_low2;
    double mag_high = mag_high1 + mag_high2;
    int nLow = nLow1 + nLow2;
    int nHigh = nHigh1 + nHigh2;

    if (nLow == 0 || nHigh == 0 || mag_low == 0) return 0.0;
    return (mag_high / nHigh) / (mag_low / nLow);
}

std::unique_ptr<ComplexGrayscale> FFTProcessor::forwardFFT(const GrayscaleImage& img) {
    auto fft = std::make_unique<ComplexGrayscale>(img.width, img.height);
    fftwf_execute_dft_r2c(get_plan_r2c(img.width, img.height, img.data, fft->data), img.data, fft->data);
    return fft;
}