#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#undef near
#undef far

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define APP_RT_RCDATA ((LPCSTR)((ULONG_PTR)(WORD)10))

static Image LoadImageFromResource(const char* resName) {
    Image img = { 0 };
    HMODULE hModule = GetModuleHandleA(NULL);
    HRSRC hRes = FindResourceA(hModule, resName, APP_RT_RCDATA);
    if (hRes == NULL) return img;
    HGLOBAL hData = LoadResource(hModule, hRes);
    if (hData == NULL) return img;
    const unsigned char* data = (const unsigned char*)LockResource(hData);
    DWORD size = SizeofResource(hModule, hRes);
    if (data == NULL || size == 0) return img;
    img = LoadImageFromMemory(".png", data, (int)size);
    return img;
}

/*CONSTANTS & DATA */

#define SCREEN_W 1100
#define SCREEN_H 720
#define NUM_BH_TYPES 4
#define NUM_DISTANCES 4
#define NUM_TELESCOPES 2
#define NUM_STARS 220
#define RAD_TO_MICROARCSEC 2.062648e11  /* 1 radian in microarcseconds */
#ifndef DEG2RAD
#define DEG2RAD (3.14159265358979323846f / 180.0f)
#endif
#define PI_D 3.14159265358979323846

typedef enum {
    SCR_WELCOME = 0,
    SCR_SELECT_BH,
    SCR_SELECT_DIST,
    SCR_SELECT_TEL,
    SCR_VIEW,
    SCR_EDUCATION,
    SCR_COMPARE
} AppScreen;

typedef struct {
    const char* name;
    const char* shortName;
    double massSolar;              /* representative mass, in solar masses */
    double schwarzschildKm;        /* Rs = 2.95 km * massSolar (approx)     */
    const char* example;
    Color theme;
} BlackHoleInfo;

typedef struct {
    const char* label;
    double km;                     /* altitude above the event horizon */
} DistanceOption;

typedef struct {
    const char* name;
    const char* shortName;
    double resolutionMicroArcsec;  /* real angular resolution */
    const char* description;
} TelescopeInfo;

typedef struct {
    int bhIdx;
    int telIdx;
    const char* resourceName;   /* RCDATA name in WindowsProject1.rc, e.g. "SGRA_EHT_PNG" */
    const char* sourceUrl;      /* where the real image originally came from */
    const char* caption;
} ComparisonAsset;

static ComparisonAsset g_assets[] = {
    { 2, 0, "SGRA_EHT_PNG",
      "ESO/EHT Collaboration -- cdn.eso.org/images/screen/eso2208-eht-mwa.jpg",
      "The actual 2022 Event Horizon Telescope image of Sagittarius A* -- "
      "the real radio/VLBI observation this simulator's supermassive+VLBI "
      "view is modeled on." },

    { 2, 1, "SGRA_CHANDRA_PNG",
      "NASA/CXC -- chandra.si.edu/photo/2022/sgra/sgra_xray.jpg",
      "A Chandra X-ray Observatory context image of the Sgr A* region: hot "
      "gas blown out near the black hole, imaged at much coarser resolution "
      "than the EHT's radio view -- the horizon itself isn't resolved here." },

    { 0, 1, "CYGX1_CHANDRA_PNG",
      "NASA/CXC -- chandra.si.edu/photo/2011/cygx1/cygx1_xray.jpg",
      "A real X-ray image of a stellar-mass black hole binary. Even a "
      "powerful X-ray telescope only sees it as a bright point of light, "
      "not a resolved disk -- exactly what this simulator predicts." },
};
#define NUM_ASSETS (int)(sizeof(g_assets) / sizeof(g_assets[0]))

static Texture2D g_compareTex;
static bool g_compareTexLoaded = false;
static char g_compareResName[64] = ""; /* which resource is currently loaded into g_compareTex */

typedef struct {
    double schwarzschildKm;
    double altitudeKm;
    double distanceFromCenterKm;
    double ratio;                  /* Rs / distanceFromCenter, in [0,1) */
    double angularSizeRad;
    double angularSizeMicroArcsec;
    bool   resolved;
    bool   insidePhotonSphere;     /* observer at or within the photon sphere (r <= 1.5 Rs) */
    float  screenRadius;
} Observation;

typedef struct {
    Vector2 pos;
    float   size;
    float   phase;
} Star;

static BlackHoleInfo g_blackHoles[NUM_BH_TYPES] = {
    { "Stellar-Mass Black Hole", "Stellar", 21.0, 61.95,
      "Cygnus X-1 (~21 solar masses)", {255, 140, 60, 255} },

    { "Intermediate-Mass Black Hole", "Intermediate", 5000.0, 14750.0,
      "Candidate IMBH in Omega Centauri (~5,000 solar masses)", {170, 130, 255, 255} },

    { "Supermassive Black Hole", "Supermassive", 4300000.0, 12685000.0,
      "Sagittarius A*, our galaxy's core (~4.3 million solar masses)", {255, 205, 90, 255} },

    { "Primordial Black Hole", "Primordial", 0.000003003, 0.00000886,
      "Hypothetical Earth-mass primordial black hole (horizon ~8.9 mm!)", {120, 220, 255, 255} },
};

static DistanceOption g_distances[NUM_DISTANCES] = {
    { "1 Light-Year Away",          9.4607e12 },
    { "1 AU Away",                  1.495979e8 },
    { "1,000 km Above the Horizon", 1000.0 },
    { "10 km Above the Horizon",    10.0 },
};

static TelescopeInfo g_telescopes[NUM_TELESCOPES] = {
    { "Very Long Baseline Interferometry (VLBI)", "VLBI", 25.0,
      "Very Long Baseline Interferometry (VLBI) detects radio waves using "
      "multiple radio telescopes separated by large distances. By combining "
      "their observations, VLBI achieves extremely high angular resolution, "
      "allowing astronomers to study the black hole shadow and the "
      "surrounding radio-emitting material. The Event Horizon Telescope "
      "uses this technique to observe the environments around supermassive "
      "black holes such as M87* and Sagittarius A*." },

    { "High-Resolution X-Ray Imaging", "X-Ray", 500000.0,
      "High-resolution X-ray imaging detects X-rays produced by extremely "
      "hot material surrounding a black hole. These observations reveal "
      "the hot, energetic regions around the black hole, allowing "
      "astronomers to study phenomena such as superheated gas and "
      "high-energy activity near the black hole. X-ray observatories use "
      "specialized grazing-incidence mirrors to focus these high-energy "
      "X-rays onto detectors." },
};

