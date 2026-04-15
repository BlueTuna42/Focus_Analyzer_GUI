#include "bmp.h"
#include <cstring>
#include <strings.h>

#define STB_IMAGE_IMPLEMENTATION
#include "Lib/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Lib/stb_image_write.h"
#include <libraw/libraw.h>

bool ImageIO::isRaw(const std::string& filename) {
    size_t extPos = filename.find_last_of('.');
    if (extPos == std::string::npos) return false;
    
    std::string ext = filename.substr(extPos);
    return strcasecmp(ext.c_str(), ".CR2") == 0
        || strcasecmp(ext.c_str(), ".NEF") == 0
        || strcasecmp(ext.c_str(), ".ARW") == 0
        || strcasecmp(ext.c_str(), ".RW2") == 0
        || strcasecmp(ext.c_str(), ".RAF") == 0
        || strcasecmp(ext.c_str(), ".DNG") == 0;
}

std::unique_ptr<RGBImage> ImageIO::readCompressed(const std::string& filename) {
    int width, height, channels;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &channels, 3);
    
    if (!data) {
        std::cerr << "Failed to load image " << filename << ": " << stbi_failure_reason() << std::endl;
        return nullptr;
    }
    
    auto bmp = std::make_unique<RGBImage>(width, height);
    int total = width * height;
    
    for (int i = 0; i < total; ++i) {
        bmp->red[i][0]   = static_cast<float>(data[3 * i]);
        bmp->red[i][1]   = 0.0f;
        
        bmp->green[i][0] = static_cast<float>(data[3 * i + 1]);
        bmp->green[i][1] = 0.0f;
        
        bmp->blue[i][0]  = static_cast<float>(data[3 * i + 2]);
        bmp->blue[i][1]  = 0.0f;
    }
    
    stbi_image_free(data);
    return bmp;
}

std::unique_ptr<RGBImage> ImageIO::readRaw(const std::string& filename) {
    libraw_data_t *lr = libraw_init(0);
    if (!lr) {
        std::cerr << "libraw_init() initialization error" << std::endl;
        return nullptr;
    }

    if (libraw_open_file(lr, filename.c_str()) != LIBRAW_SUCCESS) {
        std::cerr << "libraw_open_file(" << filename << ") error" << std::endl;
        libraw_close(lr);
        return nullptr;
    }
    if (libraw_unpack(lr) != LIBRAW_SUCCESS) {
        std::cerr << "libraw_unpack() error" << std::endl;
        libraw_close(lr);
        return nullptr;
    }
    if (libraw_dcraw_process(lr) != LIBRAW_SUCCESS) {
        std::cerr << "libraw_dcraw_process() error" << std::endl;
        libraw_close(lr);
        return nullptr;
    }

    int errc = 0;
    libraw_processed_image_t *img = libraw_dcraw_make_mem_image(lr, &errc); 
    if (!img || errc != LIBRAW_SUCCESS) {
        std::cerr << "libraw_dcraw_make_mem_image() error " << errc << std::endl;
        libraw_close(lr);
        return nullptr;
    }

    auto bmp = std::make_unique<RGBImage>(img->width, img->height);

    int colors = img->colors;
    unsigned char *data = img->data;
    
    for (int y = 0; y < bmp->height; y++) {
        for (int x = 0; x < bmp->width; x++) {
            int rgbIdx = y * bmp->width + x;
            int idx = rgbIdx * colors;
            
            bmp->red[rgbIdx][0]   = static_cast<float>(data[idx + 0]);
            bmp->red[rgbIdx][1]   = 0.0f;
            
            bmp->green[rgbIdx][0] = static_cast<float>(data[idx + 1]);
            bmp->green[rgbIdx][1] = 0.0f;
            
            bmp->blue[rgbIdx][0]  = static_cast<float>(data[idx + 2]);
            bmp->blue[rgbIdx][1]  = 0.0f;
        }
    }

    libraw_dcraw_clear_mem(img);
    libraw_close(lr);
    return bmp;
}

std::unique_ptr<GrayscaleImage> ImageIO::convertToGrayscale(const RGBImage& rgbImg) {
    auto grayImg = std::make_unique<GrayscaleImage>(rgbImg.width, rgbImg.height);
    int total = rgbImg.width * rgbImg.height;
    
    for (int i = 0; i < total; ++i) {
        // Luminance: Y = 0.299*R + 0.587*G + 0.114*B
        grayImg->gray[i][0] = 0.299f * rgbImg.red[i][0] + 
                              0.587f * rgbImg.green[i][0] + 
                              0.114f * rgbImg.blue[i][0];
        grayImg->gray[i][1] = 0.0f;
    }
    
    return grayImg;
}

std::unique_ptr<RGBImage> ImageIO::readImage(const std::string& filename) {
    if (isRaw(filename)) {
        return readRaw(filename);
    } else {
        return readCompressed(filename);
    }
}

bool ImageIO::writeJpg(const ComplexRGB& img, const std::string& filename, int quality) {
    int N = img.width * img.height;
    std::vector<unsigned char> buffer(3 * N);

    // Global max for normalization
    float max_val = 0.0f;
    for (int i = 0; i < N; i++) {
        float mr = std::log(1.0f + std::hypot(img.red[i][0], img.red[i][1]));
        float mg = std::log(1.0f + std::hypot(img.green[i][0], img.green[i][1]));
        float mb = std::log(1.0f + std::hypot(img.blue[i][0], img.blue[i][1]));
        
        if (mr > max_val) max_val = mr;
        if (mg > max_val) max_val = mg;
        if (mb > max_val) max_val = mb;
    }

    float scale = (max_val > 0 ? 255.0f / max_val : 0.0f);

    // Fill RGB buffer
    for (int i = 0; i < N; i++) {
        buffer[3*i + 0] = static_cast<unsigned char>(std::log(1.0f + std::hypot(img.red[i][0], img.red[i][1])) * scale);
        buffer[3*i + 1] = static_cast<unsigned char>(std::log(1.0f + std::hypot(img.green[i][0], img.green[i][1])) * scale);
        buffer[3*i + 2] = static_cast<unsigned char>(std::log(1.0f + std::hypot(img.blue[i][0], img.blue[i][1])) * scale);
    }  

    // Flip vertically
    stbi_flip_vertically_on_write(1);

    int ret = stbi_write_jpg(filename.c_str(), img.width, img.height, 3, buffer.data(), quality);

    return ret != 0;
}