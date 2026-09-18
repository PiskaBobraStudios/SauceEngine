#include "hud_l.h"
#include "weapon_l.h"
#include <GLFW/glfw3.h>
#include <string>
#include <algorithm>
#include <cmath>

static void drawRoundedRect(float x,float y,float w,float h,float r){
    const int segments=12;
    const float pi=3.14159265358979323846f;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x+w*0.5f,y+h*0.5f);
    for(int corner=0;corner<4;++corner){
        const float cx=(corner==0||corner==3)?x+w-r:x+r;
        const float cy=(corner<2)?y+r:y+h-r;
        const float a0=(corner*90.0f-90.0f)*pi/180.0f;
        const float a1=a0+90.0f*pi/180.0f;
        for(int i=0;i<=segments;++i){
            const float a=a0+(a1-a0)*(float)i/(float)segments;
            glVertex2f(cx+std::cos(a)*r,cy+std::sin(a)*r);
        }
    }
    glEnd();
}

void HUD_L::drawDigit(int d,float x,float y,float w,float h){
    if(d<0||d>9)return;
    static const bool s[10][7]={{1,1,1,1,1,1,0},{0,1,1,0,0,0,0},{1,1,0,1,1,0,1},{1,1,1,1,0,0,1},{0,1,1,0,0,1,1},{1,0,1,1,0,1,1},{1,0,1,1,1,1,1},{1,1,1,0,0,0,0},{1,1,1,1,1,1,1},{1,1,1,1,0,1,1}};
    glBegin(GL_LINES);
    if(s[d][0]){glVertex2f(x,y);glVertex2f(x+w,y);} if(s[d][1]){glVertex2f(x+w,y);glVertex2f(x+w,y+h*0.5f);} if(s[d][2]){glVertex2f(x+w,y+h*0.5f);glVertex2f(x+w,y+h);} if(s[d][3]){glVertex2f(x,y+h);glVertex2f(x+w,y+h);} if(s[d][4]){glVertex2f(x,y+h*0.5f);glVertex2f(x,y+h);} if(s[d][5]){glVertex2f(x,y);glVertex2f(x,y+h*0.5f);} if(s[d][6]){glVertex2f(x,y+h*0.5f);glVertex2f(x+w,y+h*0.5f);} 
    glEnd();
}

void HUD_L::drawNumber(int number,float x,float y,float w,float h){
    const std::string t=std::to_string(std::max(0,number));
    float dx=0.0f;
    for(char c:t){drawDigit(c-'0',x+dx,y,w,h);dx+=w+4.0f;}
}

void HUD_L::render(int screenWidth,int screenHeight,const Camera& player,const Weapon_L* weapon){
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0,screenWidth,screenHeight,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    const float panelR=0.055f,panelG=0.065f,panelB=0.075f;
    const float accentR=1.0f,accentG=0.68f,accentB=0.08f;
    const int bottom=screenHeight-18;

    if(player.hasSuit){
                                                                                      
                                                                   
        const float left=16.0f, top=(float)screenHeight-74.0f;
        glColor4f(panelR,panelG,panelB,0.78f);
        drawRoundedRect(left,top,142,56,8.0f);
        glColor4f(accentR,accentG,accentB,0.96f);
        glLineWidth(2.3f);
        drawNumber(std::clamp(player.health,0,999),left+13,top+11,13,24);

        const float barX=left+78.0f, barY=top+13.0f, barW=52.0f, barH=8.0f;
        glColor4f(1,1,1,0.12f);
        glBegin(GL_QUADS);
        glVertex2f(barX,barY); glVertex2f(barX+barW,barY);
        glVertex2f(barX+barW,barY+barH); glVertex2f(barX,barY+barH);
        glEnd();
        const float hp=std::clamp(player.health,0,100)/100.0f;
        if(hp>0){
            glColor4f(accentR,accentG,accentB,0.80f);
            glBegin(GL_QUADS);
            glVertex2f(barX+1,barY+1); glVertex2f(barX+1+(barW-2)*hp,barY+1);
            glVertex2f(barX+1+(barW-2)*hp,barY+barH-1); glVertex2f(barX+1,barY+barH-1);
            glEnd();
        }

        const float auxY=top+35.0f, aux=std::clamp(player.sprintEnergy,0.0f,100.0f)/100.0f;
        glColor4f(1,1,1,0.10f);
        glBegin(GL_QUADS);
        glVertex2f(barX,auxY); glVertex2f(barX+barW,auxY);
        glVertex2f(barX+barW,auxY+6); glVertex2f(barX,auxY+6);
        glEnd();
        if(aux>0){
            glColor4f(accentR,accentG,accentB,0.58f);
            glBegin(GL_QUADS);
            glVertex2f(barX+1,auxY+1); glVertex2f(barX+1+(barW-2)*aux,auxY+1);
            glVertex2f(barX+1+(barW-2)*aux,auxY+5); glVertex2f(barX+1,auxY+5);
            glEnd();
        }
    }

    if(weapon && weapon->isActive(player) && weapon->showsAmmo()){
        const int clip=std::max(0,weapon->clipCount());
        const int reserve=std::max(0,weapon->reserveCount());
        const bool clipWeapon=weapon->usesClipAmmo();
        const float right=std::max(180.0f,(float)screenWidth-18.0f);
        const float w=196.0f, top=(float)screenHeight-78.0f;
        const float left=right-w;

        glColor4f(panelR,panelG,panelB,0.82f);
        drawRoundedRect(left,top,w,60,8.0f);
        glColor4f(accentR,accentG,accentB,0.96f);
        glLineWidth(2.7f);
        drawNumber(clipWeapon?clip:reserve,left+14,top+10,15,25);

        if(clipWeapon){
            glColor4f(accentR,accentG,accentB,0.28f);
            glLineWidth(1.4f);
            glBegin(GL_LINES);
            glVertex2f(left+104,top+10); glVertex2f(left+104,top+50);
            glEnd();
            glColor4f(accentR,accentG,accentB,0.82f);
            glLineWidth(1.8f);
            drawNumber(reserve,left+118,top+25,11,17);
        }
    }

                                                                                             
    const float cx=screenWidth*0.5f, cy=screenHeight*0.5f;
    glColor4f(1,1,1,0.78f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex2f(cx-5,cy); glVertex2f(cx-1.5f,cy);
    glVertex2f(cx+1.5f,cy); glVertex2f(cx+5,cy);
    glVertex2f(cx,cy-5); glVertex2f(cx,cy-1.5f);
    glVertex2f(cx,cy+1.5f); glVertex2f(cx,cy+5);
    glEnd();

    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glEnable(GL_TEXTURE_2D);
}
