#ifndef GUI_H
#define GUI_H

#include <string>

class VisualGUI {
public:
    VisualGUI();
    ~VisualGUI();

    std::string SelectDirectory();
    void SetCurrentDirectory(const std::string& dirpath);
    void AddResult(const std::string& filename, bool isBlurry);
    void ResetProgress(int totalFiles);
    void UpdateProgress(int processedFiles, int totalFiles);
    void ShowFinished(int sharpFiles, int blurryFiles);
    bool IsClosed() const;
};

#endif
