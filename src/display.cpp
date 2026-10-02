#include "display.h"

#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include "OpenFontRender.h"

#include "board.h"
#include "version.h"
#include "timeconst.h"
#include "priceService.h"
#include "media/fonts.h"

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COL_BG       RGB565(13, 17, 23)
#define COL_PANEL    RGB565(30, 36, 44)
#define COL_GRID     RGB565(48, 54, 62)
#define COL_TEXT     RGB565(240, 243, 246)
#define COL_MUTED    RGB565(139, 148, 158)
#define COL_UP       RGB565(35, 197, 130)
#define COL_DOWN     RGB565(240, 72, 80)
#define COL_WARN     RGB565(230, 170, 40)
#define COL_UP_DIM   RGB565(16, 58, 44)
#define COL_DOWN_DIM RGB565(68, 26, 32)

#define HEADER_H     24
#define STALE_MS     (5 * MINUTE_MS)

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static OpenFontRender render;

static int s_screen = 0;
static bool s_dirty = true;

/******************** Helpers ********************/

enum TextAlign { TA_LEFT, TA_CENTER, TA_RIGHT };

// y is the top of the text line
static void text(const char *str, int x, int y, int size, uint16_t fg, uint16_t bg, TextAlign align = TA_LEFT)
{
    render.setFontSize(size);
    switch (align)
    {
    case TA_LEFT:   render.drawString(str, x, y, fg, bg); break;
    case TA_CENTER: render.cdrawString(str, x, y, fg, bg); break;
    case TA_RIGHT:  render.rdrawString(str, x, y, fg, bg); break;
    }
}

static int textWidth(const char *str, int size)
{
    render.setFontSize(size);
    return render.getTextWidth("%s", str);
}

// Largest font size (in steps of 4) that fits the given width
static int fitSize(const char *str, int maxSize, int maxWidth)
{
    int size = maxSize;
    while (size > 12 && textWidth(str, size) > maxWidth)
        size -= 4;
    return size;
}

static void fmtPrice(double price, char *buf, size_t len)
{
    int decimals = price >= 100 ? 2 : price >= 1 ? 4 : price >= 0.01 ? 5 : 8;
    snprintf(buf, len, "%.*f", decimals, price);
}

static void fmtCompact(double value, char *buf, size_t len)
{
    static const char *suffix[] = {"", "K", "M", "B", "T"};
    int i = 0;
    while (value >= 1000 && i < 4)
    {
        value /= 1000;
        i++;
    }
    snprintf(buf, len, "%.2f%s", value, suffix[i]);
}

static bool localNow(struct tm &out)
{
    time_t now = time(nullptr);
    if (now < 1700000000) // NTP not synced yet
        return false;
    localtime_r(&now, &out);
    return true;
}

// Approximation of the XRP mark: two mirrored curved chevrons
static void drawXrpLogo(int cx, int cy, int r, uint16_t color, uint16_t bg)
{
    const float a = r * 0.85f;
    const float gap = r * 0.2f;
    const float width = max(2.0f, r * 0.24f);
    const int steps = 10;

    for (int side = -1; side <= 1; side += 2) // -1 upper chevron, +1 lower chevron
    {
        float x0 = cx - a, y0 = cy + side * a;
        float x1 = cx,     y1 = cy - side * (a - 2 * gap);
        float x2 = cx + a, y2 = cy + side * a;

        float px = x0, py = y0;
        for (int i = 1; i <= steps; i++)
        {
            float t = (float)i / steps, u = 1 - t;
            float x = u * u * x0 + 2 * u * t * x1 + t * t * x2;
            float y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
            spr.drawWideLine(px, py, x, y, width, color, bg);
            px = x;
            py = y;
        }
    }
}

// Coloured triangle + percentage, anchored at x according to align
static void drawChange(double change, int x, int y, int size, uint16_t bg, TextAlign align)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.2f%%", change);
    bool up = change >= 0;
    uint16_t color = up ? COL_UP : COL_DOWN;

    int tri = size * 6 / 10;
    int gap = size / 3;
    int total = tri + gap + textWidth(buf, size);
    int left = align == TA_LEFT ? x : align == TA_CENTER ? x - total / 2 : x - total;

    int midY = y + size * 55 / 100;
    int half = tri / 2;
    if (up)
        spr.fillTriangle(left, midY + half, left + tri, midY + half, left + half, midY - half, color);
    else
        spr.fillTriangle(left, midY - half, left + tri, midY - half, left + half, midY + half, color);

    text(buf, left + tri + gap, y, size, color, bg);
}

