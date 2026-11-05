#!/usr/bin/env python3
"""Patch 351Files source to add DEVICE_RG56PRO and DEVICE_RG43H support.

RG56PRO: 1280x720 (portrait, rotated via SDL2 RGA)
RG43H:   1024x768 (landscape native)

Both use the rk3562-joystick driver which has different joydev button
indices from the retrogame_joypad (RG503).

  rk3562-joystick joydev button indices (verified on-device with evtest,
  RetroArch/SDL udev numbering = ascending evdev-code order):
    b0=B(south)  b1=A(east)  b2=X(north)  b3=Y(west)
    b4=L1(TL)    b5=R1(TR)   b6=L2(TL2)   b7=R2(TR2)
    b8=SELECT    b9=START    b10=GUIDE(MODE)
    b11=L3(THUMBL) b12=R3(THUMBR)
    b13=DPadUp   b14=DPadDown b15=DPadLeft b16=DPadRight  b17=HAPPY1
  L2/R2 analog are on axes ABS_Z/ABS_RZ (joydev a2/a5); no secondary page key.
"""

# --- Patch src/def.h ---

with open("src/def.h") as f:
    defh = f.read()

# Step 1: Blanket replace — add both devices wherever RG503 appears.
# This handles all #elif chains (buttons, screen params, etc.)
defh = defh.replace(
    "defined(DEVICE_RG503)",
    "defined(DEVICE_RG503) || defined(DEVICE_RG56PRO) || defined(DEVICE_RG43H)"
)

# Step 2: Fix BUTTON section — insert proper rk3562 button block and
# revert the combined guard back to RG503-only for that section.
#
# rk3562-joystick joydev button indices (verified on-device with evtest):
#   b0=B(south) b1=A(east) b2=X(north) b3=Y(west)
#   b4=L1 b5=R1 b6=L2(TL2) b7=R2(TR2) b8=SELECT b9=START b10=GUIDE(MODE)
#   b11=L3 b12=R3 b13=DPadUp b14=DPadDown b15=DPadLeft b16=DPadRight
#
# L2/R2 analog are on axes ABS_Z/ABS_RZ (joydev a2/a5) — no secondary page key.

rk3562_button_block = """\
#elif defined(DEVICE_RG56PRO) || defined(DEVICE_RG43H)
   #define BUTTON_PRESSED_UP              event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 13
   #define BUTTON_PRESSED_DOWN            event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 14
   #define BUTTON_PRESSED_LEFT            event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 15
   #define BUTTON_PRESSED_RIGHT           event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 16
   #define BUTTON_PRESSED_PAGEUP          event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 4
   #define BUTTON_PRESSED_PAGEDOWN        event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 5
   #define BUTTON_PRESSED_VALIDATE        event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 1
   #define BUTTON_PRESSED_BACK            event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 0
   #define BUTTON_PRESSED_MENU_CONTEXT    event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 2
   #define BUTTON_PRESSED_SELECT          event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 3
   #define BUTTON_HELD_UP                 SDL_JoystickGetButton(g_joystick, 13)
   #define BUTTON_HELD_DOWN               SDL_JoystickGetButton(g_joystick, 14)
   #define BUTTON_HELD_LEFT               SDL_JoystickGetButton(g_joystick, 15)
   #define BUTTON_HELD_RIGHT              SDL_JoystickGetButton(g_joystick, 16)
   #define BUTTON_HELD_PAGEUP             SDL_JoystickGetButton(g_joystick, 4)
   #define BUTTON_HELD_PAGEDOWN           SDL_JoystickGetButton(g_joystick, 5)
   #define BUTTON_HELD_SELECT             SDL_JoystickGetButton(g_joystick, 3)
   #define BUTTON_HELD_VALIDATE           SDL_JoystickGetButton(g_joystick, 1)
"""

defh = defh.replace(
    "#elif defined(DEVICE_RG503) || defined(DEVICE_RG56PRO) || defined(DEVICE_RG43H)\n"
    "   #define BUTTON_PRESSED_UP",
    rk3562_button_block
    + "#elif defined(DEVICE_RG503)\n"
    "   #define BUTTON_PRESSED_UP"
)