static Star g_stars[NUM_STARS];
static float g_time = 0.0f;

static Font g_font;
static bool g_usingCustomFont = false;

static Texture2D g_bgTexture;
static bool g_bgLoaded = false;

static Texture2D g_iconTexture;
static bool g_iconLoaded = false;

#define DrawText(text, x, y, fontSize, color) \
    DrawTextEx(g_font, (text), (Vector2){(float)(x), (float)(y)}, (float)(fontSize), (float)(fontSize) * 0.02f, (color))
#define MeasureText(text, fontSize) \
    ((int)MeasureTextEx(g_font, (text), (float)(fontSize), (float)(fontSize) * 0.02f).x)

static int g_selBH = -1;
static int g_selDist = -1;
static int g_selTel = -1;
static AppScreen g_screen = SCR_WELCOME;

static char g_eduText[4096];
static RenderTexture2D g_target;
static float   g_renderScale = 1.0f;
static Vector2 g_renderOffset = { 0.0f, 0.0f };

/* UTILITIES */
static Vector2 GetVirtualMouse(void) {
    Vector2 mouse = GetMousePosition();
    Vector2 v = {
        (mouse.x - g_renderOffset.x) / g_renderScale,
        (mouse.y - g_renderOffset.y) / g_renderScale
    };
    if (v.x < 0) v.x = 0;
    if (v.y < 0) v.y = 0;
    if (v.x > SCREEN_W) v.x = SCREEN_W;
    if (v.y > SCREEN_H) v.y = SCREEN_H;
    return v;
}

static void InitStars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        g_stars[i].pos.x = (float)GetRandomValue(0, SCREEN_W);
        g_stars[i].pos.y = (float)GetRandomValue(0, SCREEN_H);
        g_stars[i].size = (float)GetRandomValue(1, 3) * 0.5f;
        g_stars[i].phase = (float)GetRandomValue(0, 628) / 100.0f;
    }
}

static void DrawStarfield(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        float tw = 0.5f + 0.5f * sinf(g_time * 1.5f + g_stars[i].phase);
        DrawCircleV(g_stars[i].pos, g_stars[i].size, Fade(WHITE, 0.25f + 0.55f * tw));
    }
}

/*the app background */
static void DrawBackground(void) {
    if (!g_bgLoaded) {
        DrawStarfield();
        return;
    }
    float zoom = 1.06f + 0.03f * sinf(g_time * 0.12f);
    float panX = 12.0f * sinf(g_time * 0.07f);
    float panY = 6.0f * cosf(g_time * 0.05f);

    float texW = (float)g_bgTexture.width, texH = (float)g_bgTexture.height;
    float scale = zoom * fmaxf((float)SCREEN_W / texW, (float)SCREEN_H / texH);
    float dw = texW * scale, dh = texH * scale;
    float dx = (SCREEN_W - dw) / 2.0f + panX;
    float dy = (SCREEN_H - dh) / 2.0f + panY;

    DrawTextureEx(g_bgTexture, (Vector2) { dx, dy }, 0.0f, scale, Fade(WHITE, 0.55f));

    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f));

    for (int i = 0; i < NUM_STARS; i += 2) {
        float tw = 0.5f + 0.5f * sinf(g_time * 1.5f + g_stars[i].phase);
        DrawCircleV(g_stars[i].pos, g_stars[i].size, Fade(WHITE, 0.10f + 0.20f * tw));
    }
}

