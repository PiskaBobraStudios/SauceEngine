<div align="center">

<img src="https://images.steamusercontent.com/ugc/18413614534107357906/93255D3C84B5C526CE4AAD8785380A35D1D9FDB7/" width="220">

# Sauce Engine

### A Source-compatible game engine written from scratch.

**An independent spiritual successor to the ideas behind Xash3D.**

<br>

<a href="https://www.reddit.com/user/Piskabobrastudios/">
  <img src="https://img.shields.io/badge/Reddit-PiskaBobraStudios-FF4500?style=for-the-badge&logo=reddit&logoColor=white" alt="Reddit">
</a>
<a href="https://steamcommunity.com/id/PiskaBobraStudios/">
  <img src="https://img.shields.io/badge/Steam-PiskaBobraStudios-171A21?style=for-the-badge&logo=steam&logoColor=white" alt="Steam">
</a>
<a href="https://youtube.com/@piskabobrastudios?si=uqssz4fU3YQhGbMt">
  <img src="https://img.shields.io/badge/YouTube-PiskaBobraStudios-FF0000?style=for-the-badge&logo=youtube&logoColor=white" alt="YouTube">
</a>
<a href="https://www.tiktok.com/@piskabobrastudios?_r=1&_t=ZP-99pnNMG1djb">
  <img src="https://img.shields.io/badge/TikTok-PiskaBobraStudios-000000?style=for-the-badge&logo=tiktok&logoColor=white" alt="TikTok">
</a>

</div>

---

## About

**Sauce Engine** is an independently developed game engine written completely from scratch in **C++** and **OpenGL**.

The project has been in development for approximately **one year** at the time of this repository's publication.

Sauce Engine is designed around the idea of creating an independent, Source-compatible engine without relying on leaked or stolen Source Engine source code.

The long-term goal is to provide an alternative to the original Source technology while improving its technical limits and expanding what can be achieved by the engine.

The project is currently in active development.

---

## What makes Sauce Engine different?

* Written from scratch
* Source-compatible architecture and content support
* C++ / OpenGL based renderer
* Jolt Physics integration
* Designed with higher engine limits in mind
* Independent implementation without leaked Source Engine source code

---

## Half-Life 2 Testing

Sauce Engine is being tested with real Source Engine game content.

### Half-Life 2

<p align="center">
  <img src="https://images.steamusercontent.com/ugc/13062105072635286138/1A47B3D990A6199D130A8D416DD8B7F1DFCE4B5E/" width="49%" alt="Half-Life 2 running on Sauce Engine">
  <img src="https://images.steamusercontent.com/ugc/14555454646337548697/ADC0242C0DF6ED97C268B6F99198EBC4B0C0DE4D/" width="49%" alt="Half-Life 2 running on Sauce Engine">
</p>

<p align="center">
  <i>Half-Life 2 running on Sauce Engine.</i>
</p>

---

## Current Status

Sauce Engine is still under active development.

Some systems are already functional, while other parts of the engine are experimental or incomplete.

The project is being developed incrementally with a focus on:

* Source compatibility
* Rendering
* Physics
* Entity systems
* Engine infrastructure
* Improved technical limits

---

## Building

### Requirements

Currently, the engine is built on **Windows** using `g++` and **C++17**.

You will need:

* MinGW / `g++.exe`
* C++17 support
* GLFW
* Jolt Physics
* OpenGL

The project expects the GLFW and Jolt dependencies to be available in the following structure:

```text
SauceEngine/
├── main.cpp
├── bsp_l.cpp
├── texture_l.cpp
├── mdl_l.cpp
├── prop_static.cpp
├── prop_dynamic.cpp
├── prop_physics.cpp
├── phys.cpp
├── light_l.cpp
├── step_sound_l.cpp
├── entity_l.cpp
├── item_suit.cpp
├── hud_l.cpp
├── weapon_l.cpp
├── sound_l.cpp
├── sauce_compat_l.cpp
│
├── glfw/
│   ├── include/
│   └── lib/
│
└── jolt/
    ├── include/
    └── lib/
```

### Build command

Run the following command from the project directory:

```bash
g++.exe -std=c++17 -O2 -DNDEBUG -DJPH_PROFILE_ENABLED main.cpp bsp_l.cpp texture_l.cpp mdl_l.cpp prop_static.cpp prop_dynamic.cpp prop_physics.cpp phys.cpp light_l.cpp step_sound_l.cpp entity_l.cpp item_suit.cpp hud_l.cpp weapon_l.cpp sound_l.cpp sauce_compat_l.cpp -I. -I.\glfw\include -I.\jolt\include -L.\glfw\lib -L.\jolt\lib -o sauce.exe -lglfw3 -lJolt -lopengl32 -lgdi32 -luser32 -lshell32 -lwinmm -static -static-libgcc -static-libstdc++ -pthread
```

After a successful build, the engine executable will be generated as:

```text
sauce.exe
```

---

## Running

To run a Source game with Sauce Engine, you need a **legally purchased copy of the game** and its required game files.

### Half-Life 2

Place `sauce.exe` in the root directory of your Half-Life 2 installation.

Example:

```text
Half-Life 2/
├── hl2.exe
├── sauce.exe
├── hl2/
├── bin/
├── platform/
└── ...
```

Then launch:

```text
sauce.exe
```

### Other Source games

For other supported games, place `sauce.exe` in the game's root directory and launch it using the `-game` parameter:

```text
sauce.exe -game <game_name>
```

For example:

```text
sauce.exe -game portal
```

The value passed to `-game` should correspond to the game's directory name.

---

## Project Goals

The development of Sauce Engine is focused on building an independent Source-compatible engine with:

* Higher technical limits
* Modernized systems
* Improved flexibility
* Independent implementation
* Support for existing Source game content

The project is being developed from the ground up rather than by modifying leaked Source Engine code.

---

## License

See the [`LICENSE`](LICENSE) file for the license of this project.

---

<div align="center">

### Sauce Engine

Developed by **PiskaBobraStudios**

[Reddit](https://www.reddit.com/user/Piskabobrastudios/) •
[Steam](https://steamcommunity.com/id/PiskaBobraStudios/) •
[YouTube](https://youtube.com/@piskabobrastudios) •
[TikTok](https://www.tiktok.com/@piskabobrastudios)

</div>
