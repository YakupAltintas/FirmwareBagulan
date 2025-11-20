#include "configuration.h"
#if HAS_SCREEN
#include "../Screen.h"
#include "DebugRenderer.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "UIRenderer.h"
#include "airtime.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "sleep.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#include <WiFi.h>
#ifdef ARCH_ESP32
#include "mesh/wifi/WiFiAPClient.h"
#endif
#endif

#ifdef ARCH_ESP32
#include "modules/StoreForwardModule.h"
#endif
#include <DisplayFormatters.h>
#include <RadioLibInterface.h>
#include <target_specific.h>

using namespace meshtastic;

// External variables
extern graphics::Screen *screen;
extern PowerStatus *powerStatus;
extern NodeStatus *nodeStatus;
extern GPSStatus *gpsStatus;
extern Channels channels;
extern AirTime *airTime;

// External functions from Screen.cpp
extern bool heartbeat;

#ifdef ARCH_ESP32
extern StoreForwardModule *storeForwardModule;
#endif

namespace graphics
{
namespace DebugRenderer
{

void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char channelStr[20];
    snprintf(channelStr, sizeof(channelStr), "#%s", channels.getName(channels.getPrimaryIndex()));
    // Display nodes status
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        UIRenderer::drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 2, nodeStatus);
    } else {
        UIRenderer::drawNodes(display, x + (SCREEN_WIDTH * 0.25), y + 3, nodeStatus);
    }
#if HAS_GPS
    // Display GPS status
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        UIRenderer::drawGpsPowerStatus(display, x, y + 2, gpsStatus);
    } else {
        if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
            UIRenderer::drawGps(display, x + (SCREEN_WIDTH * 0.63), y + 2, gpsStatus);
        } else {
            UIRenderer::drawGps(display, x + (SCREEN_WIDTH * 0.63), y + 3, gpsStatus);
        }
    }
#endif
    display->setColor(WHITE);
    // Draw the channel name
    display->drawString(x, y + FONT_HEIGHT_SMALL, channelStr);
    // Draw our hardware ID to assist with bluetooth pairing. Either prefix with Info or S&F Logo
    if (moduleConfig.store_forward.enabled) {
#ifdef ARCH_ESP32
        if (!Throttle::isWithinTimespanMs(storeForwardModule->lastHeartbeat,
                                          (storeForwardModule->heartbeatInterval * 1200))) { // no heartbeat, overlap a bit
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || defined(ST7796_CS) ||             \
     ARCH_PORTDUINO) &&                                                                                                          \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 12,
                                   8, imgQuestionL1);
            display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 12,
                                   8, imgQuestionL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 8,
                                   8, imgQuestion);
#endif
        } else {
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || defined(ST7796_CS)) &&            \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 16,
                                   8, imgSFL1);
            display->drawFastImage(x + SCREEN_WIDTH - 18 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 16,
                                   8, imgSFL2);
#else
            display->drawFastImage(x + SCREEN_WIDTH - 13 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 11,
                                   8, imgSF);
#endif
        }
#endif
    } else {
        // TODO: Raspberry Pi supports more than just the one screen size
#if (defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7735_CS) ||      \
     defined(ST7789_CS) || defined(USE_ST7789) || defined(ILI9488_CS) || defined(HX8357_CS) || defined(ST7796_CS) ||             \
     ARCH_PORTDUINO) &&                                                                                                          \
    !defined(DISPLAY_FORCE_SMALL_FONTS)
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 3 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL1);
        display->drawFastImage(x + SCREEN_WIDTH - 14 - display->getStringWidth(screen->ourId), y + 11 + FONT_HEIGHT_SMALL, 12, 8,
                               imgInfoL2);
#else
        display->drawFastImage(x + SCREEN_WIDTH - 10 - display->getStringWidth(screen->ourId), y + 2 + FONT_HEIGHT_SMALL, 8, 8,
                               imgInfo);
#endif
    }

    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(screen->ourId), y + FONT_HEIGHT_SMALL, screen->ourId);

    // Draw any log messages
    display->drawLogBuffer(x, y + (FONT_HEIGHT_SMALL * 2));

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

