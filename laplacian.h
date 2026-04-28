#ifndef LAPLACIAN_H 
#define LAPLACIAN_H

#include "struct.h"

class LaplacianProcessor {
public:
    // Calculates the Laplacian variance for a specific image block
    static double blockVariance(const GrayscaleImage& img, int startX, int startY, int blockW, int blockH);

    // Evaluates sharpness using a grid (default 5x5) and returns the maximum block variance
    static double evaluateSharpness(const GrayscaleImage& img, int gridCols = 5, int gridRows = 5);
};

#endif