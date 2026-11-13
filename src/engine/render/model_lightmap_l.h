#pragma once

class BSP_L;

class ModelLightmap_L {
public:
    static bool Begin(const BSP_L& map, bool enabled = true);
    static void End();
};