static bool Button(Rectangle rec, const char* text, bool enabled) {
    Vector2 mouse = GetVirtualMouse();
    bool hover = enabled && CheckCollisionPointRec(mouse, rec);

    Color base = enabled ? (Color) { 35, 40, 60, 255 } : (Color) { 25, 25, 30, 255 };
    Color hov = (Color){ 70, 90, 140, 255 };
    Color line = enabled ? (Color) { 120, 160, 220, 255 } : (Color) { 60, 60, 65, 255 };

    DrawRectangleRec(rec, hover ? hov : base);
    DrawRectangleLinesEx(rec, 2.0f, line);

    int fontSize = 20;
    int tw = MeasureText(text, fontSize);
    Color txtColor = enabled ? RAYWHITE : GRAY;
    DrawText(text, (int)(rec.x + (rec.width - tw) / 2), (int)(rec.y + (rec.height - fontSize) / 2),
        fontSize, txtColor);

    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
static bool CircleButton(Vector2 center, float radius, const char* text, bool enabled) {
    Vector2 mouse = GetVirtualMouse();
    bool hover = enabled && CheckCollisionPointCircle(mouse, center, radius);

    Color base = enabled ? (Color) { 70, 35, 10, 255 } : (Color) { 25, 25, 30, 255 };
    Color hov = (Color){ 180, 90, 20, 255 };
    Color line = enabled ? (Color) { 255, 150, 40, 255 } : (Color) { 60, 60, 65, 255 };

    DrawCircleV(center, radius, hover ? hov : base);
    DrawRing(center, radius - 2.0f, radius, 0, 360, 64, line);

    int fontSize = 20;
    int tw = MeasureText(text, fontSize);
    Color txtColor = enabled ? RAYWHITE : GRAY;
    DrawText(text, (int)(center.x - tw / 2), (int)(center.y - fontSize / 2), fontSize, txtColor);

    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void DrawTextWrapped(const char* text, int x, int y, int maxWidth, int fontSize, Color color, int lineSpacing) {
    int len = (int)strlen(text);
    char line[1024] = { 0 };
    int curY = y;
    int i = 0;

    while (i < len) {
        int wordStart = i;
        while (i < len && text[i] != ' ' && text[i] != '\n') i++;
        int wordLen = i - wordStart;
        if (wordLen > 500) wordLen = 500;
        char word[512] = { 0 };
        memcpy(word, text + wordStart, wordLen);
        word[wordLen] = '\0';

        char testLine[1024];
        if (line[0] == '\0') snprintf(testLine, sizeof(testLine), "%s", word);
        else snprintf(testLine, sizeof(testLine), "%s %s", line, word);

        int w = MeasureText(testLine, fontSize);
        if (w > maxWidth && line[0] != '\0') {
            DrawText(line, x, curY, fontSize, color);
            curY += fontSize + lineSpacing;
            snprintf(line, sizeof(line), "%s", word);
        }
        else {
            snprintf(line, sizeof(line), "%s", testLine);
        }

        if (i < len && text[i] == '\n') {
            DrawText(line, x, curY, fontSize, color);
            curY += fontSize + lineSpacing;
            line[0] = '\0';
            i++;
        }
        else {
            i++;
        }
    }
    if (line[0] != '\0') {
        DrawText(line, x, curY, fontSize, color);
    }
}

static void DrawHeader(const char* title, const char* subtitle) {
    DrawText(title, 60, 40, 34, RAYWHITE);
    if (subtitle) DrawText(subtitle, 60, 82, 18, LIGHTGRAY);
    DrawLine(60, 115, SCREEN_W - 60, 115, (Color) { 80, 90, 120, 180 });
}

/*PHYSICS */

static Observation ComputeObservation(int bhIdx, int distIdx, int telIdx) {
    Observation o = { 0 };
    o.schwarzschildKm = g_blackHoles[bhIdx].schwarzschildKm;
    o.altitudeKm = g_distances[distIdx].km;
    o.distanceFromCenterKm = o.schwarzschildKm + o.altitudeKm;

    double Rs = o.schwarzschildKm;
    double r_o = o.distanceFromCenterKm;
    double x = Rs / r_o;              /* dimensionless, in (0, 1) */
    o.ratio = x;

    double halfAngle; /* psi, radians, always in [0, pi/2] */
    if (r_o <= 1.5 * Rs) {
        o.insidePhotonSphere = true;
        halfAngle = PI_D / 2.0;       /* shadow fills a full hemisphere */
    }
    else {
        double sin2psi = 6.75 * x * x * (1.0 - x);
        if (sin2psi > 1.0) sin2psi = 1.0;
        halfAngle = asin(sqrt(sin2psi));
    }

    o.angularSizeRad = 2.0 * halfAngle;   /* angular diameter, 0 to pi */
    o.angularSizeMicroArcsec = o.angularSizeRad * RAD_TO_MICROARCSEC;
    o.resolved = o.angularSizeMicroArcsec >= g_telescopes[telIdx].resolutionMicroArcsec;

    double normalized = halfAngle / (PI_D / 2.0); /* 0..1, physically grounded */
    o.screenRadius = 20.0f + 230.0f * (float)normalized;
    return o;
}
static const ComparisonAsset* FindAsset(int bh, int tel);
static const char* NoImageExplanation(int bh);

static void BuildEducationText(int bhIdx, int distIdx, int telIdx, Observation obs) {
    (void)distIdx;
    TelescopeInfo tel = g_telescopes[telIdx];
    char buf[4096];
    int n = 0;

    if (obs.insidePhotonSphere) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "You are at or inside the photon sphere here the region where light itself "
            "can orbit the black hole. Escaping light is bent so severely that the shadow "
            "doesn't just look big, it fills an entire hemisphere of the sky. This view is "
            "clamped at that point rather than modeling the extreme multiply-lensed image "
            "beyond it.\n\n");
    }

    if (obs.resolved) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "This view is resolved: at %.4g microarcseconds, it's larger than %s's "
            "%.0f microarcsecond resolution limit, so real structure is visible instead "
            "of just a point of light.\n\n",
            obs.angularSizeMicroArcsec, tel.shortName, tel.resolutionMicroArcsec);

        if (telIdx == 0) {
            n += snprintf(buf + n, sizeof(buf) - n,
                "In this render: the dark disc is the event horizon, blocking light from "
                "behind it. The bright ring hugging its edge is the photon ring light "
                "that orbited the black hole one or more times before escaping toward you. "
                "The warm arcs above and below are the far side of the accretion disk, bent "
                "into view by gravitational lensing.");
        }
        else {
            n += snprintf(buf + n, sizeof(buf) - n,
                "In this render: the bright, flattened band is the accretion disk, hotter "
                "and bluer toward the center. One side reads brighter:that's Doppler "
                "beaming, from material moving toward you being relativistically brightened. "
                "The narrow beams above and below are relativistic jets.");
        }
    }
    else {
        n += snprintf(buf + n, sizeof(buf) - n,
            "This view is unresolved: at %.4g microarcseconds, it's smaller than %s's "
            "%.0f microarcsecond resolution limit. No real structure can be shown here so "
            "the honest result is a single point of light, exactly like a real telescope "
            "would return in this situation.",
            obs.angularSizeMicroArcsec, tel.shortName, tel.resolutionMicroArcsec);
    }

    n += snprintf(buf + n, sizeof(buf) - n,
        "\n\nSimulation vs. reality:\nWhat's on screen isn't a photograph, and it isn't a "
        "light-by-light ray trace either. The physics deciding WHETHER anything is visible "
        "the angular size and resolution numbers above is real. The colors, glow, and "
        "exact shapes are a stylized rendering built on top of that result, not a precise "
        "reconstruction of what a telescope's sensor would actually record.\n\n");

    const ComparisonAsset* asset = FindAsset(bhIdx, telIdx);
    if (asset != NULL) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "A genuine photograph exists for this exact combination. Tap \"Compare to Real "
            "Photo\" to see how this simulation stacks up against it.");
    }
    else {
        n += snprintf(buf + n, sizeof(buf) - n,
            "No real photograph exists for this exact combination. Tap \"Compare to Real "
            "Photo\" to see why either the resolution was never achievable, or the object "
            "itself has never been directly observed at all.");
    }

    strncpy(g_eduText, buf, sizeof(g_eduText) - 1);
    g_eduText[sizeof(g_eduText) - 1] = '\0';
}

