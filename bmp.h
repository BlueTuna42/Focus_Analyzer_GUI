#ifndef BMP_H 
#define BMP_H

#include "struct.h"
#include <string>
#include <memory>

class ImageIO {
public:
    static std::unique_ptr<RGBImage> readImage(const std::string& filename, bool halfSize = false);
    static std::unique_ptr<GrayscaleImage> convertToGrayscale(const RGBImage& rgbImg);
};

#endif