static void drawWaiting(const PriceData &d)
{
    char buf[40];
    if (WiFi.status() != WL_CONNECTED)
        snprintf(buf, sizeof(buf), "Sin conexion WiFi");
    else if (d.lastError != 0)
        snprintf(buf, sizeof(buf), "Error API (%d), reintentando", d.lastError);
    else
        snprintf(buf, sizeof(buf), "Obteniendo precio...");
    text(buf, SCREEN_WIDTH / 2, 76, 16, COL_MUTED, COL_BG, TA_CENTER);
}

static void drawHeader(const PriceData &d, bool showClock)
{
    spr.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, COL_PANEL);
    drawXrpLogo(14, 12, 8, COL_TEXT, COL_PANEL);

    char pair[16];
    snprintf(pair, sizeof(pair), "XRP / %s", d.currency);
    text(pair, 30, 4, 15, COL_TEXT, COL_PANEL);

    // Status: green = fresh data, amber = stale data, red = no WiFi
    uint16_t status = WiFi.status() != WL_CONNECTED                  ? COL_DOWN
                      : (d.valid && millis() - d.fetchedMs < STALE_MS) ? COL_UP
                                                                       : COL_WARN;
    spr.fillSmoothCircle(SCREEN_WIDTH - 10, HEADER_H / 2, 4, status, COL_PANEL);

    const int right = SCREEN_WIDTH - 22;
    if (showClock)
    {
        struct tm now;
        char buf[8] = "--:--";
        if (localNow(now))
            strftime(buf, sizeof(buf), "%H:%M", &now);
        text(buf, right, 4, 15, COL_TEXT, COL_PANEL, TA_RIGHT);
    }
    else if (d.valid)
    {
        char change[16], price[24];
        snprintf(change, sizeof(change), "%+.2f%%", d.change24h);
        fmtPrice(d.price, price, sizeof(price));
        text(change, right, 4, 15, d.change24h >= 0 ? COL_UP : COL_DOWN, COL_PANEL, TA_RIGHT);
        text(price, right - textWidth(change, 15) - 8, 4, 15, COL_TEXT, COL_PANEL, TA_RIGHT);
    }
}

/******************** Screens ********************/

static void screenPrice(const PriceData &d)
{
    drawHeader(d, true);
    if (!d.valid)
        return drawWaiting(d);

    char buf[24];
    fmtPrice(d.price, buf, sizeof(buf));
    text(buf, SCREEN_WIDTH / 2, 32, fitSize(buf, 72, SCREEN_WIDTH - 24), COL_TEXT, COL_BG, TA_CENTER);

    drawChange(d.change24h, SCREEN_WIDTH / 2, 108, 20, COL_BG, TA_CENTER);

    if (d.chartValid)
    {
        char high[24], low[24];
        fmtPrice(d.high24h, high, sizeof(high));
        fmtPrice(d.low24h, low, sizeof(low));

        text("MAX", 12, 141, 12, COL_MUTED, COL_BG);
        text(high, 44, 139, 15, COL_TEXT, COL_BG);

        int lowWidth = textWidth(low, 15);
        text(low, SCREEN_WIDTH - 12, 139, 15, COL_TEXT, COL_BG, TA_RIGHT);
        text("MIN", SCREEN_WIDTH - 12 - lowWidth - 6, 141, 12, COL_MUTED, COL_BG, TA_RIGHT);
    }
}