/* VIEW RENDERING */

static float Hash11(int seed) {
    unsigned int x = (unsigned int)seed;
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return (float)(x % 1000) / 1000.0f;
}

static void DrawLensedArc(Vector2 c, float radius, float thickness,
    float a0, float a1,
    Color outer, Color inner, float alpha)
{
    const int layers = 42;

    for (int i = layers; i >= 1; i--)
    {
        float t = (float)i / (float)layers;
        float curve = t * t;
        float rr = radius + thickness * curve;

        Color col =
        {
            (unsigned char)(
                outer.r +
                (inner.r - outer.r) * (1.0f - t)
            ),

            (unsigned char)(
                outer.g +
                (inner.g - outer.g) * (1.0f - t)
            ),

            (unsigned char)(
                outer.b +
                (inner.b - outer.b) * (1.0f - t)
            ),

            255
        };

        float lineWidth =
            2.2f + 4.5f * (1.0f - t);

        float pulse =
            0.75f +
            0.25f * sinf(
                g_time * 1.5f +
                t * 35.0f
            );

        float a =
            alpha *
            pulse *
            (0.013f + 0.022f * (1.0f - t));

        DrawRing(
            c,
            rr,
            rr + lineWidth,
            a0,
            a1,
            128,
            Fade(col, a)
        );
    }
}

static void DrawViewScene(int bhIdx, int telIdx, Observation obs) {
    (void)bhIdx;
    int cx = SCREEN_W / 2;
    int cy = 380;
    Vector2 c = { (float)cx, (float)cy };

    if (!obs.resolved) {
        float pulse = 2.0f + sinf(g_time * 3.0f) * 1.0f;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleV(c, 14.0f + pulse * 2, Fade(WHITE, 0.08f));
        DrawCircleV(c, 8.0f + pulse, Fade(WHITE, 0.20f));
        EndBlendMode();
        DrawCircleV(c, 3.0f + pulse * 0.4f, WHITE);
        const char* msg = "UNRESOLVED : appears as a point source";
        DrawText(msg, cx - MeasureText(msg, 20) / 2, cy + 60, 20, LIGHTGRAY);
        return;
    }

    float r = obs.screenRadius;

    Color outerCol = (telIdx == 0) ? (Color) { 160, 20, 10, 255 } : (Color) { 15, 55, 150, 255 };
    Color midCol = (telIdx == 0) ? (Color) { 255, 90, 20, 255 } : (Color) { 60, 150, 255, 255 };
    Color innerCol = (telIdx == 0) ? (Color) { 255, 210, 120, 255 } : (Color) { 220, 240, 255, 255 };

    BeginBlendMode(BLEND_ADDITIVE);

    for (int i = 8; i >= 1; i--) {
        float rr = r * (1.6f + i * 0.35f);
        DrawCircleV(c, rr, Fade(outerCol, 0.012f));
    }

    if (telIdx == 1) {
        for (int side = -1; side <= 1; side += 2) {
            float len = r * 2.4f;
            float baseY = (float)cy + side * r * 0.9f;
            for (int i = 8; i >= 1; i--) {
                float w = (2.0f + i * 1.6f) * (1.0f - (float)i / 10.0f + 0.3f);
                float flick = 0.6f + 0.4f * sinf(g_time * 4.0f + i + side);
                Rectangle jet = { (float)cx - w / 2, side > 0 ? baseY : baseY - len, w, len };
                DrawRectangleRec(jet, Fade(midCol, 0.05f * flick));
            }
        }
    }

    float diskRW = r * 2.7f;
    float diskRH = diskRW * 0.16f;
    for (int i = 14; i >= 1; i--) {
        float t = (float)i / 14.0f;
        float rw = diskRW * t;
        float rh = diskRH * t + 2.0f;
        Color col = (Color){
            (unsigned char)(outerCol.r + (innerCol.r - outerCol.r) * (1.0f - t)),
            (unsigned char)(outerCol.g + (innerCol.g - outerCol.g) * (1.0f - t)),
            (unsigned char)(outerCol.b + (innerCol.b - outerCol.b) * (1.0f - t)),
            255
        };
        DrawEllipse(cx, cy, rw, rh, Fade(col, 0.10f));
    }

    DrawLensedArc(c, r * 1.02f, r * 0.55f, 200.0f, 340.0f, outerCol, innerCol, 1.0f);
    DrawLensedArc(c, r * 1.02f, r * 0.40f, 20.0f, 160.0f, outerCol, innerCol, 0.85f);

    if (telIdx == 1) {
        for (int i = 0; i < 40; i++) {
            float n1 = Hash11(i * 31 + 7);
            float n2 = Hash11(i * 53 + 91);
            float angle = n1 * 360.0f + g_time * 10.0f;
            float rad = (0.3f + 0.6f * n2);
            float px = cx + cosf(angle * DEG2RAD) * diskRW * 0.5f * (0.5f + rad);
            float py = cy + sinf(angle * DEG2RAD) * diskRH * 0.5f * (0.5f + rad);
            float approach = cosf(angle * DEG2RAD);
            float bright = 0.15f + 0.35f * (approach > 0 ? approach : approach * 0.2f);
            if (bright < 0.04f) bright = 0.04f;
            DrawCircleV((Vector2) { px, py }, 1.5f + 2.0f * n2, Fade(innerCol, bright));
        }
    }
    else {
      
        for (int i = 10; i >= 1; i--) {
            float t = (float)i / 10.0f;
            DrawRing(c, r * (0.85f + 0.30f * t), r * (0.95f + 0.30f * t), -50, 50, 64,
                Fade(innerCol, 0.030f * (1.0f - t * 0.4f)));
        }
    }

    EndBlendMode();

    DrawCircleV(c, r * 0.85f, BLACK);
    for (int i = 10; i >= 1; i--) {
        float t = (float)i / 10.0f;                 /* 1 -> 0 going outward */
        float rr = r * (0.85f + 0.15f * (1.0f - t)); /* 0.85r -> 1.00r */
        DrawRing(c, rr - 2.0f, rr, 0, 360, 96, Fade(BLACK, t));
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 12; i >= 1; i--) {
        float t = (float)i / 12.0f;
        float rr = r * (0.92f + 0.22f * t);
        DrawRing(c, rr, rr + 3.0f, 0, 360, 96, Fade(innerCol, 0.045f * (1.0f - t * 0.3f)));
    }
    EndBlendMode();

    char info[128];
    snprintf(info, sizeof(info), "Apparent size: %.3g microarcseconds", obs.angularSizeMicroArcsec);
    DrawText(info, cx - MeasureText(info, 18) / 2, cy + (int)r + 50, 18, LIGHTGRAY);
}

