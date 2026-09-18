#pragma once
#include "entity_l.h"
#include "mdl_l.h"

class ItemSuit : public Entity {
public:
    MDL_L model;
    std::string gameDirectory;

    ItemSuit(const Vec3& p, const std::string& gDir, Texture_L* texL);

    void update(float dt, Camera& player, BSP_L* map) override;
    void render() override;

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new ItemSuit(pos, gameDir, texL);
    }
};