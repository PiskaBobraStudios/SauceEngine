#include "item_suit.h"
#include "sound_l.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

ItemSuit::ItemSuit(const Vec3& p, const std::string& gDir, Texture_L* texL) {
    pos = p;
    gameDirectory = gDir;
    model.load("models/items/hevsuit.mdl", *texL);
}

                                                                                               
void ItemSuit::update(float dt, Camera& player, BSP_L* map) {
                                                                          
    float dx = player.pos.x - pos.x;
    float dz = player.pos.z - pos.z;
    float dist2D = std::sqrt(dx * dx + dz * dz);
    float heightDiff = std::abs(player.pos.y - pos.y);

    if (dist2D < 50.0f && heightDiff < 80.0f && !player.hasSuit) {
        player.hasSuit = true;
        shouldDestroy = true;

        Sound_L::Get().play("sound/items/suitchargeok1.wav", pos, 0.9f, true, true);
    }
}

void ItemSuit::render() {
    if (!model.loaded) return;

    glEnable(GL_LIGHTING);
    glPushMatrix();

    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

    model.render();

    glPopMatrix();
}