static void screenChart(const PriceData &d)
{
    drawHeader(d, true);
    if (!d.chartValid)
        return drawWaiting(d);

    // 24h closes plus the latest spot price
    float pts[CHART_MAX_POINTS + 1];
    int n = d.chartCount;
    memcpy(pts, d.chart, sizeof(float) * n);
    if (d.valid)
        pts[n++] = d.price;

    float lo = pts[0], hi = pts[0];
    for (int i = 1; i < n; i++)
    {
        lo = min(lo, pts[i]);
        hi = max(hi, pts[i]);
    }
    if (hi - lo < hi * 1e-6f) // Flat line: avoid dividing by zero
    {
        hi += hi * 0.001f;
        lo -= lo * 0.001f;
    }

    const int x0 = 8, x1 = 246, y0 = 34, y1 = 138;
    bool up = pts[n - 1] >= pts[0];
    uint16_t lineColor = up ? COL_UP : COL_DOWN;
    uint16_t fillColor = up ? COL_UP_DIM : COL_DOWN_DIM;

    auto X = [&](int i) { return x0 + (float)i * (x1 - x0) / (n - 1); };
    auto Y = [&](float v) { return y1 - (v - lo) / (hi - lo) * (y1 - y0); };

    for (int i = 0; i <= 2; i++)
    {
        int y = y0 + i * (y1 - y0) / 2;
        for (int x = x0; x < x1; x += 6)
            spr.drawFastHLine(x, y, 3, COL_GRID);
    }

    // Area under the curve, then the line on top
    for (int i = 0; i < n - 1; i++)
    {
        float xa = X(i), xb = X(i + 1), ya = Y(pts[i]), yb = Y(pts[i + 1]);
        for (int x = (int)ceilf(xa); x <= (int)xb; x++)
        {
            int y = ya + (x - xa) / (xb - xa) * (yb - ya);
            spr.drawFastVLine(x, y, y1 - y + 1, fillColor);
        }
    }
    for (int i = 0; i < n - 1; i++)
        spr.drawWideLine(X(i), Y(pts[i]), X(i + 1), Y(pts[i + 1]), 2, lineColor);
    spr.fillSmoothCircle(X(n - 1), Y(pts[n - 1]), 3, lineColor);

    // Right column: axis range and current price
    char buf[24];
    const int right = SCREEN_WIDTH - 8;
    fmtPrice(hi, buf, sizeof(buf));
    text(buf, right, y0 - 4, 13, COL_MUTED, COL_BG, TA_RIGHT);
    fmtPrice(lo, buf, sizeof(buf));
    text(buf, right, y1 - 12, 13, COL_MUTED, COL_BG, TA_RIGHT);
    fmtPrice(pts[n - 1], buf, sizeof(buf));
    text(buf, right, 72, fitSize(buf, 18, SCREEN_WIDTH - x1 - 12), lineColor, COL_BG, TA_RIGHT);

    text("Ultimas 24h", x0, 144, 13, COL_MUTED, COL_BG);
    if (d.valid)
        drawChange(d.change24h, right, 143, 14, COL_BG, TA_RIGHT);
}

static void screenClock(const PriceData &d)
{
    static const char *days[] = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};
    static const char *months[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun",
                                   "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
    drawHeader(d, false);

    struct tm now;
    if (!localNow(now))
    {
        text("--:--", SCREEN_WIDTH / 2, 36, 76, COL_MUTED, COL_BG, TA_CENTER);
        text("Sincronizando hora...", SCREEN_WIDTH / 2, 126, 16, COL_MUTED, COL_BG, TA_CENTER);
        return;
    }

    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", &now);
    text(buf, SCREEN_WIDTH / 2, 36, 76, COL_TEXT, COL_BG, TA_CENTER);

    snprintf(buf, sizeof(buf), "%s %02d %s %04d", days[now.tm_wday], now.tm_mday, months[now.tm_mon], now.tm_year + 1900);
    text(buf, SCREEN_WIDTH / 2, 126, 18, COL_MUTED, COL_BG, TA_CENTER);
}

