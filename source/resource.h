#pragma once
#define IDI_ICON1       101

// Embedded splash-screen images (loaded via FindResource/LoadResource at
// runtime, then handed to stb_image_load_from_memory).
#define IDR_SPLASH_PNG  200
#define IDR_LOGO_PNG    201

#define IDR_SPARKLE_1   210
#define IDR_SPARKLE_2   211
#define IDR_SPARKLE_3   212
#define IDR_SPARKLE_4   213
#define IDR_SPARKLE_5   214
#define IDR_SPARKLE_6   215
#define IDR_SPARKLE_7   216
#define IDR_SPARKLE_8   217

// Font Awesome 6 Solid (.ttf) — staged into include/image/ by CMake
// (see FA_TTF_STAGED) and bundled into the exe as RCDATA so we can
// load it at runtime via FindResource without depending on shipping
// the file alongside.
#define IDR_FA_FONT     230