# Step 3: Fix SCREEN section — insert per-device screen blocks and
# revert the combined guard back to RG503-only for that section.

rg52mini_screen_block = """\
#elif defined(DEVICE_RG56PRO)
   #define SCREEN_WIDTH             1280
   #define SCREEN_HEIGHT            720
   #define HARDWARE_ACCELERATION    0
   #define FULLSCREEN               1
   #define FONT_NAME                "NotoSans-Regular.ttf"
   #define FONT_NAME_MONO           "NotoSansMono-Regular.ttf"
   #define FONT_SIZE                26
   #define LINE_HEIGHT              40
   #define ICON_SIZE                32
   #define MARGIN_X                 14
   #define KEYBOARD_MARGIN          10
   #define KEYBOARD_KEY_SPACING     5
   #define KEYBOARD_SYMBOL_SIZE     30
"""

rg43h_screen_block = """\
#elif defined(DEVICE_RG43H)
   #define SCREEN_WIDTH             1024
   #define SCREEN_HEIGHT            768
   #define HARDWARE_ACCELERATION    0
   #define FULLSCREEN               1
   #define FONT_NAME                "NotoSans-Regular.ttf"
   #define FONT_NAME_MONO           "NotoSansMono-Regular.ttf"
   #define FONT_SIZE                22
   #define LINE_HEIGHT              34
   #define ICON_SIZE                32
   #define MARGIN_X                 12
   #define KEYBOARD_MARGIN          8
   #define KEYBOARD_KEY_SPACING     4
   #define KEYBOARD_SYMBOL_SIZE     26
"""

defh = defh.replace(
    "#elif defined(DEVICE_RG503) || defined(DEVICE_RG56PRO) || defined(DEVICE_RG43H)\n"
    "   #define SCREEN_WIDTH",
    rg52mini_screen_block
    + rg43h_screen_block
    + "#elif defined(DEVICE_RG503)\n"
    "   #define SCREEN_WIDTH"
)

for marker in ("#elif defined(DEVICE_RG56PRO)\n   #define SCREEN_WIDTH",
               "#elif defined(DEVICE_RG43H)\n   #define SCREEN_WIDTH",
               "#elif defined(DEVICE_RG56PRO) || defined(DEVICE_RG43H)\n   #define BUTTON_PRESSED_UP"):
    if marker not in defh:
        # str.replace() does nothing when upstream changes its text; do not ship a half-patched app
        raise SystemExit("patch_351files: src/def.h no longer matches what this patch expects: " + marker.splitlines()[0])

with open("src/def.h", "w") as f:
    f.write(defh)

# --- Patch src/sdlutils.cpp ---
# Upstream a6a3f49 ("add 1.5x scaling above 480p") turned the UI metrics into globals that start at 0 and
# are filled in only for DEVICE_RG351MP. On every other device, these two included, the icon folder came
# out as ./res/0/, the font size 0 and the line height 0: the app starts, draws nothing and the screen
# stays black. Fill them from the def.h macros in the fixed-size branch.

with open("src/sdlutils.cpp") as f:
    sdlu = f.read()

needle = "      g_screenWidth = SCREEN_WIDTH;\n      g_screenHeight = SCREEN_HEIGHT;\n"
metrics = (
    "      g_fontSize            = FONT_SIZE;\n"
    "      g_lineHeight          = LINE_HEIGHT;\n"
    "      g_iconSize            = ICON_SIZE;\n"
    "      g_marginX             = MARGIN_X;\n"
    "      g_keyboardMargin      = KEYBOARD_MARGIN;\n"
    "      g_keyboardKeySpacing  = KEYBOARD_KEY_SPACING;\n"
    "      g_keyboardSymbolSize  = KEYBOARD_SYMBOL_SIZE;\n"
)
if needle + "      g_fontSize" not in sdlu:        # not already filled in by upstream
    if needle not in sdlu:
        raise SystemExit("patch_351files: src/sdlutils.cpp no longer matches what this patch expects")
    sdlu = sdlu.replace(needle, needle + metrics, 1)

with open("src/sdlutils.cpp", "w") as f:
    f.write(sdlu)

