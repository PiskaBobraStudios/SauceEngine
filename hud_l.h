#pragma once
class Camera;
class Weapon_L;
class HUD_L {
public:
    void render(int screenWidth,int screenHeight,const Camera& player,const Weapon_L* weapon);
    void drawDigit(int digit,float x,float y,float w,float h);
    void drawNumber(int number,float x,float y,float w,float h);
};
