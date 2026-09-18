#pragma once
#include <string>
#include <vector>
#include <map>

struct GLFWwindow;
class Texture_L;

struct MenuChapter {
    std::string name;
    std::string mapName;
    std::string image;
    int number = 0;
};

enum class MenuResult {
    None,
    Resume,
    StartGame,
    MainMenu,
    Quit
};

class Menu_L {
public:
    Menu_L() = default;
    ~Menu_L();

    bool init(GLFWwindow* window, Texture_L* fileSystem,
              const std::string& gameDirectory, const std::string& gameTitle);
    void shutdown();

    MenuResult runMainMenu(GLFWwindow* window);
    MenuResult runPauseMenu(GLFWwindow* window);
    void captureCurrentFrame(int width, int height);
    void renderConsoleOverlay(int width, int height);
    void handleConsoleOverlayInput(GLFWwindow* window, int width, int height);

    const std::string& selectedMap() const { return selectedMapName; }
    const std::vector<MenuChapter>& getChapters() const { return chapters; }

private:
    enum class DialogKind { None, Chapters, Empty };

    struct TextureRef {
        unsigned int id = 0;
        int width = 1;
        int height = 1;
        bool owned = false;
    };

    struct Font {
        unsigned int listBase = 0;
        void* hfont = nullptr;
        int pixelHeight = 16;
        bool valid = false;
    };

    GLFWwindow* ownerWindow = nullptr;
    Texture_L* fileSystem = nullptr;
    std::string gameDirectory;
    std::string title;

    std::vector<MenuChapter> chapters;
    std::vector<TextureRef> backgroundTextures;
    std::vector<TextureRef> chapterTextures;
    TextureRef whiteTexture;

    unsigned int pauseFrameTexture = 0;
    int pauseFrameWidth = 0;
    int pauseFrameHeight = 0;

    Font fontTitle;
    Font fontMenu;
    Font fontSmall;
    Font fontTiny;
    bool fontsReady = false;
    bool resourcesActive = false;

    std::string selectedMapName;
    size_t selectedChapter = 0;
    size_t chapterPageStart = 0;
    int mainSelection = 0;

    bool pauseMode = false;
    DialogKind dialog = DialogKind::None;
    std::string dialogTitle;
    float dialogX = 0.0f;
    float dialogY = 0.0f;
    float dialogW = 0.0f;
    float dialogH = 0.0f;
    bool draggingDialog = false;
    bool consoleDragging = false;
    bool consoleResizing = false;
    bool consoleMouseWasDown = false;
    float consoleX = 42.0f;
    float consoleY = 42.0f;
    float consoleW = 760.0f;
    float consoleH = 500.0f;
    float consoleDragOffsetX = 0.0f;
    float consoleDragOffsetY = 0.0f;
    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;

    int pressedControl = -1;
    bool mouseWasDown = false;
    bool upWasDown = false;
    bool downWasDown = false;
    bool leftWasDown = false;
    bool rightWasDown = false;
    bool enterWasDown = false;
    bool escapeWasDown = false;

    double backgroundTime = 0.0;
    int backgroundIndex = 0;
    int pressedFrame = 0;

    void loadContent();
    void loadChapters();
    void loadBackgrounds();
    TextureRef loadTextureCandidates(const std::vector<std::string>& candidates);
    TextureRef loadRawImage(const std::string& relativePath);
    TextureRef loadMaterialTexture(const std::string& materialBase);
    bool fileExists(const std::string& relativePath) const;
    void scanTextureFiles(const std::string& keyword, std::vector<std::string>& outCandidates) const;
    void scanDiskTextureFiles(const std::string& keyword, std::vector<std::string>& outCandidates) const;

    bool initFonts();
    void destroyFonts();
    void destroyOwnedTextures();
    TextureRef createWhiteTexture();
    unsigned int resolveChapterTexture(size_t index);

    void begin2D(int width, int height, bool clear);
    void end2D();
    void drawTexture(const TextureRef& tex, float x, float y, float w, float h,
                     float alpha = 1.0f, bool crop = true, bool sourceBottomUp = false);
    void drawRect(float x, float y, float w, float h,
                  float r, float g, float b, float alpha);
    void drawRoundedRect(float x, float y, float w, float h, float radius,
                         float r, float g, float b, float alpha);
    void drawLine(float x1, float y1, float x2, float y2, float width, float alpha);
    void drawText(const std::string& text, float x, float y, const Font& font,
                  float r, float g, float b, float alpha = 1.0f);
    float textWidth(const std::string& text, const Font& font) const;

    void renderMainMenu(int width, int height);
    void renderDialog(int width, int height);
    void renderChapterSelector(int width, int height);
    void renderEmptyDialog(int width, int height);
    void renderPauseFrame(int width, int height);

    bool handleMenuInput(GLFWwindow* window, int width, int height, MenuResult& result);
    bool handleDialogInput(GLFWwindow* window, int width, int height, MenuResult& result);

    void openChapterSelector(int width, int height);
    void openEmptyDialog(const std::string& titleText, int width, int height);
    void closeDialog();
    void selectCurrentChapter();
    void centerDialog(int width, int height);
    void clampDialog(int width, int height);

    int hitMainControl(float mx, float my, int width, int height) const;
    int hitDialogControl(float mx, float my, int width, int height) const;
    bool pointIn(float px, float py, float x, float y, float w, float h) const;
    bool isKeyPressed(GLFWwindow* window, int key) const;
    void activateMainControl(int control, int width, int height, MenuResult& result);
    void activateDialogControl(int control, int width, int height, MenuResult& result);

    static bool keyPressed(GLFWwindow* window, int key);
    static std::string upperTitle(std::string text);
};
