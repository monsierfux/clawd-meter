// Clawd — animated pixel-mascot channel.
//
// Inspired by clawd-mochi (https://github.com/yousifamanuel/clawd-mochi, MIT,
// © 2026 Yousuf Amanuel) — the eye/expression animation idea, re-implemented on
// TFT_eSPI for the 320x240 CYD and wired to Claude usage.
//
//   AUTO mode   → the expression reflects your Claude 5-hour usage:
//                 relaxed/happy when there's plenty left, normal mid-way,
//                 stressed near the limit, sleepy when there's no data.
//   MANUAL mode → you pick a fixed expression in the web UI
//                 (normal / squish / code / logo).
//
// Partial-redraw discipline: draw() paints everything; tick() only repaints the
// eye band (blink/wiggle) and the footer when something changes.

#include "channel.h"
#include "display.h"
#include "theme.h"
#include "config.h"
#include "api.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ── Geometry (320x240 landscape) ──
// Square eyes (mochi style), wide gap between them.
static const int EYE_CX_L = 92;
static const int EYE_CX_R = 228;
static const int EYE_CY   = 104;    // sit a little above center (mochi look)
static const int EYE_W    = 78;
static const int EYE_H    = 84;
static const int LOOK_MAX = 14;     // max horizontal pupil wiggle

enum Expr { EX_NORMAL, EX_HAPPY, EX_SQUISH, EX_STRESSED, EX_SLEEPY, EX_CODE, EX_LOGO };

// ── tick cache ──
static int      s_expr       = -1;
static int      s_lookX      = 0;
static int      s_blinkPhase = 0;   // 0 open · 1 closed · 2 brief-open · 3 closed (double-blink)
static uint32_t s_blinkT     = 0;
static uint32_t s_lastWiggle = 0;
static uint32_t s_lastBlink  = 0;
static bool     s_squishClosed = false;
static uint32_t s_squishT    = 0;
static uint16_t s_eyeCol     = 0;
static uint16_t s_bgCol      = 0xFFFF;
static char     s_footer[40] = "";

bool chClawdEnabled(const ChannelCtx& ctx) {
    return ctx.settings && ctx.settings->showClawd;
}

// Current resolved eye color — used by main.cpp's rotation indicator so the
// progress strip stays visible against the Clawd background.
uint16_t clawdAccentColor() { return s_eyeCol; }

// ── helpers ──

// Parse a "#RRGGBB" / "RRGGBB" hex string into RGB565. Returns false if not hex.
static bool parseHex565(const String& s, uint16_t& out) {
    String h = s;
    if (h.startsWith("#")) h = h.substring(1);
    if (h.length() != 6) return false;
    char* end = nullptr;
    long v = strtol(h.c_str(), &end, 16);
    if (!end || *end != '\0') return false;
    uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    out = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return true;
}

static uint16_t eyeColor(const ChannelCtx& ctx) {
    const String& n = ctx.settings->clawdEyeColor;
    uint16_t hx; if (parseHex565(n, hx)) return hx;
    if (n == "black") return Theme::BG;          // 0x0000 (use with a colored bg)
    uint16_t c = Theme::namedColor(n.c_str());
    return c ? c : Theme::SKY;
}
static uint16_t bgColor(const ChannelCtx& ctx) {
    const String& n = ctx.settings->clawdBgColor;
    uint16_t hx; if (parseHex565(n, hx)) return hx;
    return Theme::namedColor(n.c_str());         // "black"/unknown → 0x0000
}