// ****************************
// * WiFi Screen              *
// ****************************
void drawFrameWiFi(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    // === Set Title
    const char *titleStr = "WiFi";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    const char *wifiName = config.network.wifi_ssid;

    if (WiFi.status() != WL_CONNECTED) {
        display->drawString(x, getTextPositions(display)[line++], "WiFi: Not Connected");
    } else {
        display->drawString(x, getTextPositions(display)[line++], "WiFi: Connected");

        char rssiStr[32];
        snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d", WiFi.RSSI());
        display->drawString(x, getTextPositions(display)[line++], rssiStr);
    }

    /*
    - WL_CONNECTED: assigned when connected to a WiFi network;
    - WL_NO_SSID_AVAIL: assigned when no SSID are available;
    - WL_CONNECT_FAILED: assigned when the connection fails for all the attempts;
    - WL_CONNECTION_LOST: assigned when the connection is lost;
    - WL_DISCONNECTED: assigned when disconnected from a network;
    - WL_IDLE_STATUS: it is a temporary status assigned when WiFi.begin() is called and remains active until the number of
    attempts expires (resulting in WL_CONNECT_FAILED) or a connection is established (resulting in WL_CONNECTED);
    - WL_SCAN_COMPLETED: assigned when the scan networks is completed;
    - WL_NO_SHIELD: assigned when no WiFi shield is present;

    */
    if (WiFi.status() == WL_CONNECTED) {
        char ipStr[64];
        snprintf(ipStr, sizeof(ipStr), "IP: %s", WiFi.localIP().toString().c_str());
        display->drawString(x, getTextPositions(display)[line++], ipStr);
    } else if (WiFi.status() == WL_NO_SSID_AVAIL) {
        display->drawString(x, getTextPositions(display)[line++], "SSID Not Found");
    } else if (WiFi.status() == WL_CONNECTION_LOST) {
        display->drawString(x, getTextPositions(display)[line++], "Connection Lost");
    } else if (WiFi.status() == WL_IDLE_STATUS) {
        display->drawString(x, getTextPositions(display)[line++], "Idle ... Reconnecting");
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        display->drawString(x, getTextPositions(display)[line++], "Connection Failed");
    }
#ifdef ARCH_ESP32
    else {
        // Codes:
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-reason-code
        display->drawString(x, getTextPositions(display)[line++],
                            WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(getWifiDisconnectReason())));
    }
#else
    else {
        char statusStr[32];
        snprintf(statusStr, sizeof(statusStr), "Unknown status: %d", WiFi.status());
        display->drawString(x, getTextPositions(display)[line++], statusStr);
    }
#endif

    char ssidStr[64];
    snprintf(ssidStr, sizeof(ssidStr), "SSID: %s", wifiName);
    display->drawString(x, getTextPositions(display)[line++], ssidStr);

    display->drawString(x, getTextPositions(display)[line++], "URL: http://meshtastic.local");

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
#endif
}

void drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED) {
        display->fillRect(0 + x, 0 + y, x + display->getWidth(), y + FONT_HEIGHT_SMALL);
        display->setColor(BLACK);
    }

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "B %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');

        // Line 1
        display->drawString(x, y, batStr);
        if (config.display.heading_bold)
            display->drawString(x + 1, y, batStr);
    } else {
        // Line 1
        display->drawString(x, y, "USB");
        if (config.display.heading_bold)
            display->drawString(x + 1, y, "USB");
    }

    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    // currentMillis %= 1000;
    // seconds %= 60;
    // minutes %= 60;
    // hours %= 24;

    // Show uptime as days, hours, minutes OR seconds
    std::string uptime = UIRenderer::drawTimeDelta(days, hours, minutes, seconds);

    // Line 1 (Still)
#if !defined(M5STACK_UNITC6L)
    display->drawString(x + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());
    if (config.display.heading_bold)
        display->drawString(x - 1 + SCREEN_WIDTH - display->getStringWidth(uptime.c_str()), y, uptime.c_str());

    display->setColor(WHITE);
