#include "bmp.h"
#include <iostream>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "Lib/stb_image.h"
#include <libraw/libraw.h>

std::unique_ptr<RGBImage> ImageIO::readImage(const std::string& filename, bool halfSize) {
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

        auto bmp = std::make_unique<RGBImage>(img->width, img->height);
        for (int i = 0; i < img->width * img->height; i++) {
            bmp->red[i][0] = img->data[i*3];     bmp->red[i][1] = 0;
            bmp->green[i][0] = img->data[i*3+1]; bmp->green[i][1] = 0;
            bmp->blue[i][0] = img->data[i*3+2];  bmp->blue[i][1] = 0;
        }
        libraw_dcraw_clear_mem(img);
        libraw_close(lr);
        return bmp;
    } else {
        // halfSize is ignored for non-RAW files
        int w, h, c;
        unsigned char *data = stbi_load(filename.c_str(), &w, &h, &c, 3);
        if (!data) return nullptr;
        auto bmp = std::make_unique<RGBImage>(w, h);
        for (int i = 0; i < w * h; i++) {
            bmp->red[i][0] = data[i*3];     bmp->red[i][1] = 0;
            bmp->green[i][0] = data[i*3+1]; bmp->green[i][1] = 0;
            bmp->blue[i][0] = data[i*3+2];  bmp->blue[i][1] = 0;
        }
        stbi_image_free(data);
        return bmp;
    }
}

std::unique_ptr<GrayscaleImage> ImageIO::convertToGrayscale(const RGBImage& rgbImg) {
    auto grayImg = std::make_unique<GrayscaleImage>(rgbImg.width, rgbImg.height);
    int total = rgbImg.width * rgbImg.height;
    for (int i = 0; i < total; ++i) {
        // Luminance: Y = 0.299*R + 0.587*G + 0.114*B
        grayImg->gray[i][0] = 0.299f * rgbImg.red[i][0] + 0.587f * rgbImg.green[i][0] + 0.114f * rgbImg.blue[i][0];
        grayImg->gray[i][1] = 0.0f;
    }
    return grayImg;
}