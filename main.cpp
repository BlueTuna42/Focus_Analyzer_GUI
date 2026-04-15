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
    // Adjustable parameter for RAW processing
    bool useHalfSize = true;

    void processFile(const std::string& file, std::ofstream& log) {
#ifdef DEBUG_BENCHMARK
        std::cout << "\nFile: " << file << std::endl;

        // Benchmark Read Time (Full vs Half)
        auto ts1 = std::chrono::high_resolution_clock::now();
        auto imgFull = ImageIO::readImage(file, false);
        auto te1 = std::chrono::high_resolution_clock::now();

        auto ts2 = std::chrono::high_resolution_clock::now();
        auto imgHalf = ImageIO::readImage(file, true);
        auto te2 = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> tFull = te1 - ts1;
        std::chrono::duration<double, std::milli> tHalf = te2 - ts2;

        std::cout << "  [READ] Full size: " << tFull.count() << "ms | Half size: " << tHalf.count() << "ms" << std::endl;
        std::cout << "  [READ] Speedup: " << tFull.count() / (tHalf.count() > 0 ? tHalf.count() : 1) << "x" << std::endl;

        // Select the image memory depending on the config (move semantics prevent deep copies)
        auto image = useHalfSize ? std::move(imgHalf) : std::move(imgFull);
        if (!image) {
            std::cerr << "Error reading image: " << file << std::endl;
            return;
        }

        // RGB Analysis (only for benchmarking when DEBUG_BENCHMARK is defined)
        auto s1 = std::chrono::high_resolution_clock::now();
        auto fftRGB = fftProcessor.forwardFFT(*image);
        fftProcessor.shift(*fftRGB);
        double erRGB = fftProcessor.energyRatio(*fftRGB);
        auto e1 = std::chrono::high_resolution_clock::now();
#else
        // Standard production run without extra I/O overhead
        auto image = ImageIO::readImage(file, useHalfSize);
        if (!image) {
            std::cerr << "Error reading image: " << file << std::endl;
            return;
        }
#endif

        // Grayscale Analysis (Optimized - Always runs)
        auto s2 = std::chrono::high_resolution_clock::now();
        auto gray = ImageIO::convertToGrayscale(*image);
        auto fftG = fftProcessor.forwardFFT(*gray);
        fftProcessor.shift(*fftG);
        double erG = fftProcessor.energyRatio(*fftG);
        auto e2 = std::chrono::high_resolution_clock::now();

#ifdef DEBUG_BENCHMARK
        std::chrono::duration<double, std::milli> t1 = e1 - s1, t2 = e2 - s2;
        std::cout << "  [FFT]  RGB: " << t1.count() << "ms | GRAY: " << t2.count() << "ms" << std::endl;
        std::cout << "  [FFT]  Speedup: " << t1.count()/t2.count() << "x" << std::endl;
        std::cout << "  [DATA] ER RGB: " << erRGB << " | ER GRAY: " << erG << std::endl;
#endif

        // Standard logic: sharp or blurry decision
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
    
    auto files = Scanner::scanFiles(dirpath);
    std::cout << "Found " << files.size() << " image file(s):" << std::endl;

    std::ofstream log("BlurryList.txt");
    if (!log.is_open()) {
        std::cerr << "Failed to open BlurryList.txt for writing." << std::endl;
        return 1;
    }

    FocusCheckerApp app;
    // You can modify this variable programmatically or via arguments later
    app.useHalfSize = true; 

    for (const auto& f : files) {
        app.processFile(f, log);
    }
    
    log.close();
    return 0;
}