#pragma once

class PostProcess_L {
public:
    ~PostProcess_L();

    bool init();
    void shutdown();

                                                                            
    bool beginScene(int width, int height);

                                                           
    void endScene();

    bool enabled() const { return m_ready && m_active; }

public:
    struct Impl;

private:
    Impl* m = nullptr;

    bool m_ready = false;
    bool m_active = false;
};