static const ComparisonAsset* FindAsset(int bh, int tel) {
    for (int i = 0; i < NUM_ASSETS; i++) {
        if (g_assets[i].bhIdx == bh && g_assets[i].telIdx == tel) return &g_assets[i];
    }
    return NULL;
}

static const char* NoImageExplanation(int bh) {
    switch (bh) {
    case 0: /* stellar */
        return "No telescope, whether observing in radio,"
            "visible light, or X-rays, has ever captured a clear"
            "image of a stellar-mass black hole's shadow and ring."
            "They're simply too small and too far away.Instead, scientists"
            "study their X-ray brightness and the "
            "movements of nearby companion stars.These observations give"
            "us the evidence we need to know that these black holes exist.";
    case 1: /* intermediate */
        return "No intermediate-mass black hole has ever been directly imaged, "
            "or even confirmed beyond reasonable doubt. Candidates like the "
            "one in Omega Centauri are inferred from the motion of nearby "
            "stars, not from a picture of the black hole itself.";
    case 3: /* primordial */
        return "Primordial black holes are still hypothetical, scientists"
            " have not yet found confirmed evidence that they exist.This view "
            "is a physics-based prediction of what one would look like, not "
            "a real observation.";
    default:
        return "No real photo is available for this exact combination yet.";
    }
}

/*SCREENS */

static void ScreenWelcome(void)
{
    DrawBackground();

    int cx = SCREEN_W / 2;
    int cy = 220;

    Vector2 c =
    {
        (float)cx,
        (float)cy
    };

    float r = 70.0f;

    if (g_iconLoaded) {
        float frameR = r * 1.9f;

        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 20; i >= 1; i--) {
            float t = (float)i / 20.0f;
            DrawCircleV(c, frameR * (1.0f + 0.6f * t), Fade((Color) { 200, 60, 20, 255 }, 0.012f));
        }
        EndBlendMode();

        float texW = (float)g_iconTexture.width, texH = (float)g_iconTexture.height;
        float scale = (frameR * 2.0f) / fminf(texW, texH);
        float dw = texW * scale, dh = texH * scale;
        DrawTextureEx(g_iconTexture, (Vector2) { c.x - dw / 2, c.y - dh / 2 }, 0.0f, scale, WHITE);
    }
    else {

        Color deepRed =
        {
            100, 5, 2, 255
        };

        Color outerRed =
        {
            180, 20, 5, 255
        };

        Color orange =
        {
            255, 75, 8, 255
        };

        Color hotOrange =
        {
            255, 150, 35, 255
        };


        BeginBlendMode(BLEND_ADDITIVE);

        for (int i = 30; i >= 1; i--)
        {
            float t =
                (float)i / 30.0f;

            float width =
                r * (1.8f + 2.8f * t);

            float height =
                r * (0.25f + 0.55f * t);

            float alpha =
                0.004f + 0.004f * (1.0f - t);

            DrawEllipse(
                cx,
                cy,
                width,
                height,
                Fade(deepRed, alpha)
            );
        }

        float diskRW =
            r * 3.9f;

        float diskRH =
            diskRW * 0.075f;

        for (int i = 35; i >= 1; i--)
        {
            float t =
                (float)i / 35.0f;

            float rw =
                diskRW * t;

            float rh =
                diskRH * t + 0.8f;

            Color col =
            {
                (unsigned char)(
                    outerRed.r +
                    (hotOrange.r - outerRed.r)
                    * (1.0f - t)
                ),

                (unsigned char)(
                    outerRed.g +
                    (hotOrange.g - outerRed.g)
                    * (1.0f - t)
                ),

                (unsigned char)(
                    outerRed.b +
                    (hotOrange.b - outerRed.b)
                    * (1.0f - t)
                ),

                255
            };

            float movement =
                0.85f +
                0.15f *
                sinf(
                    g_time * 2.0f +
                    t * 25.0f
                );

            DrawEllipse(
                cx,
                cy,
                rw,
                rh,
                Fade(
                    col,
                    0.025f * movement
                )
            );
        }

        for (int i = 18; i >= 1; i--)
        {
            float t = (float)i / 18.0f;

            float glowWidth =
                diskRW * (0.55f + 0.45f * t);

            float glowHeight =
                diskRH * (2.0f + 3.0f * t);

            float alpha =
                0.006f * (1.0f - t);

            DrawEllipse(
                cx,
                cy,
                glowWidth,
                glowHeight,
                Fade(outerRed, alpha)
            );
        }

        DrawLensedArc(
            c,
            r * 0.95f,
            r * 0.72f,
            198.0f,
            342.0f,
            outerRed,
            hotOrange,
            1.15f
        );

        DrawLensedArc(
            c,
            r * 0.95f,
            r * 0.52f,
            18.0f,
            162.0f,
            outerRed,
            orange,
            0.90f
        );

        DrawRing(
            c,
            r * 1.03f,
            r * 1.045f,
            205.0f,
            335.0f,
            128,
            Fade(hotOrange, 0.32f)
        );

        DrawRing(
            c,
            r * 1.08f,
            r * 1.092f,
            210.0f,
            330.0f,
            128,
            Fade(orange, 0.22f)
        );

        DrawRing(
            c,
            r * 1.14f,
            r * 1.152f,
            220.0f,
            320.0f,
            128,
            Fade(outerRed, 0.18f)
        );

        DrawRing(
            c,
            r * 1.03f,
            r * 1.045f,
            25.0f,
            155.0f,
            128,
            Fade(orange, 0.25f)
        );

        DrawRing(
            c,
            r * 1.09f,
            r * 1.101f,
            30.0f,
            150.0f,
            128,
            Fade(outerRed, 0.16f)
        );

        EndBlendMode();

        DrawCircleV(c, r, BLACK);

        BeginBlendMode(BLEND_ADDITIVE);

        DrawRing(
            c,
            r * 1.000f,
            r * 1.025f,
            0,
            360,
            128,
            Fade((Color) { 255, 80, 15, 255 }, 0.18f)
        );

        DrawRing(
            c,
            r * 1.005f,
            r * 1.018f,
            0,
            360,
            128,
            Fade((Color) { 255, 130, 25, 255 }, 0.45f)
        );

        DrawRing(
            c,
            r * 1.012f,
            r * 1.020f,
            0,
            360,
            128,
            Fade((Color) { 255, 175, 50, 255 }, 0.55f)
        );

        EndBlendMode();

    } 

    const char* title =
        "BLACKMYSTERY";

    DrawText(
        title,
        cx - MeasureText(title, 46) / 2,
        400,
        46,
        RAYWHITE
    );

    const char* sub =
        "Exploring the Mysteries of Black Holes";

    DrawText(
        sub,
        cx - MeasureText(sub, 20) / 2,
        456,
        20,
        LIGHTGRAY
  
      );

    if (
        CircleButton(
            (Vector2)
    {
        (float)cx,
            565
    },
            55.0f,
            "BEGIN",
            true
        )
        )
    {
        g_screen =
            SCR_SELECT_BH;
    }

    const char* hint =
        "Built by Hajer Chetoui with C + raylib";

    DrawText(
        hint,
        cx - MeasureText(hint, 16) / 2,
        645,
        16,
        GRAY
    );

    const char* fshint = "Press F11 to toggle fullscreen";
    Color fshintColor = { 130, 140, 165, 255 };
    DrawText(fshint, cx - MeasureText(fshint, 14) / 2, 668, 14, fshintColor);
}