# --- A fallback font for the characters NotoSans lacks ---
# 351Files draws all its text with res/NotoSans-Regular.ttf (and the Mono one), which has Latin, Greek and
# Cyrillic but no CJK: a Chinese, Japanese or Korean file name came out as a row of boxes. SDL2_ttf has no
# fallback fonts, so renderText() now splits the text into runs: what NotoSans has it draws as before, the
# rest it draws with Noto Sans CJK (fonts-noto-cjk, in the image), or DroidSansFallbackFull.ttf (there for
# EmulationStation, without Hangul) if that is missing, and puts the runs side by side on one baseline. A
# text NotoSans covers whole goes the old way, pixel for pixel.

with open("src/sdlutils.cpp") as f:
    sdlu = f.read()

old_render = """SDL_Texture *SDLUtils::renderText(const std::string &p_text, TTF_Font *p_font, const SDL_Color &p_fg, const SDL_Color &p_bg)
{
   // Create surface
   SDL_Surface *surface = TTF_RenderUTF8_Shaded(p_font, p_text.c_str(), p_fg, p_bg);
"""
new_render = """static TTF_Font *g_fontFallback = NULL;

// Open the fallback font at the size of the main ones; none is fine (boxes, as before)
void SDLUtils::loadFallbackFont(const int p_size)
{
   static const char *paths[] = {
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
      "/usr/bin/emulationstation/resources/DroidSansFallbackFull.ttf",
      NULL };
   for (int i = 0; paths[i] != NULL && g_fontFallback == NULL; ++i)
      g_fontFallback = TTF_OpenFont(paths[i], p_size);
}

void SDLUtils::closeFallbackFont(void)
{
   if (g_fontFallback != NULL)
      TTF_CloseFont(g_fontFallback);
   g_fontFallback = NULL;
}

// Next code point of a UTF-8 string; a broken sequence gives its first byte
static Uint32 nextCodePoint(const std::string &p_text, size_t &p_pos)
{
   const unsigned char c = p_text[p_pos];
   const int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
   if (n <= 0 || p_pos + n >= p_text.size())
   {
      ++p_pos;
      return c;
   }
   Uint32 cp = c & (0x3F >> n);
   for (int i = 1; i <= n; ++i)
   {
      const unsigned char cc = p_text[p_pos + i];
      if ((cc & 0xC0) != 0x80)
      {
         ++p_pos;
         return c;
      }
      cp = (cp << 6) | (cc & 0x3F);
   }
   p_pos += n + 1;
   return cp;
}

SDL_Texture *SDLUtils::renderText(const std::string &p_text, TTF_Font *p_font, const SDL_Color &p_fg, const SDL_Color &p_bg)
{
   // Runs of the text by font: p_font where it has the glyph, else the fallback where that has it
   std::vector<std::pair<TTF_Font *, std::string> > runs;
   if (g_fontFallback != NULL)
   {
      size_t pos = 0;
      while (pos < p_text.size())
      {
         const size_t start = pos;
         const Uint32 cp = nextCodePoint(p_text, pos);
         TTF_Font *font = p_font;
         if (cp >= 0x80 && !TTF_GlyphIsProvided32(p_font, cp) && TTF_GlyphIsProvided32(g_fontFallback, cp))
            font = g_fontFallback;
         if (runs.empty() || runs.back().first != font)
            runs.push_back(std::make_pair(font, std::string()));
         runs.back().second.append(p_text, start, pos - start);
      }
   }
   if (runs.size() > 1 || (runs.size() == 1 && runs[0].first != p_font))
   {
      // Each run drawn alone, then laid side by side on a common baseline
      int ascent = 0, descent = 0;
      for (size_t i = 0; i < runs.size(); ++i)
      {
         if (TTF_FontAscent(runs[i].first) > ascent) ascent = TTF_FontAscent(runs[i].first);
         if (-TTF_FontDescent(runs[i].first) > descent) descent = -TTF_FontDescent(runs[i].first);
      }
      if (TTF_FontHeight(p_font) > ascent + descent)
         descent = TTF_FontHeight(p_font) - ascent;
      std::vector<SDL_Surface *> parts;
      int width = 0;
      for (size_t i = 0; i < runs.size(); ++i)
      {
         SDL_Surface *part = TTF_RenderUTF8_Shaded(runs[i].first, runs[i].second.c_str(), p_fg, p_bg);
         parts.push_back(part);
         if (part != NULL)
            width += part->w;
      }
      SDL_Surface *surface = width > 0 ? SDL_CreateRGBSurfaceWithFormat(0, width, ascent + descent, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
      if (surface != NULL)
      {
         SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, p_bg.r, p_bg.g, p_bg.b));
         int x = 0;
         for (size_t i = 0; i < runs.size(); ++i)
         {
            if (parts[i] == NULL)
               continue;
            SDL_Rect dst = { x, ascent - TTF_FontAscent(runs[i].first), parts[i]->w, parts[i]->h };
            SDL_SetSurfaceBlendMode(parts[i], SDL_BLENDMODE_NONE);
            SDL_BlitSurface(parts[i], NULL, surface, &dst);
            x += parts[i]->w;
         }
      }
      for (size_t i = 0; i < parts.size(); ++i)
         if (parts[i] != NULL)
            SDL_FreeSurface(parts[i]);
      if (surface != NULL)
      {
         SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer, surface);
         SDL_FreeSurface(surface);
         if (texture == NULL)
            std::cerr << "Unable to create texture from surface. SDL_Error: " << SDL_GetError() << std::endl;
         return texture;
      }
      // Could not compose: draw it all with p_font, as before
   }
   // Create surface
   SDL_Surface *surface = TTF_RenderUTF8_Shaded(p_font, p_text.c_str(), p_fg, p_bg);
"""
if "loadFallbackFont" not in sdlu:
    if sdlu.count(old_render) != 1:
        raise SystemExit("patch_351files: src/sdlutils.cpp renderText() no longer matches what this patch expects")
    sdlu = sdlu.replace(old_render, new_render)
    sdlu = sdlu.replace('#include "sdlutils.h"\n', '#include "sdlutils.h"\n#include <vector>\n#include <utility>\n', 1)
    with open("src/sdlutils.cpp", "w") as f:
        f.write(sdlu)

