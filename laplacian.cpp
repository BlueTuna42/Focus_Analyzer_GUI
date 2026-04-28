#include "laplacian.h"
#include <algorithm>

double LaplacianProcessor::blockVariance(const GrayscaleImage& img, int startX, int startY, int blockW, int blockH) {
    if (blockW <= 2 || blockH <= 2) return 0.0;

    double sum = 0.0;
    double sq_sum = 0.0;
    int count = (blockW - 2) * (blockH - 2); 
    
    // Laplacian kernel:
    //  0  1  0
    //  1 -4  1
    //  0  1  0

    for (int y = startY + 1; y < startY + blockH - 1; ++y) {
        int rowOffset = y * img.width;
        int rowAbove = (y - 1) * img.width;
        int rowBelow = (y + 1) * img.width;

        for (int x = startX + 1; x < startX + blockW - 1; ++x) {
            float top    = img.data[rowAbove + x];
            float bottom = img.data[rowBelow + x];
            float left   = img.data[rowOffset + (x - 1)];
            float right  = img.data[rowOffset + (x + 1)];
            float center = img.data[rowOffset + x];

            double laplacian = top + bottom + left + right - 4.0 * center;
            
            sum += laplacian;
            sq_sum += laplacian * laplacian;
        }
    }

    double mean = sum / count;
    double variance = (sq_sum / count) - (mean * mean);
    
    return variance;
}

double LaplacianProcessor::evaluateSharpness(const GrayscaleImage& img, int gridCols, int gridRows) {
    int blockW = img.width / gridCols;
    int blockH = img.height / gridRows;
    
    double maxVar = 0.0;

    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridCols; ++c) {
            int startX = c * blockW;
            int startY = r * blockH;
            
            int curW = (c == gridCols - 1) ? (img.width - startX) : blockW;
            int curH = (r == gridRows - 1) ? (img.height - startY) : blockH;

            double var = blockVariance(img, startX, startY, curW, curH);
            maxVar = std::max(maxVar, var);
        }
    }

    return maxVar;
}