static void ScreenSelectBH(void) {
    DrawBackground();
    DrawHeader("Choose a Black Hole Type:", "Each type uses a representative mass and corresponding Schwarzschild radius.");

    int cols = 2;
    int cardW = 460, cardH = 190, gapX = 40, gapY = 30;
    int startX = (SCREEN_W - (cardW * cols + gapX)) / 2;
    int startY = 150;

    for (int i = 0; i < NUM_BH_TYPES; i++) {
        int col = i % cols, row = i / cols;
        Rectangle rec = { (float)(startX + col * (cardW + gapX)), (float)(startY + row * (cardH + gapY)),
                           (float)cardW, (float)cardH };
        bool hover = CheckCollisionPointRec(GetVirtualMouse(), rec);

        DrawRectangleRec(rec, hover ? (Color) { 40, 45, 70, 255 } : (Color) { 24, 26, 40, 255 });
        DrawRectangleLinesEx(rec, 2.0f, g_blackHoles[i].theme);

        if (g_iconLoaded) {
            float badgeR = 22.0f;
            float texW = (float)g_iconTexture.width, texH = (float)g_iconTexture.height;
            float scale = (badgeR * 2.0f) / fminf(texW, texH);
            float dw = texW * scale, dh = texH * scale;
            DrawTextureEx(g_iconTexture, (Vector2) { rec.x + 45 - dw / 2, rec.y + 45 - dh / 2 }, 0.0f, scale, WHITE);
        }
        else {
            DrawCircleV((Vector2) { rec.x + 45, rec.y + 45 }, 22, g_blackHoles[i].theme);
            DrawCircleV((Vector2) { rec.x + 45, rec.y + 45 }, 12, BLACK);
        }

        DrawText(g_blackHoles[i].name, (int)rec.x + 85, (int)rec.y + 20, 22, RAYWHITE);
        DrawText(TextFormat("~%.3g solar masses", g_blackHoles[i].massSolar),
            (int)rec.x + 85, (int)rec.y + 50, 16, LIGHTGRAY);
        DrawText(TextFormat("Horizon radius: %.4g km", g_blackHoles[i].schwarzschildKm),
            (int)rec.x + 20, (int)rec.y + 100, 16, LIGHTGRAY);
        DrawTextWrapped(g_blackHoles[i].example, (int)rec.x + 20, (int)rec.y + 128, cardW - 40, 15, GRAY, 4);

        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_selBH = i;
            g_screen = SCR_SELECT_DIST;
        }
    }

    if (Button((Rectangle) { 40, (float)SCREEN_H - 80, 130, 45 }, "< Back", true)) {
        g_screen = SCR_WELCOME;
    }
}