with open("src/sdlutils.h") as f:
    sdlh = f.read()
old_decl = "   TTF_Font *loadFont(const std::string &p_path, const int p_size);\n"
if "loadFallbackFont" not in sdlh:
    if sdlh.count(old_decl) != 1:
        raise SystemExit("patch_351files: src/sdlutils.h no longer matches what this patch expects")
    sdlh = sdlh.replace(old_decl, old_decl + "   void loadFallbackFont(const int p_size);\n   void closeFallbackFont(void);\n")
    with open("src/sdlutils.h", "w") as f:
        f.write(sdlh)

with open("src/main.cpp") as f:
    mainc = f.read()
old_load = "   g_fontMono = SDLUtils::loadFont(std::string(RES_PATH) + \"/\" + FONT_NAME_MONO, g_fontSize);\n"
old_close = "   TTF_CloseFont(g_fontMono);\n   g_fontMono = NULL;\n"
if "loadFallbackFont" not in mainc:
    if mainc.count(old_load) != 1 or mainc.count(old_close) != 1:
        raise SystemExit("patch_351files: src/main.cpp no longer matches what this patch expects")
    mainc = mainc.replace(old_load, old_load + "   SDLUtils::loadFallbackFont(g_fontSize);\n")
    mainc = mainc.replace(old_close, old_close + "   SDLUtils::closeFallbackFont();\n")
    with open("src/main.cpp", "w") as f:
        f.write(mainc)

# --- Patch build_RG351.sh ---
# Add RG56PRO and RG43H to the conditional that triggers dual-build
# (/roms and /roms2)

with open("build_RG351.sh") as f:
    buildsh = f.read()

buildsh = buildsh.replace(
    '"RG503" ]]; then',
    '"RG503" ]] || [[ "${1}" == "RG56PRO" ]] || [[ "${1}" == "RG43H" ]]; then'
)

with open("build_RG351.sh", "w") as f:
    f.write(buildsh)

print("Patched src/def.h, src/sdlutils.cpp (fallback font), src/main.cpp and build_RG351.sh for DEVICE_RG56PRO (1280x720) and DEVICE_RG43H (1024x768)")