#endif
    // Setup string to assemble analogClock string
    std::string analogClock = "";

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN

        char timebuf[12];

        if (config.display.use_12h_clock) {
            std::string meridiem = "am";
            if (hour >= 12) {
                if (hour > 12)
                    hour -= 12;
                meridiem = "pm";
            }
            if (hour == 00) {
                hour = 12;
            }
            snprintf(timebuf, sizeof(timebuf), "%d:%02d:%02d%s", hour, min, sec, meridiem.c_str());
        } else {
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
        }
        analogClock += timebuf;
    }

    // Line 2
    display->drawString(x, y + FONT_HEIGHT_SMALL * 1, analogClock.c_str());

    // Display Channel Utilization
    //char chUtil[13];
    //snprintf(chUtil, sizeof(chUtil), "ChUtil %2.0f%%", airTime->channelUtilizationPercent());
    //display->drawString(x + SCREEN_WIDTH - display->getStringWidth(chUtil), y + FONT_HEIGHT_SMALL * 1, chUtil);

#if HAS_GPS
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        // Line 3
        if (uiconfig.gps_format != meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS) // if DMS then don't draw altitude
            UIRenderer::drawGpsAltitude(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);

        // Line 4
        UIRenderer::drawGpsCoordinates(display, x, y + FONT_HEIGHT_SMALL * 3, gpsStatus);
    } else {
        UIRenderer::drawGpsPowerStatus(display, x, y + FONT_HEIGHT_SMALL * 2, gpsStatus);
    }
