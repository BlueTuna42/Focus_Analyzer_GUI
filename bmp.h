#ifndef BMP_H 
#define BMP_H

#include "struct.h"
#include <string>
#include <memory>

// Class for image input/output operations
class ImageIO {
private:
    static bool isRaw(const std::string& filename);
    static std::unique_ptr<RGBImage> readCompressed(const std::string& filename);
    static std::unique_ptr<RGBImage> readRaw(const std::string& filename);

public:
    static std::unique_ptr<RGBImage> readImage(const std::string& filename);
    static bool writeJpg(const ComplexRGB& img, const std::string& filename, int quality);
    
    // Method to non-destructively convert RGB image to Grayscale
    static std::unique_ptr<GrayscaleImage> convertToGrayscale(const RGBImage& rgbImg);
};

#endif