// Resolve the expression to show + the Claude 5-hour "used" value (or -1).
static Expr resolveExpr(const ChannelCtx& ctx, float& usedOut) {
    usedOut = -1.f;
    if (ctx.settings->clawdMode == "manual") {
        const String& e = ctx.settings->clawdExpr;
        if (e == "happy")  return EX_HAPPY;
        if (e == "squish") return EX_SQUISH;
        if (e == "code")   return EX_CODE;
        if (e == "logo")   return EX_LOGO;
        return EX_NORMAL;
    }
    float p = ctx.claude ? ctx.claude->sessionPct : -1.f;
    if (p < 0) return EX_SLEEPY;
    float used = ctx.settings->usageShowConsumed ? p : (100.f - p);
    usedOut = used;
    if (used >= 85.f) return EX_STRESSED;   // near limit
    if (used <  40.f) return EX_HAPPY;      // lots left  → ^ ^
    if (used <  60.f) return EX_SQUISH;     // good       → > <
    return EX_NORMAL;
}

static bool isAnimated(int e) { return e == EX_NORMAL || e == EX_STRESSED || e == EX_SQUISH; }

// Clear the rectangular band that holds both eyes (generous for wiggle).
static void clearEyeBand(uint16_t bg) {
    tft.fillRect(EYE_CX_L - EYE_W/2 - LOOK_MAX - 6, EYE_CY - EYE_H/2 - 32,
                 (EYE_CX_R - EYE_CX_L) + EYE_W + 2*(LOOK_MAX + 6), EYE_H + 72, bg);
}

// Blocky thick segment with SQUARE ends (two filled triangles) — gives the
// angular, no-anti-alias look of the mochi eyes.
static void thickLine(int x0, int y0, int x1, int y1, int w, uint16_t col) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float ox = -dy / len * (w * 0.5f), oy = dx / len * (w * 0.5f);
    int ax = lroundf(x0 + ox), ay = lroundf(y0 + oy);
    int bx = lroundf(x0 - ox), by = lroundf(y0 - oy);
    int cx = lroundf(x1 - ox), cy = lroundf(y1 - oy);
    int ex = lroundf(x1 + ox), ey = lroundf(y1 + oy);
    tft.fillTriangle(ax, ay, bx, by, cx, cy, col);
    tft.fillTriangle(ax, ay, cx, cy, ex, ey, col);
}

static void drawOpenEye(int cx, int lookX, int h, uint16_t col) {
    tft.fillRect(cx - EYE_W/2 + lookX, EYE_CY - h/2, EYE_W, h, col);
}
static void drawBlinkEye(int cx, uint16_t col) {
    tft.fillRect(cx - EYE_W/2, EYE_CY - 6, EYE_W, 12, col);
}
// Happy "^ ^": blocky caret, apex on top. A filled dot at the apex cleans the joint.
static void drawHappyEye(int cx, uint16_t col) {
    int half = EYE_W/2, v = EYE_H/3, t = 14;
    thickLine(cx - half, EYE_CY + v, cx,        EYE_CY - v, t, col);
    thickLine(cx,        EYE_CY - v, cx + half, EYE_CY + v, t, col);
    tft.fillCircle(cx, EYE_CY - v, t/2, col);
}
// Squish "> <": blocky chevron — left eye ">", right eye "<".
static void drawSquishEye(int cx, bool pointRight, uint16_t col) {
    int half = EYE_W/2, v = EYE_H/3, t = 14;
    int ax = pointRight ? cx + half : cx - half;   // apex x
    if (pointRight) {   // ">"  apex on the right
        thickLine(cx - half, EYE_CY - v, cx + half, EYE_CY,     t, col);
        thickLine(cx + half, EYE_CY,     cx - half, EYE_CY + v, t, col);
    } else {            // "<"  apex on the left
        thickLine(cx + half, EYE_CY - v, cx - half, EYE_CY,     t, col);
        thickLine(cx - half, EYE_CY,     cx + half, EYE_CY + v, t, col);
    }
    tft.fillCircle(ax, EYE_CY, t/2, col);          // clean the apex joint
}
static void drawSleepyEye(int cx, uint16_t col) {
    tft.fillRect(cx - EYE_W/2, EYE_CY - 4, EYE_W, 8, col);
}
// Worried brows, sitting well above the eyes (inner ends raised).
static void drawBrow(int cx, bool leftBrow, uint16_t col) {
    int half = EYE_W/2;
    int yHi = EYE_CY - EYE_H/2 - 26;   // inner (raised)
    int yLo = EYE_CY - EYE_H/2 - 12;   // outer
    if (leftBrow) thickLine(cx - half, yLo, cx + half, yHi, 9, col);  // "/"
    else          thickLine(cx - half, yHi, cx + half, yLo, 9, col);  // "\"
}