// Days since 1970-01-01 for a civil date (proleptic Gregorian)
static long daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int daysInMonth(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

// EU rule: summer time from the last Sunday of March to the last Sunday of October, at 01:00 UTC
static time_t euDstSwitch(int year, int month)
{
    long last = daysFromCivil(year, month, daysInMonth(year, month));
    long weekday = (last + 4) % 7; // 1970-01-01 was a Thursday; 0 = Sunday
    return (time_t)(last - weekday) * 86400 + 3600;
}

// Madrid local time, independent of the timezone configured in the portal
static bool madridNow(struct tm &out, bool &summer)
{
    time_t now = time(nullptr);
    if (now < 1700000000) // NTP not synced yet
        return false;

    struct tm utc;
    gmtime_r(&now, &utc);
    int year = utc.tm_year + 1900;
    summer = now >= euDstSwitch(year, 3) && now < euDstSwitch(year, 10);

    time_t local = now + (summer ? 2 : 1) * 3600;
    gmtime_r(&local, &out);
    return true;
}

static void screenCalendar(const PriceData &d)
{
    static const char *days[] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
    static const char *months[] = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio",
                                   "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};
    static const char *weekHeader[] = {"L", "M", "X", "J", "V", "S", "D"};
    drawHeader(d, false);

    struct tm now;
    bool summer;
    if (!madridNow(now, summer))
    {
        text("Sincronizando hora...", SCREEN_WIDTH / 2, 76, 16, COL_MUTED, COL_BG, TA_CENTER);
        return;
    }

    // Left column: Madrid clock and date
    char buf[32];
    text("MADRID", 12, 30, 12, COL_MUTED, COL_BG);
    strftime(buf, sizeof(buf), "%H:%M", &now);
    text(buf, 10, 44, 38, COL_TEXT, COL_BG);
    int clockWidth = textWidth(buf, 38);
    snprintf(buf, sizeof(buf), "%02d", now.tm_sec);
    text(buf, 10 + clockWidth + 4, 62, 16, COL_MUTED, COL_BG);

    text(days[now.tm_wday], 12, 92, 18, COL_TEXT, COL_BG);
    snprintf(buf, sizeof(buf), "%d %s %d", now.tm_mday, months[now.tm_mon], now.tm_year + 1900);
    text(buf, 12, 116, 14, COL_MUTED, COL_BG);
    text(summer ? "UTC+2 (verano)" : "UTC+1 (invierno)", 12, 138, 12, COL_GRID, COL_BG);

    // Right column: month grid, Monday first
    const int gx = 152, cellW = 23, cellH = 15, gy = 58;
    const int gridCenter = gx + cellW * 7 / 2;
    snprintf(buf, sizeof(buf), "%s %d", months[now.tm_mon], now.tm_year + 1900);
    text(buf, gridCenter, 28, 13, COL_TEXT, COL_BG, TA_CENTER);

    for (int c = 0; c < 7; c++)
        text(weekHeader[c], gx + c * cellW + cellW / 2, 44, 11, c >= 5 ? COL_DOWN : COL_MUTED, COL_BG, TA_CENTER);

    int firstWeekday = ((now.tm_wday - (now.tm_mday - 1)) % 7 + 7) % 7; // 0 = Sunday
    int col = (firstWeekday + 6) % 7;                                  // 0 = Monday
    int total = daysInMonth(now.tm_year + 1900, now.tm_mon + 1);
    int row = 0;
    for (int day = 1; day <= total; day++)
    {
        int x = gx + col * cellW;
        int y = gy + row * cellH;
        snprintf(buf, sizeof(buf), "%d", day);
        if (day == now.tm_mday)
        {
            spr.fillSmoothRoundRect(x + 1, y - 1, cellW - 2, cellH, 4, COL_UP, COL_BG);
            text(buf, x + cellW / 2, y, 11, COL_BG, COL_UP, TA_CENTER);
        }
        else
        {
            text(buf, x + cellW / 2, y, 11, col >= 5 ? COL_MUTED : COL_TEXT, COL_BG, TA_CENTER);
        }
        if (++col == 7)
        {
            col = 0;
            row++;
        }
    }
}

static void screenMarket(const PriceData &d)
{
    drawHeader(d, true);
    if (!d.valid)
        return drawWaiting(d);

    char values[6][40];
    char tmp[24];
    const char *labels[6] = {"Cap. mercado", "Volumen 24h", "Max 24h", "Min 24h", "Actualizado", "WiFi"};

    fmtCompact(d.marketCap, tmp, sizeof(tmp));
    snprintf(values[0], sizeof(values[0]), "%s %s", tmp, d.currency);
    fmtCompact(d.volume24h, tmp, sizeof(tmp));
    snprintf(values[1], sizeof(values[1]), "%s %s", tmp, d.currency);

    if (d.chartValid)
    {
        fmtPrice(d.high24h, values[2], sizeof(values[2]));
        fmtPrice(d.low24h, values[3], sizeof(values[3]));
    }
    else
    {
        strcpy(values[2], "--");
        strcpy(values[3], "--");
    }

    strcpy(values[4], "--");
    if (d.updatedAt > 0)
    {
        time_t updated = d.updatedAt;
        struct tm t;
        localtime_r(&updated, &t);
        strftime(values[4], sizeof(values[4]), "%H:%M:%S", &t);
    }

    snprintf(values[5], sizeof(values[5]), "%d dBm  %s", WiFi.RSSI(), WiFi.localIP().toString().c_str());

    for (int i = 0; i < 6; i++)
    {
        int y = 30 + i * 21;
        if (i > 0)
            spr.drawFastHLine(12, y - 3, SCREEN_WIDTH - 24, COL_GRID);
        text(labels[i], 12, y, 14, COL_MUTED, COL_BG);
        text(values[i], SCREEN_WIDTH - 12, y, 14, COL_TEXT, COL_BG, TA_RIGHT);
    }
}

typedef void (*ScreenFunction)(const PriceData &);
static const ScreenFunction screens[] = {screenPrice, screenChart, screenClock, screenCalendar, screenMarket};
static const int screenCount = sizeof(screens) / sizeof(screens[0]);

static void drawPager()
{
    const int spacing = 10;
    int x = SCREEN_WIDTH / 2 - (screenCount - 1) * spacing / 2;
    for (int i = 0; i < screenCount; i++, x += spacing)
        spr.fillSmoothCircle(x, SCREEN_HEIGHT - 6, 2, i == s_screen ? COL_TEXT : COL_GRID, COL_BG);
}

/******************** Public API ********************/

void displayInit()
{
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(COL_BG);

    spr.setColorDepth(16);
    if (!spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT))
        Serial.println("Display: sprite allocation failed");

    render.setDrawer(spr);
    if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
        Serial.println("Display: font load error");
}

