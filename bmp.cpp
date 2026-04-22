#include "bmp.h"
#include <iostream>
#include <cstring>
#include <future>

#define STB_IMAGE_IMPLEMENTATION
#include "Lib/stb_image.h"
#include <libraw/libraw.h>

std::unique_ptr<GrayscaleImage> ImageIO::readImage(const std::string& filename, bool halfSize) {
    size_t extPos = filename.find_last_of('.');
    std::string ext = (extPos == std::string::npos) ? "" : filename.substr(extPos);
    
    bool isRaw = (strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
                  strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
                  strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0);

    if (isRaw) {
        libraw_data_t *lr = libraw_init(0);

        if (libraw_open_file(lr, filename.c_str()) != LIBRAW_SUCCESS) return nullptr;
        // half size optimization
        if (halfSize) {
             lr->params.half_size = 1;
        }

        libraw_unpack(lr);
        libraw_dcraw_process(lr);
        int err = 0;
        libraw_processed_image_t *img = libraw_dcraw_make_mem_image(lr, &err);
        if (!img) { libraw_close(lr); return nullptr; }

        auto grayImg = std::make_unique<GrayscaleImage>(img->width, img->height);
        int total = img->width * img->height;

        auto process_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                grayImg->data[i] = 0.299f * img->data[i*3] + 
                                   0.587f * img->data[i*3+1] + 
                                   0.114f * img->data[i*3+2];
            }
        };

        int mid = total / 2;
        auto f1 = std::async(std::launch::async, process_chunk, 0, mid);
        process_chunk(mid, total);
        f1.wait();

        libraw_dcraw_clear_mem(img);
        libraw_close(lr);
        return grayImg;
    } else {
        int w, h, c;
        unsigned char *data = stbi_load(filename.c_str(), &w, &h, &c, 3);
        if (!data) return nullptr;
        
        auto grayImg = std::make_unique<GrayscaleImage>(w, h);
        int total = w * h;

        auto process_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                grayImg->data[i] = 0.299f * data[i*3] + 
                                   0.587f * data[i*3+1] + 
                                   0.114f * data[i*3+2];
            }
        };

        int mid = total / 2;
        auto f1 = std::async(std::launch::async, process_chunk, 0, mid);
        process_chunk(mid, total);
        f1.wait();

        stbi_image_free(data);
        return grayImg;
    }
}