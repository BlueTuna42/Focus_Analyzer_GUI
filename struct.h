#ifndef STRUCT_H 
#define STRUCT_H

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fftw3.h>

#define PATH_MAX 256

class GrayscaleImage {
public:
    int width;
    int height;
    float *data;

    GrayscaleImage(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        data = static_cast<float*>(fftwf_malloc(sizeof(float) * total));
    }

    ~GrayscaleImage() {
        if (data) fftwf_free(data);
    }

    GrayscaleImage(const GrayscaleImage&) = delete;
    GrayscaleImage& operator=(const GrayscaleImage&) = delete;
};

// Stores only the non-redundant half of the spectrum
class ComplexGrayscale {
public:
    int width;       // Original width
    int height;      // Original height
    int complex_w;   // Width in the frequency domain (width/2 + 1)
    fftwf_complex *data;

    ComplexGrayscale(int w, int h) : width(w), height(h) {
        complex_w = width / 2 + 1;
        size_t total = (size_t)height * complex_w;
        data = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~ComplexGrayscale() {
        if (data) fftwf_free(data);
    }

    ComplexGrayscale(const ComplexGrayscale&) = delete;
    ComplexGrayscale& operator=(const ComplexGrayscale&) = delete;
};

#endif