void displayLoadingScreen(const char *status)
{
    spr.fillSprite(COL_BG);
    drawXrpLogo(SCREEN_WIDTH / 2, 56, 30, COL_TEXT, COL_BG);
    text(APP_NAME, SCREEN_WIDTH / 2, 96, 26, COL_TEXT, COL_BG, TA_CENTER);
    text(status, SCREEN_WIDTH / 2, 134, 14, COL_MUTED, COL_BG, TA_CENTER);
    text(CURRENT_VERSION, SCREEN_WIDTH - 8, 154, 11, COL_GRID, COL_BG, TA_RIGHT);
    spr.pushSprite(0, 0);
}

void displaySetupScreen(const char *apName, const char *apPassword)
{
    spr.fillSprite(COL_BG);
    drawXrpLogo(24, 22, 13, COL_TEXT, COL_BG);
    text("Configuracion", 48, 9, 22, COL_TEXT, COL_BG);

    const char *labels[] = {"Red WiFi", "Clave", "Web"};
    const char *values[] = {apName, apPassword, "192.168.4.1"};
    for (int i = 0; i < 3; i++)
    {
        int y = 50 + i * 26;
        text(labels[i], 16, y + 2, 14, COL_MUTED, COL_BG);
        text(values[i], 100, y, 17, COL_TEXT, COL_BG);
    }

    text("Elige tu WiFi, moneda y zona horaria", SCREEN_WIDTH / 2, 140, 13, COL_MUTED, COL_BG, TA_CENTER);
    spr.pushSprite(0, 0);
}

void displayDraw()
{
    PriceData data = priceServiceGet();
    spr.fillSprite(COL_BG);
    screens[s_screen](data);
    drawPager();
    spr.pushSprite(0, 0);
}

void displayNextScreen()
{
    s_screen = (s_screen + 1) % screenCount;
    s_dirty = true;
}

bool displayConsumeDirty()
{
    bool dirty = s_dirty;
    s_dirty = false;
    return dirty;
}

void displayToggleBacklight()
{
    digitalWrite(TFT_BL, !digitalRead(TFT_BL));
}

void displayFlipRotation()
{
    tft.setRotation((tft.getRotation() + 2) % 4);
    s_dirty = true;
}