static void ScreenSelectDist(void) {
    DrawBackground();
    DrawHeader("Choose Your Observation Distance:", "Observation Distance from the Black Hole.");

    int btnW = 480, btnH = 80, gapY = 24;
    int startX = (SCREEN_W - btnW) / 2;
    int startY = 170;

    for (int i = 0; i < NUM_DISTANCES; i++) {
        Rectangle rec = { (float)startX, (float)(startY + i * (btnH + gapY)), (float)btnW, (float)btnH };
        bool hover = CheckCollisionPointRec(GetVirtualMouse(), rec);
        DrawRectangleRec(rec, hover ? (Color) { 40, 45, 70, 255 } : (Color) { 24, 26, 40, 255 });
        DrawRectangleLinesEx(rec, 2.0f, (Color) { 110, 140, 200, 255 });

        if (g_iconLoaded) {
            float badgeR = 20.0f;
            Vector2 badgeC = { rec.x + 42, rec.y + btnH / 2.0f };
            float texW = (float)g_iconTexture.width, texH = (float)g_iconTexture.height;
            float scale = (badgeR * 2.0f) / fminf(texW, texH);
            float dw = texW * scale, dh = texH * scale;
            DrawTextureEx(g_iconTexture, (Vector2) { badgeC.x - dw / 2, badgeC.y - dh / 2 }, 0.0f, scale, WHITE);
        }

        int textX = g_iconLoaded ? (int)rec.x + 78 : (int)rec.x + 24;
        DrawText(g_distances[i].label, textX, (int)rec.y + 16, 24, RAYWHITE);
        DrawText(TextFormat("%.5g km", g_distances[i].km), textX, (int)rec.y + 46, 16, LIGHTGRAY);

        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_selDist = i;
            g_screen = SCR_SELECT_TEL;
        }
    }

    if (Button((Rectangle) { 40, (float)SCREEN_H - 80, 130, 45 }, "< Back", true)) {
        g_screen = SCR_SELECT_BH;
    }
}

static void ScreenSelectTel(void) {
    DrawBackground();
    DrawHeader("Select Your Astronomical Instrument:", "Different telescopes reveal completely different things.");

    int cardW = 480, cardH = 320, gap = 40;
    int startX = (SCREEN_W - (cardW * 2 + gap)) / 2;
    int startY = 160;

    for (int i = 0; i < NUM_TELESCOPES; i++) {
        Rectangle rec = { (float)(startX + i * (cardW + gap)), (float)startY, (float)cardW, (float)cardH };
        bool hover = CheckCollisionPointRec(GetVirtualMouse(), rec);
        DrawRectangleRec(rec, hover ? (Color) { 40, 45, 70, 255 } : (Color) { 24, 26, 40, 255 });
        DrawRectangleLinesEx(rec, 2.0f, (Color) { 110, 140, 200, 255 });

        int titleX = (int)rec.x + 20;
        if (g_iconLoaded) {
            float badgeR = 16.0f;
            Vector2 badgeC = { rec.x + 20 + badgeR, rec.y + 31 };
            float texW = (float)g_iconTexture.width, texH = (float)g_iconTexture.height;
            float scale = (badgeR * 2.0f) / fminf(texW, texH);
            float dw = texW * scale, dh = texH * scale;
            DrawTextureEx(g_iconTexture, (Vector2) { badgeC.x - dw / 2, badgeC.y - dh / 2 }, 0.0f, scale, WHITE);
            titleX = (int)(rec.x + 20 + badgeR * 2 + 12);
        }

        DrawText(g_telescopes[i].name, titleX, (int)rec.y + 20, 22, RAYWHITE);
        DrawText(TextFormat("Angular resolution: %.0f microarcsec", g_telescopes[i].resolutionMicroArcsec),
            (int)rec.x + 20, (int)rec.y + 55, 16, GOLD);
        DrawTextWrapped(g_telescopes[i].description, (int)rec.x + 20, (int)rec.y + 90, cardW - 40, 16, LIGHTGRAY, 6);

        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_selTel = i;
            g_screen = SCR_VIEW;
        }
    }

    if (Button((Rectangle) { 40, (float)SCREEN_H - 80, 130, 45 }, "< Back", true)) {
        g_screen = SCR_SELECT_DIST;
    }
}

static void ScreenView(void) {
    DrawBackground();
    Observation obs = ComputeObservation(g_selBH, g_selDist, g_selTel);

    DrawHeader(TextFormat("%s  |  %s  |  %s", g_blackHoles[g_selBH].shortName,
        g_distances[g_selDist].label, g_telescopes[g_selTel].shortName), NULL);

    DrawViewScene(g_selBH, g_selTel, obs);

    if (Button((Rectangle) { (float)SCREEN_W / 2 - 140, 630, 280, 55 }, "EXPLAIN THIS VIEW", true)) {
        BuildEducationText(g_selBH, g_selDist, g_selTel, obs);
        g_screen = SCR_EDUCATION;
    }

    if (Button((Rectangle) { 40, (float)SCREEN_H - 80, 130, 45 }, "< Back", true)) {
        g_screen = SCR_SELECT_TEL;
    }
}

static void ScreenEducation(void) {
    DrawBackground();
    DrawHeader("What You're Looking At", NULL);

    Rectangle panel = { 60, 140, (float)SCREEN_W - 120, 480 };
    DrawRectangleRec(panel, Fade((Color) { 20, 22, 34, 255 }, 0.92f));
    DrawRectangleLinesEx(panel, 2.0f, (Color) { 110, 140, 200, 255 });

    DrawTextWrapped(g_eduText, (int)panel.x + 30, (int)panel.y + 25, (int)panel.width - 60, 18, RAYWHITE, 7);

    if (Button((Rectangle) { (float)SCREEN_W / 2 - 335, 650, 220, 50 }, "Compare to Real Photo", true)) {
        g_screen = SCR_COMPARE;
    }
    if (Button((Rectangle) { (float)SCREEN_W / 2 - 100, 650, 150, 50 }, "New View", true)) {
        g_screen = SCR_SELECT_TEL;
    }
    if (Button((Rectangle) { (float)SCREEN_W / 2 + 65, 650, 150, 50 }, "Start Over", true)) {
        g_selBH = g_selDist = g_selTel = -1;
        g_screen = SCR_WELCOME;
    }
}

