#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <future>
#include <mutex>
#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"

class FocusCheckerApp {
private:
    const double focusConst = 0.0004;
    FFTProcessor fftProcessor;
    std::mutex output_mutex; // Mutex to prevent console and log file text mixing

public:
    bool useHalfSize = true; // Adjustable parameter for RAW processing

    void processFile(const std::string& file, std::ofstream& log) {
#ifdef DEBUG_BENCHMARK
        auto ts_read = std::chrono::high_resolution_clock::now();
#endif
        auto image = ImageIO::readImage(file, useHalfSize);
        if (!image) return;

#ifdef DEBUG_BENCHMARK
        auto te_read = std::chrono::high_resolution_clock::now();
        auto ts_fft = std::chrono::high_resolution_clock::now();
#endif
        auto fftG = fftProcessor.forwardFFT(*image);

#ifdef DEBUG_BENCHMARK
        auto te_fft = std::chrono::high_resolution_clock::now();
        auto ts_ratio = std::chrono::high_resolution_clock::now();
#endif

        double erG = fftProcessor.energyRatio(*fftG);

#ifdef DEBUG_BENCHMARK
        auto te_ratio = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> t_read = te_read - ts_read;
        std::chrono::duration<double, std::milli> t_fft = te_fft - ts_fft;
        std::chrono::duration<double, std::milli> t_ratio = te_ratio - ts_ratio;
#endif

        bool isBlurry = (erG < focusConst);
        if (isBlurry) {
            XMPTools::writeXmpRating(file, 1);
        } else {
            XMPTools::writeXmpRating(file, 5);
        }

        // Lock the mutex before writing to the console and log file to prevent scrambling mixing up
        std::lock_guard<std::mutex> lock(output_mutex);

#ifdef DEBUG_BENCHMARK
        std::cout << "\nFile: " << file << std::endl;
        std::cout << "  [READ]    Time: " << t_read.count() << " ms" << std::endl;
        std::cout << "  [FFT R2C] Time: " << t_fft.count() << " ms" << std::endl;
        std::cout << "  [RATIO]   Time: " << t_ratio.count() << " ms" << std::endl;
        std::cout << "  [DATA]    ER:   " << erG << std::endl;
#endif

        if (isBlurry) {
#ifndef DEBUG_BENCHMARK
            std::cout << file << " is blurry" << std::endl;
#endif
            log << file << std::endl;
        } else {
#ifndef DEBUG_BENCHMARK
            std::cout << file << " is sharp" << std::endl;
#endif
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
    app.useHalfSize = true; 

#ifdef DEBUG_BENCHMARK
    auto ts_total_start = std::chrono::high_resolution_clock::now();
#endif

    // Vector to store futures for parallel execution
    std::vector<std::future<void>> futures;

    // Launch processing
    for (const auto& f : files) {
        futures.push_back(std::async(std::launch::async, [&app, &log, f]() {
            app.processFile(f, log);
        }));
    }

    // Wait for all processЫЫЫing threads to finish
    for (auto& fut : futures) {
        fut.wait();
    }
    
#ifdef DEBUG_BENCHMARK
    auto ts_total_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> total_time = ts_total_end - ts_total_start;
    std::cout << "\n========================================" << std::endl;
    std::cout << "  [TOTAL] Directory processing time: " << total_time.count() << " ms" << std::endl;
    std::cout << "========================================" << std::endl;
#endif

    log.close();
    return 0;
}