#endif
/* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}

// Trampoline functions for DebugInfo class access
void drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrame(display, state, x, y);
}

void drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrameSettings(display, state, x, y);
}

void drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    drawFrameWiFi(display, state, x, y);
}

// ****************************
// * LoRa Focused Screen      *
// ****************************
void drawLoRaFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    // Başlangıç satırı (dinamik kaydırma uygulanacak)
    int line = 1;
    
    // === Set Title
    const char *titleStr = (isHighResolution) ? "LongRange Info" : "LongRange";

    // Header en sonda çizilecek (alt bar)
    graphics::drawCommonHeader(display, x, y, titleStr);

    // İçeriği biraz yukarı taşımak için offset
    const int lift = isHighResolution ? 12 : 6;

    // === ChUtil bar: Role kısmının Üstü ===
    int chutil_percent = 0;
    if (airTime) chutil_percent = airTime->channelUtilizationPercent();
    if (chutil_percent < 0) chutil_percent = 0;
    if (chutil_percent > 100) chutil_percent = 100;

    const int barWidth  = isHighResolution ? 100 : 50;
    const int barHeight = isHighResolution ? 12  : 7;
    const int barGap    = 2; // role ile bar arası boşluk

    // Dinamik: bar yüksekliğine göre kaç satır yer kaydırmamız gerektiğini hesapla
    int pageShiftLines = (barHeight + barGap + (FONT_HEIGHT_SMALL - 1)) / FONT_HEIGHT_SMALL; // roundup
    int roleLine = line + pageShiftLines; // role bu satırda çizilecek

    // bar, roleLine'in üstüne yerleşecek (y hesaplanıyor)
    int barY = getTextPositions(display)[roleLine] - lift - barHeight - (barGap);

    // CHANGED: Label+Bar birlikte ortalanacak. totalWidth = label + padding + bar
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    int labelWidth = display->getStringWidth("CU:");
    int labelPadding = isHighResolution ? 6 : 3; // arasındaki boşluk
    int totalWidth = labelWidth + labelPadding + barWidth;
    int centerX = SCREEN_WIDTH / 2;
    int startX = centerX - (totalWidth / 2);
    if (startX < x) startX = x; // sol kenarı aşmasın
    int labelX = startX;
    int labelY = barY + (barHeight - FONT_HEIGHT_SMALL) / 2;
    int barX = labelX + labelWidth + labelPadding;
    // Çiz
    display->drawString(labelX, labelY, "CU:");

    // Bar çerçevesi ve dolgu
    display->drawRect(barX, barY, barWidth, barHeight);
    int fillW = (barWidth * chutil_percent) / 100;
    if (fillW > 0) {
        display->fillRect(barX, barY, fillW, barHeight);
    }

    // === Role ===
    auto role = DisplayFormatters::getDeviceRole(config.device.role);
    char device_role[25];
    snprintf(device_role, sizeof(device_role), "Role: %s", role);
    int textWidth = display->getStringWidth(device_role);
    int nameX = (SCREEN_WIDTH - textWidth) / 2;
    // Role'ü roleLine'a göre çiz
    display->drawString(nameX, getTextPositions(display)[roleLine] - lift, device_role);
    // Sonraki içerik roleLine+1'den başlayacak
    line = roleLine + 1;

    // === Region / Modem Preset ===
    auto mode = DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset);
    char regionradiopreset[25];
    const char *region = myRegion ? myRegion->name : NULL;
    if (region != nullptr) {
#if defined(M5STACK_UNITC6L)
        snprintf(regionradiopreset, sizeof(regionradiopreset), "%s", region);
#else
        snprintf(regionradiopreset, sizeof(regionradiopreset), "%s/%s", region, mode);
#endif
        textWidth = display->getStringWidth(regionradiopreset);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line++] - lift, regionradiopreset);
    }
    // === Bottom header: battery/time/title ===
}

// ****************************
// *      System Screen       *
// ****************************
void drawSystemScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Set Title
    const char *titleStr = "System";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === Layout ===
    int line = 1;
    // Ensure nameX/textWidth are defined for later centered text calculations
    int textWidth = 0;
    int nameX = 0;
    const int barHeight = 6;
    const int labelX = x;
    int barsOffset = (isHighResolution) ? 24 : 0;
#ifdef USE_EINK
    barsOffset -= 12;
#endif
#if defined(M5STACK_UNITC6L)
    const int barX = x + 45 + barsOffset;
#else
    const int barX = x + 40 + barsOffset;
#endif
    auto drawUsageRow = [&](const char *label, uint32_t used, uint32_t total, bool isHeap = false) {
        if (total == 0)
            return;

        int percent = (used * 100) / total;

        char combinedStr[24];
        if (isHighResolution) {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%  %u/%uKB", (percent > 80) ? "! " : "", percent, used / 1024,
                     total / 1024);
        } else {
            snprintf(combinedStr, sizeof(combinedStr), "%s%3d%%", (percent > 80) ? "! " : "", percent);
        }

        int textWidth = display->getStringWidth(combinedStr);
        int adjustedBarWidth = SCREEN_WIDTH - barX - textWidth - 6;
        if (adjustedBarWidth < 10)
            adjustedBarWidth = 10;

        int fillWidth = (used * adjustedBarWidth) / total;

        // Label
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(labelX, getTextPositions(display)[line], label);
#if !defined(M5STACK_UNITC6L)
        // Bar
        int barY = getTextPositions(display)[line] + (FONT_HEIGHT_SMALL - barHeight) / 2;
        display->setColor(WHITE);
        display->drawRect(barX, barY, adjustedBarWidth, barHeight);

        display->fillRect(barX, barY, fillWidth, barHeight);
        display->setColor(WHITE);
#endif
        // Value string
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(SCREEN_WIDTH - 2, getTextPositions(display)[line], combinedStr);
    };

    // === Memory values ===
    uint32_t heapUsed = memGet.getHeapSize() - memGet.getFreeHeap();
    uint32_t heapTotal = memGet.getHeapSize();

    uint32_t psramUsed = memGet.getPsramSize() - memGet.getFreePsram();
    uint32_t psramTotal = memGet.getPsramSize();

    uint32_t flashUsed = 0, flashTotal = 0;
#ifdef ESP32
    flashUsed = FSCom.usedBytes();
    flashTotal = FSCom.totalBytes();
#endif

    uint32_t sdUsed = 0, sdTotal = 0;
    bool hasSD = false;
    /*
    #ifdef HAS_SDCARD
        hasSD = SD.cardType() != CARD_NONE;
        if (hasSD) {
            sdUsed = SD.usedBytes();
            sdTotal = SD.totalBytes();
        }
    #endif
    */
    // === Draw memory rows
    drawUsageRow("Heap:", heapUsed, heapTotal, true);
#ifdef ESP32
    if (psramUsed > 0) {
        line += 1;
        drawUsageRow("PSRAM:", psramUsed, psramTotal);
    }
    if (flashTotal > 0) {
        line += 1;
        drawUsageRow("Flash:", flashUsed, flashTotal);
    }
     // --- Static hint under the heap/uptime area ---
    // Draw a centered small-font hint that says "Long Press For Menu" just below the
    // stats area. This is purely visual (no behavior attached).
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char *hint = "Long Press For Menu";
    int hintLine = line + 1; // position the hint after the last drawn line
    if (hintLine > 5)
        hintLine = 5; // clamp into getTextPositions range
    int hintY = getTextPositions(display)[hintLine];
    // fallback: ensure it's visible and inside the screen
    if (hintY < 0 || hintY > (SCREEN_HEIGHT - FONT_HEIGHT_SMALL))
        hintY = SCREEN_HEIGHT - FONT_HEIGHT_SMALL - 1;
    // Ensure color is white so the text is visible against the background
    display->setColor(WHITE);
    display->drawString(SCREEN_WIDTH / 2, hintY, hint);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