static void ScreenCompare(void) {
    DrawBackground();
    DrawHeader("Real Photo Comparison", NULL);

    Rectangle panel = { 60, 140, (float)SCREEN_W - 120, 480 };
    DrawRectangleRec(panel, Fade((Color) { 20, 22, 34, 255 }, 0.92f));
    DrawRectangleLinesEx(panel, 2.0f, (Color) { 110, 140, 200, 255 });

    const ComparisonAsset* asset = FindAsset(g_selBH, g_selTel);

    if (asset == NULL) {
        DrawText("There's no real image of this combination", (int)panel.x + 30, (int)panel.y + 25, 22, GOLD);
        DrawTextWrapped(NoImageExplanation(g_selBH), (int)panel.x + 30, (int)panel.y + 65,
            (int)panel.width - 60, 18, RAYWHITE, 7);
    }
    else {
        if (!g_compareTexLoaded || strcmp(g_compareResName, asset->resourceName) != 0) {
            if (g_compareTexLoaded) { UnloadTexture(g_compareTex); g_compareTexLoaded = false; }
            Image img = LoadImageFromResource(asset->resourceName);
            if (img.data != NULL) {
                g_compareTex = LoadTextureFromImage(img);
                UnloadImage(img);
                g_compareTexLoaded = (g_compareTex.id != 0);
                snprintf(g_compareResName, sizeof(g_compareResName), "%s", asset->resourceName);
            }
        }

        if (g_compareTexLoaded && strcmp(g_compareResName, asset->resourceName) == 0) {
            float maxW = panel.width - 60, maxH = 380;
            float scale = fminf(maxW / g_compareTex.width, maxH / g_compareTex.height);
            float dw = g_compareTex.width * scale, dh = g_compareTex.height * scale;
            float dx = panel.x + (panel.width - dw) / 2, dy = panel.y + 20;
            DrawTextureEx(g_compareTex, (Vector2) { dx, dy }, 0.0f, scale, WHITE);
            char credit[256];
            snprintf(credit, sizeof(credit), "Credit: %s", asset->sourceUrl);
            int cw = MeasureText(credit, 16);
            DrawText(credit, (int)(panel.x + (panel.width - cw) / 2), (int)(dy + dh + 20), 16, GRAY);
        }
        else {
            DrawText("Couldn't load this comparison photo", (int)panel.x + 30, (int)panel.y + 25, 22, GOLD);
            char msg[512];
            snprintf(msg, sizeof(msg),
                "Source: %s\n\n%s",
                asset->sourceUrl, asset->caption);
            DrawTextWrapped(msg, (int)panel.x + 30, (int)panel.y + 65, (int)panel.width - 60, 17, RAYWHITE, 6);
        }
    }

    if (Button((Rectangle) { (float)SCREEN_W / 2 - 80, 650, 160, 50 }, "< Back", true)) {
        g_screen = SCR_EDUCATION;
    }
}

/*MAIN */

int main(void) {
    SetRandomSeed((unsigned int)time(NULL));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Black Hole Observer");
    SetWindowMinSize(480, 320);
    SetTargetFPS(60);
    InitStars();

    g_target = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(g_target.texture, TEXTURE_FILTER_BILINEAR);

    {
        Image winIcon = LoadImageFromResource("ICON_PNG");
        if (winIcon.data != NULL) {
            SetWindowIcon(winIcon);
        }
        UnloadImage(winIcon);
    }

    const char* fontCandidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (int i = 0; i < 4; i++) {
        if (FileExists(fontCandidates[i])) {
            g_font = LoadFontEx(fontCandidates[i], 96, NULL, 0);
            if (g_font.texture.id != 0) {
                SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
                g_usingCustomFont = true;
                break;
            }
        }
    }
    if (!g_usingCustomFont) g_font = GetFontDefault();

    {
        Image bgImg = LoadImageFromResource("BACKGROUND_PNG");
        if (bgImg.data != NULL) {
            g_bgTexture = LoadTextureFromImage(bgImg);
            UnloadImage(bgImg);
            if (g_bgTexture.id != 0) {
                SetTextureFilter(g_bgTexture, TEXTURE_FILTER_BILINEAR);
                g_bgLoaded = true;
            }
        }
    }

    {
        Image iconImg = LoadImageFromResource("ICON_PNG");
        if (iconImg.data != NULL) {
            g_iconTexture = LoadTextureFromImage(iconImg);
            UnloadImage(iconImg);
            if (g_iconTexture.id != 0) {
                SetTextureFilter(g_iconTexture, TEXTURE_FILTER_BILINEAR);
                g_iconLoaded = true;
            }
        }
    }

    while (!WindowShouldClose()) {
        g_time += GetFrameTime();

        /* F11 for fullscreen  */
        if (IsKeyPressed(KEY_F11)) {
            ToggleFullscreen();
        }

        int winW = GetScreenWidth();
        int winH = GetScreenHeight();
        if (winW < 1) winW = 1;
        if (winH < 1) winH = 1;
        g_renderScale = fminf((float)winW / SCREEN_W, (float)winH / SCREEN_H);
        float destW = SCREEN_W * g_renderScale;
        float destH = SCREEN_H * g_renderScale;
        g_renderOffset.x = (winW - destW) / 2.0f;
        g_renderOffset.y = (winH - destH) / 2.0f;

        BeginTextureMode(g_target);
        ClearBackground((Color) { 8, 9, 16, 255 });

        switch (g_screen) {
        case SCR_WELCOME:     ScreenWelcome();     break;
        case SCR_SELECT_BH:   ScreenSelectBH();     break;
        case SCR_SELECT_DIST: ScreenSelectDist();   break;
        case SCR_SELECT_TEL:  ScreenSelectTel();    break;
        case SCR_VIEW:        ScreenView();         break;
        case SCR_EDUCATION:   ScreenEducation();    break;
        case SCR_COMPARE:     ScreenCompare();      break;
        }
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);

        Rectangle src = { 0.0f, 0.0f, (float)g_target.texture.width, -(float)g_target.texture.height };
        Rectangle dst = { g_renderOffset.x, g_renderOffset.y, destW, destH };
        DrawTexturePro(g_target.texture, src, dst, (Vector2) { 0, 0 }, 0.0f, WHITE);

        EndDrawing();
    }

    UnloadRenderTexture(g_target);
    if (g_compareTexLoaded) UnloadTexture(g_compareTex);
    if (g_bgLoaded) UnloadTexture(g_bgTexture);
    if (g_iconLoaded) UnloadTexture(g_iconTexture);
    if (g_usingCustomFont) UnloadFont(g_font);
    CloseWindow();
    return 0;
}
