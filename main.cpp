#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>
#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"
#include "gui.h"

class FocusCheckerApp {
private:
    const double focusConst = 0.0004;
    FFTProcessor fftProcessor;
    std::mutex output_mutex; // Mutex to prevent console and log file text mixing

public:
    bool useHalfSize = true; // Adjustable parameter for RAW processing

    bool processDirectory(const std::string& dirpath, VisualGUI& gui) {
        auto files = Scanner::scanFiles(dirpath);
        std::cout << "Found " << files.size() << " image file(s):" << std::endl;
        const int totalFiles = static_cast<int>(files.size());
        std::atomic<int> processedFiles{0};
        std::atomic<int> sharpFiles{0};
        std::atomic<int> blurryFiles{0};
        gui.SetCurrentDirectory(dirpath);
        gui.ResetProgress(totalFiles);

        std::ofstream log("BlurryList.txt");
        if (!log.is_open()) {
            std::cerr << "Failed to open BlurryList.txt for writing." << std::endl;
            return false;
        }

#ifdef DEBUG_BENCHMARK
        auto ts_total_start = std::chrono::high_resolution_clock::now();
#endif

        // Vector to store futures for parallel execution
        std::vector<std::future<void>> futures;

        // Launch processing
        for (const auto& f : files) {
            futures.push_back(std::async(std::launch::async, [this, &log, &gui, &processedFiles, &sharpFiles, &blurryFiles, totalFiles, f]() {
                const int result = processFile(f, log, gui);
                if (result == 0) {
                    ++sharpFiles;
                } else if (result == 1) {
                    ++blurryFiles;
                }
                const int current = ++processedFiles;
                gui.UpdateProgress(current, totalFiles);
            }));
        }

        // Wait for all processing threads to finish
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
        gui.ShowFinished(sharpFiles.load(), blurryFiles.load());
        return true;
    }

    int processFile(const std::string& file, std::ofstream& log, VisualGUI& gui) {
#ifdef DEBUG_BENCHMARK
        auto ts_read = std::chrono::high_resolution_clock::now();
#endif
        auto image = ImageIO::readImage(file, useHalfSize);
        if (!image) return -1;

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

        gui.AddResult(file, isBlurry);

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

        return isBlurry ? 1 : 0;
    }
};

int main(int argc, char** argv) {
    std::string dirpath;
    VisualGUI gui;
    FocusCheckerApp app;
    app.useHalfSize = true;
    
    if (argc > 1) {
        dirpath = argv[1];
        if (dirpath.empty()) {
            std::cerr << "No directory selected." << std::endl;
            return 1;
        }

        return app.processDirectory(dirpath, gui) ? 0 : 1;
    } else {
        while (!gui.IsClosed()) {
            std::cout << "Waiting for directory selection in GUI..." << std::endl;
            dirpath = gui.SelectDirectory();

            if (dirpath.empty()) {
                break;
            }

            if (!app.processDirectory(dirpath, gui)) {
                return 1;
            }
        }
    }
    
    return 0;
}