// Paint the eye pair for the current expression + animation state.
static void paintEyes(int expr, int lookX, bool blink, uint16_t eyeCol, uint16_t bg) {
    clearEyeBand(bg);
    switch (expr) {
        case EX_HAPPY:
            drawHappyEye(EYE_CX_L, eyeCol);
            drawHappyEye(EYE_CX_R, eyeCol);
            break;
        case EX_SQUISH:
            if (blink) { drawBlinkEye(EYE_CX_L, eyeCol); drawBlinkEye(EYE_CX_R, eyeCol); }
            else {
                drawSquishEye(EYE_CX_L, true,  eyeCol);
                drawSquishEye(EYE_CX_R, false, eyeCol);
            }
            break;
        case EX_SLEEPY:
            drawSleepyEye(EYE_CX_L, eyeCol);
            drawSleepyEye(EYE_CX_R, eyeCol);
            break;
        case EX_STRESSED: {
            // Mood is conveyed by the worried brows + shape, not by recoloring —
            // the user's chosen eye color is always respected.
            if (blink) { drawBlinkEye(EYE_CX_L, eyeCol); drawBlinkEye(EYE_CX_R, eyeCol); }
            else {
                drawOpenEye(EYE_CX_L, lookX, EYE_H, eyeCol);
                drawOpenEye(EYE_CX_R, lookX, EYE_H, eyeCol);
            }
            drawBrow(EYE_CX_L, true,  eyeCol);
            drawBrow(EYE_CX_R, false, eyeCol);
            break;
        }
        case EX_NORMAL:
        default:
            if (blink) { drawBlinkEye(EYE_CX_L, eyeCol); drawBlinkEye(EYE_CX_R, eyeCol); }
            else {
                drawOpenEye(EYE_CX_L, lookX, EYE_H, eyeCol);
                drawOpenEye(EYE_CX_R, lookX, EYE_H, eyeCol);
            }
            break;
    }
}

static void buildFooter(const ChannelCtx& ctx, char* out, size_t n) {
    float s = ctx.claude ? ctx.claude->sessionPct : -1.f;
    float w = ctx.claude ? ctx.claude->weeklyPct  : -1.f;
    const char* tag = ctx.settings->usageShowConsumed ? "USED" : "LEFT";
    char a[18], b[18];
    if (s < 0) snprintf(a, sizeof(a), "5H --"); else snprintf(a, sizeof(a), "5H %.0f%% %s", s, tag);
    if (w < 0) snprintf(b, sizeof(b), "7D --"); else snprintf(b, sizeof(b), "7D %.0f%% %s", w, tag);
    snprintf(out, n, "%s \xC2\xB7 %s", a, b);   // "5H 42% LEFT · 7D 88% LEFT"
}

static void paintFooter(const ChannelCtx& ctx, uint16_t bg) {
    buildFooter(ctx, s_footer, sizeof(s_footer));
    tft.fillRect(0, 212, SCREEN_W, 20, bg);
    Display::useFont("DMMono-11");
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(eyeColor(ctx), bg);        // match the eyes / accent
    tft.drawString(s_footer, SCREEN_W/2, 220);
}