#endif
    if (hasSD && sdTotal > 0) {
        line += 1;
        drawUsageRow("SD:", sdUsed, sdTotal);
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    // System Uptime
    if (line < 2) {
        line += 1;
    }
    line += 1;

    // Ensure appversionstr is initialized to avoid garbage characters on screen
    char appversionstr[35] = {0};
    // If you have a build-time version macro, fill it here e.g.:
    // snprintf(appversionstr, sizeof(appversionstr), "Ver: %s", optstr(APP_VERSION));
    char appversionstr_formatted[40] = {0};

    if (appversionstr[0] != '\0') {
        char *lastDot = strrchr(appversionstr, '.');
#if defined(M5STACK_UNITC6L)
        if (lastDot != nullptr) {
            *lastDot = '\0'; // truncate string
        }
#else
        if (lastDot) {
            size_t prefixLen = lastDot - appversionstr;
            strncpy(appversionstr_formatted, appversionstr, prefixLen);
            appversionstr_formatted[prefixLen] = '\0';
            strncat(appversionstr_formatted, " (", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncat(appversionstr_formatted, lastDot + 1, sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncat(appversionstr_formatted, ")", sizeof(appversionstr_formatted) - strlen(appversionstr_formatted) - 1);
            strncpy(appversionstr, appversionstr_formatted, sizeof(appversionstr) - 1);
            appversionstr[sizeof(appversionstr) - 1] = '\0';
        }
#endif
    }

    // Only draw the version string if it's non-empty (prevents garbage display)
    if (appversionstr[0] != '\0') {
        int textWidth = display->getStringWidth(appversionstr);
        int nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line], appversionstr);
    }
#if !defined(M5STACK_UNITC6L)
    if (SCREEN_HEIGHT > 64 || (SCREEN_HEIGHT <= 64 && line < 4)) { // Only show uptime if the screen can show it
        line += 1;
        char uptimeStr[32] = "";
        uint32_t uptime = millis() / 1000;
        uint32_t days = uptime / 86400;
        uint32_t hours = (uptime % 86400) / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        // Show as "Up: 2d 3h", "Up: 5h 14m", or "Up: 37m"
        //Up yazisi
        // if (days)
        //     snprintf(uptimeStr, sizeof(uptimeStr), " Up: %ud %uh", days, hours);
        // else if (hours)
        //     snprintf(uptimeStr, sizeof(uptimeStr), " Up: %uh %um", hours, mins);
        // else
        //     snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %um", mins);
        textWidth = display->getStringWidth(uptimeStr);
        nameX = (SCREEN_WIDTH - textWidth) / 2;
        display->drawString(nameX, getTextPositions(display)[line], uptimeStr);
    }
#endif
}

// ****************************
// * Chirpy Screen      *
// ****************************
void drawChirpy(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;
    int iconX = SCREEN_WIDTH - chirpy_width - (chirpy_width / 3);
    int iconY = (SCREEN_HEIGHT - chirpy_height) / 2;
    int textX_offset = 10;
    if (isHighResolution) {
        iconX = SCREEN_WIDTH - chirpy_width_hirez - (chirpy_width_hirez / 3);
        iconY = (SCREEN_HEIGHT - chirpy_height_hirez) / 2;
        textX_offset = textX_offset * 4;
        display->drawXbm(iconX, iconY, chirpy_width_hirez, chirpy_height_hirez, chirpy_hirez);
    } else {
        display->drawXbm(iconX, iconY, chirpy_width, chirpy_height, chirpy);
    }

    int textX = (display->getWidth() / 2) - textX_offset - (display->getStringWidth("Hello") / 2);
    display->drawString(textX, getTextPositions(display)[line++], "Hello");
    textX = (display->getWidth() / 2) - textX_offset - (display->getStringWidth("World!") / 2);
    display->drawString(textX, getTextPositions(display)[line++], "World!");
}

} // namespace DebugRenderer
} // namespace graphics
#endif