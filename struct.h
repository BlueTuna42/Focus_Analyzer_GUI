#ifndef STRUCT_H 
#define STRUCT_H

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fftw3.h>

#define PATH_MAX 256

// BMP Header structures for file compatibility
#pragma pack(push, 1)
struct BITMAPFILEHEADER {
    uint16_t bfType;      // BM identifier (0x4D42)
    uint32_t bfSize;      // File size
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;   // Offset to image data
};

struct BITMAPINFOHEADER {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

// Class for storing RGB image data in floating point (complex) format
class RGBImage {
public:
    int width;
    int height;
    fftwf_complex *red;
    fftwf_complex *green;
    fftwf_complex *blue;

    RGBImage(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        red = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        green = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        blue = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~RGBImage() {
        if (red) fftwf_free(red);
        if (green) fftwf_free(green);
        if (blue) fftwf_free(blue);
    }

    // Prevent copying to avoid double-freeing pointers
    RGBImage(const RGBImage&) = delete;
    RGBImage& operator=(const RGBImage&) = delete;
};

// Class for storing the Fourier spectra of an RGB image
class ComplexRGB {
public:
    int width;
    int height;
    fftwf_complex *red;
    fftwf_complex *green;
    fftwf_complex *blue;

    ComplexRGB(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        red = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        green = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
        blue = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~ComplexRGB() {
        if (red) fftwf_free(red);
        if (green) fftwf_free(green);
        if (blue) fftwf_free(blue);
    }

    ComplexRGB(const ComplexRGB&) = delete;
    ComplexRGB& operator=(const ComplexRGB&) = delete;
};

// Class for storing single-channel (Grayscale) image data
class GrayscaleImage {
public:
    int width;
    int height;
    fftwf_complex *gray;

    GrayscaleImage(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        gray = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~GrayscaleImage() {
        if (gray) fftwf_free(gray);
    }

    GrayscaleImage(const GrayscaleImage&) = delete;
    GrayscaleImage& operator=(const GrayscaleImage&) = delete;
};

// Class for storing the Fourier spectra of a Grayscale image
class ComplexGrayscale {
public:
    int width;
    int height;
    fftwf_complex *gray;

    ComplexGrayscale(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        gray = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * total));
    }

    ~ComplexGrayscale() {
        if (gray) fftwf_free(gray);
    }

    ComplexGrayscale(const ComplexGrayscale&) = delete;
    ComplexGrayscale& operator=(const ComplexGrayscale&) = delete;
};

#endif