// ── full draw ──
void chClawdDraw(const ChannelCtx& ctx) {
    uint16_t eyeCol = eyeColor(ctx);
    uint16_t bg     = bgColor(ctx);
    float used; Expr expr = resolveExpr(ctx, used);

    tft.fillScreen(bg);

    if (expr == EX_CODE) {
        // "Claude Code" splash with accent bar
        tft.fillRect(0, EYE_CY - 24, SCREEN_W, 4, Theme::CORAL);
        Display::useFont("Silkscreen-16");
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Theme::INK, bg);
        tft.drawString("Claude Code", SCREEN_W/2, EYE_CY + 8);
    } else if (expr == EX_LOGO) {
        Display::drawLogo(SCREEN_W/2 - 8, EYE_CY - 28, Theme::AMBER);
        Display::useFont("Silkscreen-16");
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Theme::INK, bg);
        tft.drawString("CLAWD", SCREEN_W/2, EYE_CY + 8);
    } else {
        s_lookX = 0; s_blinkPhase = 0; s_squishClosed = false;
        paintEyes(expr, 0, false, eyeCol, bg);
    }

    paintFooter(ctx, bg);

    // seed cache
    s_expr = expr; s_eyeCol = eyeCol; s_bgCol = bg;
    s_lastWiggle = s_lastBlink = s_blinkT = s_squishT = ctx.now_ms;
}

// ── tick: blink / wiggle / footer ──
void chClawdTick(const ChannelCtx& ctx) {
    uint16_t eyeCol = eyeColor(ctx);
    uint16_t bg     = bgColor(ctx);
    float used; Expr expr = resolveExpr(ctx, used);

    // Expression or colors changed → full repaint.
    if ((int)expr != s_expr || eyeCol != s_eyeCol || bg != s_bgCol) {
        chClawdDraw(ctx);
        return;
    }

    // Footer follows the live usage value.
    char fresh[40]; buildFooter(ctx, fresh, sizeof(fresh));
    if (strcmp(fresh, s_footer) != 0) paintFooter(ctx, bg);

    if (!isAnimated(expr)) return;

    int speed = ctx.settings->clawdSpeed; if (speed < 1) speed = 1; if (speed > 3) speed = 3;
    uint32_t now = ctx.now_ms;

    // Squish: gently squint open/closed (chevron <-> line).
    if (expr == EX_SQUISH) {
        if (!s_squishClosed && now - s_squishT >= (uint32_t)(1600 / speed)) {
            s_squishClosed = true;  s_squishT = now; paintEyes(expr, 0, true,  eyeCol, bg);
        } else if (s_squishClosed && now - s_squishT >= 180) {
            s_squishClosed = false; s_squishT = now; paintEyes(expr, 0, false, eyeCol, bg);
        }
        return;
    }

    // Normal / stressed: double-blink (closed→open→closed→open) + slow wiggle.
    uint32_t wiggleEvery = 2400 / speed;
    uint32_t blinkEvery  = 10000 / speed;    // less frequent, calmer
    if (s_blinkPhase == 0) {
        if (now - s_lastBlink >= blinkEvery) {
            s_blinkPhase = 1; s_blinkT = now; paintEyes(expr, s_lookX, true,  eyeCol, bg);
        }
    } else if (s_blinkPhase == 1 && now - s_blinkT >= 280) {
        s_blinkPhase = 2; s_blinkT = now; paintEyes(expr, s_lookX, false, eyeCol, bg);
    } else if (s_blinkPhase == 2 && now - s_blinkT >= 200) {
        s_blinkPhase = 3; s_blinkT = now; paintEyes(expr, s_lookX, true,  eyeCol, bg);
    } else if (s_blinkPhase == 3 && now - s_blinkT >= 280) {
        s_blinkPhase = 0; s_blinkT = now; s_lastBlink = now;
        paintEyes(expr, s_lookX, false, eyeCol, bg);
    }

    // Wiggle only while fully open (not mid-blink).
    if (s_blinkPhase == 0 && now - s_lastWiggle >= wiggleEvery) {
        s_lastWiggle = now;
        int next = (s_lookX == 0) ? LOOK_MAX : (s_lookX > 0 ? -LOOK_MAX : 0);
        s_lookX = next;
        paintEyes(expr, s_lookX, false, eyeCol, bg);
    }
}
