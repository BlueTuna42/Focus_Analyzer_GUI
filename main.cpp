#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <future>

#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"
#include "tinyfiledialogs.h"

namespace fs = std::filesystem;

//colors
namespace Theme {
    const sf::Color Bg = sf::Color(30, 30, 30);
    const sf::Color Sidebar = sf::Color(45, 45, 45);
    const sf::Color Text = sf::Color(220, 220, 220);
    const sf::Color Accent = sf::Color(0, 122, 204);
    const sf::Color AccentHover = sf::Color(30, 140, 220);
    const sf::Color Sharp = sf::Color(50, 200, 50);
    const sf::Color Blurry = sf::Color(220, 50, 50);
}

//button
struct StyledButton {
    sf::RectangleShape rect;
    sf::Text text;
    bool isHovered = false;

    StyledButton(const std::string& label, sf::Font& font, sf::Vector2f pos) {
        text.setFont(font);
        text.setString(label);
        text.setCharacterSize(18);
        text.setFillColor(sf::Color::White);

        sf::FloatRect tr = text.getLocalBounds();
        rect.setSize({200, 45});
        rect.setFillColor(Theme::Accent);
        rect.setPosition(pos);

        text.setPosition(pos.x + (200 - tr.width) / 2.0f,
                         pos.y + (45 - tr.height) / 2.0f - 5);
    }

    void update(const sf::Vector2i& mousePos) {
        isHovered = rect.getGlobalBounds().contains((float)mousePos.x, (float)mousePos.y);
        rect.setFillColor(isHovered ? Theme::AccentHover : Theme::Accent);
    }

    bool isClicked(sf::Event& event) {
        return isHovered &&
               event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left;
    }

    void draw(sf::RenderWindow& window) {
        window.draw(rect);
        window.draw(text);
    }
};

//analyzing process
class FocusCheckerApp {
private:
    const double focusConst = 0.0004;
    FFTProcessor fftProcessor;

public:
    bool useHalfSize = true;

    bool checkFocus(const std::string& f) {
        auto img = ImageIO::readImage(f, useHalfSize);
        if (!img) return false;

        auto fft = fftProcessor.forwardFFT(*img);

        double ER = fftProcessor.energyRatio(*fft);

        bool isSharp = ER >= focusConst;
        XMPTools::writeXmpRating(f, isSharp ? 5 : 1);

        return isSharp;
    }
};

//analysis chart
void drawAnalysisChart(sf::RenderWindow& window, sf::Vector2f pos,
                       int sharp, int blurry, sf::Font& font) {

    float total = (float)sharp + blurry;
    float percentage = (total > 0) ? (sharp / total) : 0.0f;

    float radius = 60.f;
    float thickness = 12.f;

    sf::CircleShape background(radius);
    background.setOrigin(radius, radius);
    background.setPosition(pos);
    background.setFillColor(sf::Color::Transparent);
    background.setOutlineThickness(-thickness);
    background.setOutlineColor(Theme::Blurry);
    window.draw(background);

    if (total > 0 && sharp > 0) {
        sf::VertexArray sector(sf::TriangleFan, 40);
        sector[0].position = pos;
        sector[0].color = Theme::Sharp;

        float angleLimit = percentage * 360.f;

        for (int i = 1; i < 40; ++i) {
            float angle = (i - 1) * angleLimit / 38.f - 90.f;
            float rad = angle * 3.14159f / 180.f;

            sector[i].position = pos + sf::Vector2f(std::cos(rad) * radius,
                                                    std::sin(rad) * radius);
            sector[i].color = Theme::Sharp;
        }

        window.draw(sector);

        sf::CircleShape mask(radius - thickness);
        mask.setOrigin(radius - thickness, radius - thickness);
        mask.setPosition(pos);
        mask.setFillColor(Theme::Bg);
        window.draw(mask);
    }

    sf::Text pctText(std::to_string((int)(percentage * 100)) + "%", font, 20);
    pctText.setFillColor(sf::Color::White);

    sf::FloatRect b = pctText.getLocalBounds();
    pctText.setOrigin(b.left + b.width / 2.0f, b.top + b.height / 2.0f);
    pctText.setPosition(pos);

    window.draw(pctText);
}

