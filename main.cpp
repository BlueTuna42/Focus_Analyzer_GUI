#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"

class FocusCheckerApp {
private:
    const double focusConst = 0.12;
    FFTProcessor fftProcessor;

public:
    void processFile(const std::string& file, std::ofstream& log) {
        auto image = ImageIO::readImage(file);
        if (!image) {
            std::cerr << "Error reading image: " << file << std::endl;
            return;
        }

        // Grayscale Analysis
        auto s2 = std::chrono::high_resolution_clock::now();
        auto gray = ImageIO::convertToGrayscale(*image);
        auto fftG = fftProcessor.forwardFFT(*gray);
        fftProcessor.shift(*fftG);
        double erG = fftProcessor.energyRatio(*fftG);
        auto e2 = std::chrono::high_resolution_clock::now();

#ifdef DEBUG_BENCHMARK
        std::cout << "\nFile: " << file << std::endl;
        
        // RGB Analysis
        auto s1 = std::chrono::high_resolution_clock::now();
        auto fftRGB = fftProcessor.forwardFFT(*image);
        fftProcessor.shift(*fftRGB);
        double erRGB = fftProcessor.energyRatio(*fftRGB);
        auto e1 = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> t1 = e1 - s1, t2 = e2 - s2;
        std::cout << "  [RGB]  ER: " << erRGB << " | Time: " << t1.count() << "ms" << std::endl;
        std::cout << "  [GRAY] ER: " << erG   << " | Time: " << t2.count() << "ms" << std::endl;
        std::cout << "  Speedup: " << t1.count()/t2.count() << "x" << std::endl;
#endif
        if (erG < focusConst) {
            std::cout << file << " is blurry" << std::endl;
            XMPTools::writeXmpRating(file, 1);
            log << file << std::endl;
        } else {
            std::cout << file << " is sharp" << std::endl;
            XMPTools::writeXmpRating(file, 5);
        }
    }
};

int main(int argc, char** argv) {
    std::string dirpath;
    
    // Check if path is provided as argument or needs to be requested
    if (argc > 1) {
        dirpath = argv[1];
    } else {
        std::cout << "Enter directory path: ";
        if (!std::getline(std::cin, dirpath)) {
            std::cerr << "Error reading input." << std::endl;
            return 1;
        }
    }
    
    auto files = Scanner::scanBmpFiles(dirpath);
    std::cout << "Found " << files.size() << " image file(s):" << std::endl;

    std::ofstream log("BlurryList.txt");
    if (!log.is_open()) {
        std::cerr << "Failed to open BlurryList.txt for writing." << std::endl;
        return 1;
    }

    FocusCheckerApp app;

    for (const auto& f : files) {
        app.processFile(f, log);
    }
    
    log.close();
    return 0;
}