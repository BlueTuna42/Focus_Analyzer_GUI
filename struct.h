#ifndef STRUCT_H 
#define STRUCT_H

#include <iostream>
#include <vector>
#include <string>

#define PATH_MAX 256

class GrayscaleImage {
public:
    int width;
    int height;
    float *data;

    GrayscaleImage(int w, int h) : width(w), height(h) {
        size_t total = (size_t)width * height;
        data = new float[total]();
    }

    ~GrayscaleImage() {
        if (data) delete[] data;
    }

    GrayscaleImage(const GrayscaleImage&) = delete;
    GrayscaleImage& operator=(const GrayscaleImage&) = delete;
};

#endif