int main() {
    sf::RenderWindow window(sf::VideoMode(900, 600), "Focus Analyzer");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) {
        std::cerr << "arial.ttf not found\n";
        return 1;
    }

    sf::RectangleShape sidebar(sf::Vector2f(250, 600));
    sidebar.setFillColor(Theme::Sidebar);

    StyledButton analyzeButton("Analyze Folder", font, {25, 30});
    FocusCheckerApp app;

    struct Result {
        std::string name;
        bool sharp;
        sf::Texture texture;
    };
    std::vector<Result> results;

    //progress bar (WIP)
    float progress = 0;
    size_t processed = 0, total = 0;
    bool analyzing = false;

    //scroll
    float scrollOffset = 0.f;
    float maxScroll = 0.f;

    while (window.isOpen()) {
        sf::Event event;
        sf::Vector2i mouse = sf::Mouse::getPosition(window);

        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            
            if (event.type == sf::Event::MouseWheelScrolled)
            {
                if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel)
                {
                    scrollOffset -= event.mouseWheelScroll.delta * 40.f;

                    if (scrollOffset < 0.f) scrollOffset = 0.f;
                    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
                }
            }

            if (analyzeButton.isClicked(event)) {
                const char* path = tinyfd_selectFolderDialog("Select Folder", "");
                if (!path) continue;

                auto files = Scanner::scanFiles(path);
                total = files.size();
                if (!total) continue;

                results.clear();
                processed = 0;
                analyzing = true;

                std::vector<std::future<bool>> futures;

                for (const auto& f : files) {
                    futures.push_back(std::async(std::launch::async,
                        [&app, f]() { return app.checkFocus(f); }));
                }

                for (size_t i = 0; i < futures.size(); ++i) {
                bool res = futures[i].get();

                Result r;
                r.name = fs::path(files[i]).filename().string();
                r.sharp = res;

                sf::Image img;
                if (!img.loadFromFile(files[i]))
                {
                    processed++;
                    progress = (float)processed / total;
                    continue;
                }

                //crop to square
                unsigned w = img.getSize().x;
                unsigned h = img.getSize().y;
                unsigned minSide = std::min(w, h);

                unsigned x = (w - minSide) / 2;
                unsigned y = (h - minSide) / 2;

                sf::Image cropped;
                cropped.create(minSide, minSide);
                cropped.copy(img, 0, 0, sf::IntRect(x, y, minSide, minSide));

                img = std::move(cropped);

                r.texture.loadFromImage(img);

                results.push_back(std::move(r));

                processed++;
                progress = (float)processed / total;
            }

                analyzing = false;
            }
        }

        analyzeButton.update(mouse);

        window.clear(Theme::Bg);
        window.draw(sidebar);
        analyzeButton.draw(window);

        if (!results.empty()) {
            int sharp = 0;
            for (auto& r : results) if (r.sharp) sharp++;

            drawAnalysisChart(window, {130, 150},
                              sharp, results.size() - sharp, font);
        }

        float START_X = 280.f;
        float START_Y = 40.f;

        float availableWidth = window.getSize().x - START_X - 20.f;

        int COLS = std::max(1, (int)(availableWidth / 160.f));

        float SIZE = (availableWidth - (COLS - 1) * 10.f) / COLS;

        float totalGridWidth = COLS * SIZE + (COLS - 1) * 10.f;
        float offsetX = START_X + (availableWidth - totalGridWidth) / 2.f;

        int rows = (results.size() + COLS - 1) / COLS;
        float contentHeight = rows * (SIZE + 10.f);

        float visibleHeight = window.getSize().y;

        maxScroll = std::max(0.f, contentHeight - (window.getSize().y - START_Y));

        sf::View view = window.getDefaultView();

        float viewWidth = window.getSize().x - START_X;
        float viewHeight = window.getSize().y;

    //    view.setViewport(sf::FloatRect(
    //        START_X / window.getSize().x,
    //        0.f,
    //        viewWidth / window.getSize().x,
    //        1.f
    //    ));

        view.setCenter(
            window.getSize().x / 2.f,
            window.getSize().y / 2.f + scrollOffset
        );

        window.setView(view);

        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].texture.getSize().x == 0) continue;

            int row = i / COLS;
            int col = i % COLS;

            float x = offsetX + col * (SIZE + 10);
            float y = START_Y + row * (SIZE + 10);

            sf::Sprite sprite(results[i].texture);

            auto texSize = results[i].texture.getSize();
            float scale = texSize.x > 0 ? SIZE / texSize.x : 1.f;

            sprite.setScale(scale, scale);
            sprite.setPosition(x, y);

            window.draw(sprite);

            sf::RectangleShape border({SIZE, SIZE});
            border.setPosition(x, y);
            border.setFillColor(sf::Color::Transparent);
            border.setOutlineThickness(3.f);
            border.setOutlineColor(results[i].sharp ? Theme::Sharp : Theme::Blurry);

            window.draw(border);
        }

        window.setView(window.getDefaultView());

        window.display();
    }
}