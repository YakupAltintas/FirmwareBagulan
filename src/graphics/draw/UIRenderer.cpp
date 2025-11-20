#include "configuration.h"
#if HAS_SCREEN
#include "CompassRenderer.h"
#include "GPSStatus.h"
#include "NodeDB.h"
#include "NodeListRenderer.h"
#include "UIRenderer.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <RTC.h>
#include <cstring>

// External variables
extern graphics::Screen *screen;
static uint32_t lastSwitchTime = 0;
namespace graphics
{
NodeNum UIRenderer::currentFavoriteNodeNum = 0;
std::vector<meshtastic_NodeInfoLite *> graphics::UIRenderer::favoritedNodes;

void graphics::UIRenderer::rebuildFavoritedNodes()
{
    favoritedNodes.clear();
    size_t total = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < total; i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum())
            continue;
        if (n->is_favorite)
            favoritedNodes.push_back(n);
    }

    std::sort(favoritedNodes.begin(), favoritedNodes.end(),
              [](const meshtastic_NodeInfoLite *a, const meshtastic_NodeInfoLite *b) { return a->num < b->num; });
}

#if !MESHTASTIC_EXCLUDE_GPS
// GeoCoord object for coordinate conversions
extern GeoCoord geoCoord;

// Threshold values for the GPS lock accuracy bar display
extern uint32_t dopThresholds[5];

// Draw GPS status summary
void UIRenderer::drawGps(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    // CHANGED: satellite icon removed; only text is drawn
    char textString[10];

    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        snprintf(textString, sizeof(textString), "Fixed");
    }
    if (!gps->getIsConnected()) {
        snprintf(textString, sizeof(textString), "No Lock");
    }
    if (!gps->getHasLock()) {
        // Draw "No sats" to the right of the icon with slightly more gap
        snprintf(textString, sizeof(textString), "No Sats");
    } else {
        snprintf(textString, sizeof(textString), "%u sats", gps->getNumSatellites());
    }
    // Keep previous horizontal offset roughly (no icon), or center if x==0
    if (x == 0) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(display->getWidth() / 2, y, textString);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
    } else {
        int xoff = isHighResolution ? 12 : 8;
        display->drawString(x + xoff, y, textString);
    }
}

// Draw status when GPS is disabled or not present
void UIRenderer::drawGpsPowerStatus(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    const char *displayLine;
    int pos;
    if (y < FONT_HEIGHT_SMALL) { // Line 1: use short string
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        pos = display->getWidth() - display->getStringWidth(displayLine);
    } else {
        displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "GPS not present"
                                                                                                       : "GPS is disabled";
        pos = (display->getWidth() - display->getStringWidth(displayLine)) / 2;
    }
    display->drawString(x + pos, y, displayLine);
}

void UIRenderer::drawGpsAltitude(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps)
{
    char displayLine[32];
    if (!gps->getIsConnected() && !config.position.fixed_position) {
        // displayLine = "No GPS Module";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        // displayLine = "No GPS Lock";
        // display->drawString(x + (SCREEN_WIDTH - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    } else {
        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL)
            snprintf(displayLine, sizeof(displayLine), "Altitude: %.0fft", geoCoord.getAltitude() * METERS_TO_FEET);
        else
            snprintf(displayLine, sizeof(displayLine), "Altitude: %.0im", geoCoord.getAltitude());
        display->drawString(x + (display->getWidth() - (display->getStringWidth(displayLine))) / 2, y, displayLine);
    }
}

// Draw GPS status coordinates
void UIRenderer::drawGpsCoordinates(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::GPSStatus *gps,
                                    const char *mode)
{
    auto gpsFormat = uiconfig.gps_format;
    char displayLine[32];

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        strcpy(displayLine, "No GPS present");
        display->drawString(x, y, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        strcpy(displayLine, "No GPS Lock");
        display->drawString(x, y, displayLine);
    } else {

        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));

        if (gpsFormat != meshtastic_DeviceUIConfig_GpsCoordinateFormat_DMS) {
            char coordinateLine_1[22];
            char coordinateLine_2[22];
            if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC) { // Decimal Degrees
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "Lat: %f", geoCoord.getLatitude() * 1e-7);
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "Lon: %f", geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_UTM) { // Universal Transverse Mercator
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%2i%1c %06u E", geoCoord.getUTMZone(),
                         geoCoord.getUTMBand(), geoCoord.getUTMEasting());
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%07u N", geoCoord.getUTMNorthing());
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_MGRS) { // Military Grid Reference System
                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%2i%1c %1c%1c", geoCoord.getMGRSZone(),
                         geoCoord.getMGRSBand(), geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k());
                snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%05u E %05u N", geoCoord.getMGRSEasting(),
                         geoCoord.getMGRSNorthing());
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine_1);
                coordinateLine_2[0] = '\0';
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_OSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') { // OSGR is only valid around the UK region
                    snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%s", "Out of Boundary");
                    coordinateLine_2[0] = '\0';
                } else {
                    snprintf(coordinateLine_1, sizeof(coordinateLine_1), "%1c%1c", geoCoord.getOSGRE100k(),
                             geoCoord.getOSGRN100k());
                    snprintf(coordinateLine_2, sizeof(coordinateLine_2), "%05u E %05u N", geoCoord.getOSGREasting(),
                             geoCoord.getOSGRNorthing());
                }
            } else if (gpsFormat == meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS) { // Maidenhead Locator System
                double lat = geoCoord.getLatitude() * 1e-7;
                double lon = geoCoord.getLongitude() * 1e-7;

                // Normalize
                if (lat > 90.0)
                    lat = 90.0;
                if (lat < -90.0)
                    lat = -90.0;
                while (lon < -180.0)
                    lon += 360.0;
                while (lon >= 180.0)
                    lon -= 360.0;

                double adjLon = lon + 180.0;
                double adjLat = lat + 90.0;

                char maiden[10]; // enough for 8-char + null

                // Field (2 letters)
                int lonField = int(adjLon / 20.0);
                int latField = int(adjLat / 10.0);
                adjLon -= lonField * 20.0;
                adjLat -= latField * 10.0;

                // Square (2 digits)
                int lonSquare = int(adjLon / 2.0);
                int latSquare = int(adjLat / 1.0);
                adjLon -= lonSquare * 2.0;
                adjLat -= latSquare * 1.0;

                // Subsquare (2 letters)
                double lonUnit = 2.0 / 24.0;
                double latUnit = 1.0 / 24.0;
                int lonSub = int(adjLon / lonUnit);
                int latSub = int(adjLat / latUnit);

                snprintf(maiden, sizeof(maiden), "%c%c%c%c%c%c", 'A' + lonField, 'A' + latField, '0' + lonSquare, '0' + latSquare,
                         'A' + lonSub, 'A' + latSub);

                snprintf(coordinateLine_1, sizeof(coordinateLine_1), "MH: %s", maiden);
                coordinateLine_2[0] = '\0'; // only need one line
            }

            if (strcmp(mode, "line1") == 0) {
                display->drawString(x, y, coordinateLine_1);
            } else if (strcmp(mode, "line2") == 0) {
                display->drawString(x, y, coordinateLine_2);
            } else if (strcmp(mode, "combined") == 0) {
                display->drawString(x, y, coordinateLine_1);
                if (coordinateLine_2[0] != '\0') {
                    display->drawString(x + display->getStringWidth(coordinateLine_1), y, coordinateLine_2);
                }
            }

        } else {
            char coordinateLine_1[22];
            char coordinateLine_2[22];
            snprintf(coordinateLine_1, sizeof(coordinateLine_1), "Lat: %2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(),
                     geoCoord.getDMSLatMin(), geoCoord.getDMSLatSec(), geoCoord.getDMSLatCP());
            snprintf(coordinateLine_2, sizeof(coordinateLine_2), "Lon: %3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(),
                     geoCoord.getDMSLonMin(), geoCoord.getDMSLonSec(), geoCoord.getDMSLonCP());
            if (strcmp(mode, "line1") == 0) {
                display->drawString(x, y, coordinateLine_1);
            } else if (strcmp(mode, "line2") == 0) {
                display->drawString(x, y, coordinateLine_2);
            } else { // both
                display->drawString(x, y, coordinateLine_1);
                display->drawString(x, y + 10, coordinateLine_2);
            }
        }
    }
}
#endif // !MESHTASTIC_EXCLUDE_GPS

// Draw nodes status
void UIRenderer::drawNodes(OLEDDisplay *display, int16_t x, int16_t y, const meshtastic::NodeStatus *nodeStatus, int node_offset,
                           bool show_total, String additional_words)
{
    char usersString[20];
    int nodes_online = (nodeStatus->getNumOnline() > 0) ? nodeStatus->getNumOnline() + node_offset : 0;

    snprintf(usersString, sizeof(usersString), "%d %s", nodes_online, additional_words.c_str());

    if (show_total) {
        int nodes_total = (nodeStatus->getNumTotal() > 0) ? nodeStatus->getNumTotal() + node_offset : 0;
        snprintf(usersString, sizeof(usersString), "%d/%d %s", nodes_online, nodes_total, additional_words.c_str());
    }

    // CHANGED: ikonlar kaldırıldı — sadece yazıyı çiz
    display->drawString(x + 0, y - 2, usersString);
}

// **********************
// * Favorite Node Info *
// **********************
void UIRenderer::drawNodeInfo(OLEDDisplay *display, const OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (favoritedNodes.empty())
        return;

    // --- Only display if index is valid ---
    int nodeIndex = state->currentFrame - (screen->frameCount - favoritedNodes.size());
    if (nodeIndex < 0 || nodeIndex >= (int)favoritedNodes.size())
        return;

    meshtastic_NodeInfoLite *node = favoritedNodes[nodeIndex];
    if (!node || node->num == nodeDB->getNodeNum() || !node->is_favorite)
        return;
    uint32_t now = millis();
    display->clear();
#if defined(M5STACK_UNITC6L)
    if (now - lastSwitchTime >= 10000) // 10000 ms = 10 秒
    {
        display->display();
        lastSwitchTime = now;
    }
#endif
    currentFavoriteNodeNum = node->num;
    // === Create the shortName and title string ===
    const char *shortName = (node->has_user && haveGlyphs(node->user.short_name)) ? node->user.short_name : "Node";
    char titlestr[32] = {0};
    snprintf(titlestr, sizeof(titlestr), "Fav: %s", shortName);

    // === Draw battery/time/mail header (common across screens) ===
    graphics::drawCommonHeader(display, x, y, titlestr);

    // ===== DYNAMIC ROW STACKING WITH YOUR MACROS =====
    // 1. Each potential info row has a macro-defined Y position (not regular increments!).
    // 2. Each row is only shown if it has valid data.
    // 3. Each row "moves up" if previous are empty, so there are never any blank rows.
    // 4. The first line is ALWAYS at your macro position; subsequent lines use the next available macro slot.

    // List of available macro Y positions in order, from top to bottom.
    int line = 1; // which slot to use next
    std::string usernameStr;
    // === 1. Long Name (always try to show first) ===
#if defined(M5STACK_UNITC6L)
    const char *username = (node->has_user && node->user.long_name[0]) ? node->user.short_name : nullptr;
#else
    const char *username = (node->has_user && node->user.long_name[0]) ? node->user.long_name : nullptr;
#endif

    if (username) {
        usernameStr = sanitizeString(username); // Sanitize the incoming long_name just in case
        // Print node's long name (e.g. "Backpack Node")
        display->drawString(x, getTextPositions(display)[line++], usernameStr.c_str());
    }

    // === 2. Signal and Hops (combined on one line, if available) ===
    // If both are present: "Sig: 97%  [2hops]"
    // If only one: show only that one
    char signalHopsStr[32] = "";
    bool haveSignal = false;
    int percentSignal = clamp((int)((node->snr + 10) * 5), 0, 100);

    // Always use "Sig" for the label
    const char *signalLabel = " Sig";

    // --- Build the Signal/Hops line ---
    // If SNR looks reasonable, show signal
    if ((int)((node->snr + 10) * 5) >= 0 && node->snr > -100) {
        snprintf(signalHopsStr, sizeof(signalHopsStr), "%s: %d%%", signalLabel, percentSignal);
        haveSignal = true;
    }
    // If hops is valid (>0), show right after signal
    if (node->hops_away > 0) {
        size_t len = strlen(signalHopsStr);
        // Decide between "1 Hop" and "N Hops"
        if (haveSignal) {
            snprintf(signalHopsStr + len, sizeof(signalHopsStr) - len, " [%d %s]", node->hops_away,
                     (node->hops_away == 1 ? "Hop" : "Hops"));
        } else {
            snprintf(signalHopsStr, sizeof(signalHopsStr), "[%d %s]", node->hops_away, (node->hops_away == 1 ? "Hop" : "Hops"));
        }
    }
    if (signalHopsStr[0] && line < 5) {
        display->drawString(x, getTextPositions(display)[line++], signalHopsStr);
    }

    // === 3. Heard (last seen, skip if node never seen) ===
    char seenStr[20] = "";
    uint32_t seconds = sinceLastSeen(node);
    if (seconds != 0 && seconds != UINT32_MAX) {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        // Format as "Heard: Xm ago", "Heard: Xh ago", or "Heard: Xd ago"
        snprintf(seenStr, sizeof(seenStr), (days > 365 ? " Heard: ?" : " Heard: %d%c ago"),
                 (days    ? days
                  : hours ? hours
                          : minutes),
                 (days    ? 'd'
                  : hours ? 'h'
                          : 'm'));
    }
    if (seenStr[0] && line < 5) {
        //display->drawString(x, getTextPositions(display)[line++], seenStr);
    }
#if !defined(M5STACK_UNITC6L)
    // === 4. Uptime (only show if metric is present) ===
    char uptimeStr[32] = "";
    if (node->has_device_metrics && node->device_metrics.has_uptime_seconds) {
        uint32_t uptime = node->device_metrics.uptime_seconds;
        uint32_t days = uptime / 86400;
        uint32_t hours = (uptime % 86400) / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        // Show as "Up: 2d 3h", "Up: 5h 14m", or "Up: 37m"
        if (days)
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %ud %uh", days, hours);
        else if (hours)
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %uh %um", hours, mins);
        else
            snprintf(uptimeStr, sizeof(uptimeStr), " Uptime: %um", mins);
    }
    if (uptimeStr[0] && line < 5) {
        display->drawString(x, getTextPositions(display)[line++], uptimeStr);
    }

    // === 5. Distance (only if both nodes have GPS position) ===
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    char distStr[24] = ""; // Make buffer big enough for any string
    bool haveDistance = false;

    if (nodeDB->hasValidPosition(ourNode) && nodeDB->hasValidPosition(node)) {
        double lat1 = ourNode->position.latitude_i * 1e-7;
        double lon1 = ourNode->position.longitude_i * 1e-7;
        double lat2 = node->position.latitude_i * 1e-7;
        double lon2 = node->position.longitude_i * 1e-7;
        double earthRadiusKm = 6371.0;
        double dLat = (lat2 - lat1) * DEG_TO_RAD;
        double dLon = (lon2 - lon1) * DEG_TO_RAD;
        double a =
            sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        double distanceKm = earthRadiusKm * c;

        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            double miles = distanceKm * 0.621371;
            if (miles < 0.1) {
                int feet = (int)(miles * 5280);
                if (feet > 0 && feet < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dft", feet);
                    haveDistance = true;
                } else if (feet >= 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: ¼mi");
                    haveDistance = true;
                }
            } else {
                int roundedMiles = (int)(miles + 0.5);
                if (roundedMiles > 0 && roundedMiles < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dmi", roundedMiles);
                    haveDistance = true;
                }
            }
        } else {
            if (distanceKm < 1.0) {
                int meters = (int)(distanceKm * 1000);
                if (meters > 0 && meters < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dm", meters);
                    haveDistance = true;
                } else if (meters >= 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: 1km");
                    haveDistance = true;
                }
            } else {
                int km = (int)(distanceKm + 0.5);
                if (km > 0 && km < 1000) {
                    snprintf(distStr, sizeof(distStr), " Distance: %dkm", km);
                    haveDistance = true;
                }
            }
        }
    }
    // Only display if we actually have a value!
    if (haveDistance && distStr[0] && line < 5) {
       // display->drawString(x, getTextPositions(display)[line++], distStr);
    }

    // --- Compass Rendering: landscape (wide) screens use the original side-aligned logic ---
    if (SCREEN_WIDTH > SCREEN_HEIGHT) {
        bool showCompass = false;
        if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading()) && nodeDB->hasValidPosition(node)) {
            showCompass = true;
        }
        if (showCompass) {
            const int16_t topY = getTextPositions(display)[1];
            const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1);
            const int16_t usableHeight = bottomY - topY - 5;
            int16_t compassRadius = usableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            const int16_t compassDiam = compassRadius * 2;
            const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;
            const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

            const auto &op = ourNode->position;
            float myHeading = screen->hasHeading() ? screen->getHeading() * PI / 180
                                                   : screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));

            const auto &p = node->position;
            /* unused
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            */
            float bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            if (uiconfig.compass_mode == meshtastic_CompassMode_FREEZE_HEADING) {
                myHeading = 0;
            } else {
                bearing -= myHeading;
            }

            display->drawCircle(compassX, compassY, compassRadius);
            CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading, compassRadius);
            CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, bearing);
        }
        // else show nothing
    } else {
        // Portrait or square: put compass at the bottom and centered, scaled to fit available space
        bool showCompass = false;
        if (ourNode && (nodeDB->hasValidPosition(ourNode) || screen->hasHeading()) && nodeDB->hasValidPosition(node)) {
            showCompass = true;
        }
        if (showCompass) {
            int yBelowContent = (line > 0 && line <= 5) ? (getTextPositions(display)[line - 1] + FONT_HEIGHT_SMALL + 2)
                                                        : getTextPositions(display)[1];
            const int margin = 4;
// --------- PATCH FOR EINK NAV BAR (ONLY CHANGE BELOW) -----------
#if defined(USE_EINK)
            const int iconSize = (isHighResolution) ? 16 : 8;
            const int navBarHeight = iconSize + 6;
#else
            const int navBarHeight = 0;
#endif
            int availableHeight = SCREEN_HEIGHT - yBelowContent - navBarHeight - margin;
            // --------- END PATCH FOR EINK NAV BAR -----------

            if (availableHeight < FONT_HEIGHT_SMALL * 2)
                return;

            int compassRadius = availableHeight / 2;
            if (compassRadius < 8)
                compassRadius = 8;
            if (compassRadius * 2 > SCREEN_WIDTH - 16)
                compassRadius = (SCREEN_WIDTH - 16) / 2;

            int compassX = x + SCREEN_WIDTH / 2;
            int compassY = yBelowContent + availableHeight / 2;

            const auto &op = ourNode->position;
            float myHeading = 0;
            if (uiconfig.compass_mode != meshtastic_CompassMode_FREEZE_HEADING) {
                myHeading = screen->hasHeading() ? screen->getHeading() * PI / 180
                                                 : screen->estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
            }
            graphics::CompassRenderer::drawCompassNorth(display, compassX, compassY, myHeading, compassRadius);

            const auto &p = node->position;
            /* unused
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            */
            float bearing = GeoCoord::bearing(DegD(op.latitude_i), DegD(op.longitude_i), DegD(p.latitude_i), DegD(p.longitude_i));
            if (uiconfig.compass_mode != meshtastic_CompassMode_FREEZE_HEADING)
                bearing -= myHeading;
            graphics::CompassRenderer::drawNodeHeading(display, compassX, compassY, compassRadius * 2, bearing);

            display->drawCircle(compassX, compassY, compassRadius);
        }
        // else show nothing
    }
#endif
}

// ****************************
// * Device Focused Screen    *
// ****************************
void UIRenderer::drawDeviceFocused(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    // Varsayılan olarak tüm metinleri ortalayacağız
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    // Dinamik aralıklar: sadece FONT_HEIGHT_SMALL tabanlı

    const int spacing = (FONT_HEIGHT_SMALL * 4) / 5;
    // İçeriği bir yazı boyutu kadar yukarı taşı
    int yCur = getTextPositions(display)[1] - FONT_HEIGHT_SMALL;
    if (yCur < 0)
        yCur = 0;

    // Yalnızca metinleri piksel hassasiyetinde ortalayacağız
    // Piksel hassasiyetinde ortalama: metin genişliğini ölç ve sol x'i hesapla
    auto drawCentered = [&](const char *text, int16_t yPos) {
        if (!text)
            return;
        int16_t w = display->getStringWidth(text);
        int16_t left = x + (display->getWidth() - w) / 2;
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(left, yPos, text);
    };
    

    // === Header ===
#if defined(M5STACK_UNITC6L)
    graphics::drawCommonHeader(display, x, y, "Home");
#else
    graphics::drawCommonHeader(display, x, y, "");
#endif
    

    // (Device name moved to bottom area to match requested layout)

    // 2) Çevrimiçi düğüm sayısı (kendimizi çıkar)
    {
        char usersString[24];
        int nodes_online = (nodeStatus->getNumOnline() > 0) ? (nodeStatus->getNumOnline() - 1) : 0;
        snprintf(usersString, sizeof(usersString), "%d online", nodes_online);
        drawCentered(usersString, yCur);
        yCur += spacing;
    }

    // 3) GPS durumu
#if HAS_GPS
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        const char *displayLine = config.position.fixed_position
                                      ? "Fixed GPS"
                                      : (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS"
                                                                                                                         : "GPS off");
        drawCentered(displayLine, yCur);
        yCur += spacing;
    } else {
        char textString[16];
        if (config.position.fixed_position)
            snprintf(textString, sizeof(textString), "Fixed");
        else if (!gpsStatus->getIsConnected())
            snprintf(textString, sizeof(textString), "No Lock");
        else if (!gpsStatus->getHasLock())
            snprintf(textString, sizeof(textString), "No Sats");
        else
            snprintf(textString, sizeof(textString), "%u sats", gpsStatus->getNumSatellites());
        drawCentered(textString, yCur);
        yCur += spacing;
    }
#endif

    // 4) Uptime
    {
        char uptimeStr[32] = "";
        uint32_t uptime = millis() / 1000;
        uint32_t days = uptime / 86400;
        uint32_t hours = (uptime % 86400) / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        if (days)
            snprintf(uptimeStr, sizeof(uptimeStr), "Up: %ud %uh", days, hours);
        else if (hours)
            snprintf(uptimeStr, sizeof(uptimeStr), "Up: %uh %um", hours, mins);
        else
            snprintf(uptimeStr, sizeof(uptimeStr), "Up: %um", mins);
        drawCentered(uptimeStr, yCur);
    }

    // === App name at bottom (dynamic: OEM text -> node long_name -> owner.short_name -> hw id)
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char *appName = nullptr;
    static char appBuf[64] = {0};
#ifdef USERPREFS_OEM_TEXT
    appName = USERPREFS_OEM_TEXT;
#endif

if (!appName) {
        if (auto *meNode = nodeDB->getMeshNode(nodeDB->getNodeNum()); meNode && meNode->has_user && meNode->user.long_name[0]) {
            std::string sanitized = sanitizeString(meNode->user.long_name);
            strncpy(appBuf, sanitized.c_str(), sizeof(appBuf) - 1);
            appBuf[sizeof(appBuf) - 1] = '\0';
            if (haveGlyphs(appBuf))
                appName = appBuf;
        }
    }
    if (!appName && owner.short_name && owner.short_name[0] && haveGlyphs(owner.short_name))
        appName = owner.short_name;
    if (!appName)
        appName = screen->ourId;

    int appY = y + SCREEN_HEIGHT - FONT_HEIGHT_SMALL - 1;
    if (appY < 0)
        appY = SCREEN_HEIGHT - FONT_HEIGHT_SMALL - 1;
    // Draw small device name just above the app name (small, centered)
    {
        const char *devName = nullptr;
        static char devBuf[64] = {0};
        if (auto *meNode = nodeDB->getMeshNode(nodeDB->getNodeNum()); meNode && meNode->has_user && meNode->user.long_name[0]) {
            std::string sanitized = sanitizeString(meNode->user.long_name);
            strncpy(devBuf, sanitized.c_str(), sizeof(devBuf) - 1);
            devBuf[sizeof(devBuf) - 1] = '\0';
            if (haveGlyphs(devBuf))
                devName = devBuf;
        }
        if (!devName && owner.short_name && owner.short_name[0] && haveGlyphs(owner.short_name))
            devName = owner.short_name;
        if (!devName)
            devName = screen->ourId;

        int devY = appY - FONT_HEIGHT_SMALL - 2;
        if (devY < 0)
            devY = 0;
        display->setFont(FONT_SMALL);
        display->setColor(WHITE);
        drawCentered(devName, devY);
    }

    // Ensure the text color is set so the app/device name is visible
    display->setColor(WHITE);
    display->drawString(SCREEN_WIDTH / 2, appY, appName);
    display->setTextAlignment(TEXT_ALIGN_LEFT);    
}

// Start Functions to write date/time to the screen
// Helper function to check if a year is a leap year
bool isLeapYear(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

// Array of days in each month (non-leap year)
const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Fills the buffer with a formatted date/time string and returns pixel width
int UIRenderer::formatDateTime(char *buf, size_t bufSize, uint32_t rtc_sec, OLEDDisplay *display, bool includeTime)
{
    int sec = rtc_sec % 60;
    rtc_sec /= 60;
    int min = rtc_sec % 60;
    rtc_sec /= 60;
    int hour = rtc_sec % 24;
    rtc_sec /= 24;

    int year = 1970;
    while (true) {
        int daysInYear = isLeapYear(year) ? 366 : 365;
        if (rtc_sec >= (uint32_t)daysInYear) {
            rtc_sec -= daysInYear;
            year++;
        } else {
            break;
        }
    }

    int month = 0;
    while (month < 12) {
        int dim = daysInMonth[month];
        if (month == 1 && isLeapYear(year))
            dim++;
        if (rtc_sec >= (uint32_t)dim) {
            rtc_sec -= dim;
            month++;
        } else {
            break;
        }
    }

    int day = rtc_sec + 1;

    if (includeTime) {
        snprintf(buf, bufSize, "%04d-%02d-%02d %02d:%02d:%02d", year, month + 1, day, hour, min, sec);
    } else {
        snprintf(buf, bufSize, "%04d-%02d-%02d", year, month + 1, day);
    }

    return display->getStringWidth(buf);
}

// Check if the display can render a string (detect special chars; emoji)
bool UIRenderer::haveGlyphs(const char *str)
{
#if defined(OLED_PL) || defined(OLED_UA) || defined(OLED_RU) || defined(OLED_CS)
    // Don't want to make any assumptions about custom language support
    return true;
#endif

    // Check each character with the lookup function for the OLED library
    // We're not really meant to use this directly..
    bool have = true;
    for (uint16_t i = 0; i < strlen(str); i++) {
        uint8_t result = Screen::customFontTableLookup((uint8_t)str[i]);
        // If font doesn't support a character, it is substituted for ¿
        if (result == 191 && (uint8_t)str[i] != 191) {
            have = false;
            break;
        }
    }

    // LOG_DEBUG("haveGlyphs=%d", have);
    return have;
}

#ifdef USE_EINK
/// Used on eink displays while in deep sleep
void UIRenderer::drawDeepSleepFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    // Next frame should use full-refresh, and block while running, else device will sleep before async callback
    EINK_ADD_FRAMEFLAG(display, COSMETIC);
    EINK_ADD_FRAMEFLAG(display, BLOCKING);

    LOG_DEBUG("Draw deep sleep screen");

    // Display displayStr on the screen
    graphics::UIRenderer::drawIconScreen("Sleeping", display, state, x, y);
}

/// Used on eink displays when screen updates are paused
void UIRenderer::drawScreensaverOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    LOG_DEBUG("Draw screensaver overlay");

    EINK_ADD_FRAMEFLAG(display, COSMETIC); // Full refresh for screensaver

    // Config
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *pauseText = "Screen Paused";
    const char *idText = owner.short_name;
    const bool useId = haveGlyphs(idText);
    constexpr uint8_t padding = 2;
    constexpr uint8_t dividerGap = 1;

    // Text widths
    const uint16_t idTextWidth = display->getStringWidth(idText, strlen(idText), true);
    const uint16_t pauseTextWidth = display->getStringWidth(pauseText, strlen(pauseText));
    const uint16_t boxWidth = padding + (useId ? idTextWidth + padding : 0) + pauseTextWidth + padding;
    const uint16_t boxHeight = FONT_HEIGHT_SMALL + (padding * 2);

    // Flush with bottom
    const int16_t boxLeft = (display->width() / 2) - (boxWidth / 2);
    const int16_t boxTop = display->height() - boxHeight;
    const int16_t boxBottom = display->height() - 1;
    const int16_t idTextLeft = boxLeft + padding;
    const int16_t idTextTop = boxTop + padding;
    const int16_t pauseTextLeft = boxLeft + (useId ? idTextWidth + (padding * 2) : 0) + padding;
    const int16_t pauseTextTop = boxTop + padding;
    const int16_t dividerX = boxLeft + padding + idTextWidth + padding;
    const int16_t dividerTop = boxTop + dividerGap;
    const int16_t dividerBottom = boxBottom - dividerGap;

    // Draw: box
    display->setColor(EINK_WHITE);
    display->fillRect(boxLeft, boxTop, boxWidth, boxHeight);
    display->setColor(EINK_BLACK);
    display->drawRect(boxLeft, boxTop, boxWidth, boxHeight);

    // Draw: text
    if (useId)
        display->drawString(idTextLeft, idTextTop, idText);
    display->drawString(pauseTextLeft, pauseTextTop, pauseText);
    display->drawString(pauseTextLeft + 1, pauseTextTop, pauseText); // Faux bold

    // Draw: divider
    if (useId)
        display->drawLine(dividerX, dividerTop, dividerX, dividerBottom);
}
#endif

/**
 * Draw the icon with extra info printed around the corners
 */
void UIRenderer::drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // Unified boot screen: centered custom logo + "Bagulan" only
    // Draw the custom icon bitmap from images.h (icon_bits/icon_width/icon_height)
    // Center icon vertically, leaving space for the title at the bottom
    const int iconW = icon_width;
    const int iconH = icon_height;
    const int iconX = x + (SCREEN_WIDTH - iconW) / 2;
    const int iconY = y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - iconH) / 2 + 2; // slight nudge down
    display->drawXbm(iconX, iconY, iconW, iconH, icon_bits);

    // Draw title centered at bottom
    const char *title = "Bagulan";
    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);

    // Force an immediate display update for early boot visibility
    screen->forceDisplay();

    // Restore default alignment for other code paths
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

// ****************************
// * My Position Screen       *
// ****************************
void UIRenderer::drawCompassAndLocationScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int line = 1;

    const char *titleStr = "Location";
    
    graphics::drawCommonHeader(display, x, y, titleStr);

    

    // CHANGED: header en sonda çizilecek
#if HAS_GPS
    bool origBold = config.display.heading_bold;
    config.display.heading_bold = false;

    // ADDED: ortalı yazı için geçici CENTER hizası
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    // Footer/header is drawn at the bottom by drawCommonHeader.
    // Compute its height dynamically and shift content UP so it stays above it.
    const int highlightHeight = FONT_HEIGHT_SMALL - 1;

    const char *displayLine = "";
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    bool usePhoneGPS = (ourNode && nodeDB->hasValidPosition(ourNode) &&
                        config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED);

    if (usePhoneGPS) {
        // Phone-provided GPS is active
        displayLine = "Phone GPS";
        // REMOVED: uydu ikonu
    display->drawString(x + (display->getWidth() / 2), getTextPositions(display)[line++] - highlightHeight, displayLine);
    } else if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        // GPS disabled / not present
        if (config.position.fixed_position) {
            displayLine = "Fixed GPS";
        } else {
            displayLine = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT ? "No GPS" : "GPS off";
        }
        // REMOVED: uydu ikonu
    display->drawString(x + (display->getWidth() / 2), getTextPositions(display)[line++] - highlightHeight, displayLine);
    } else {
        // Onboard GPS
        // CHANGED: drawGps yerine ikon içermeyen kısa metin
        char textString[12];
        if (config.position.fixed_position) {
            snprintf(textString, sizeof(textString), "Fixed");
        } else if (!gpsStatus->getIsConnected()) {
            snprintf(textString, sizeof(textString), "No Lock");
        } else if (!gpsStatus->getHasLock()) {
            snprintf(textString, sizeof(textString), "No Sats");
        } else {
            snprintf(textString, sizeof(textString), "%u sats", gpsStatus->getNumSatellites());
        }
    display->drawString(x + (display->getWidth() / 2), getTextPositions(display)[line++] - highlightHeight, textString);
    }

    // restore LEFT
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    config.display.heading_bold = origBold;

    geoCoord.updateCoords(int32_t(gpsStatus->getLatitude()), int32_t(gpsStatus->getLongitude()),
                          int32_t(gpsStatus->getAltitude()));

    // === Determine Compass Heading ===
    // float heading = 0;
    // bool validHeading = false;
    // if (uiconfig.compass_mode == meshtastic_CompassMode_FREEZE_HEADING) {
    //     validHeading = true;
    // } else {
    //     if (screen->hasHeading()) {
    //         heading = radians(screen->getHeading());
    //         validHeading = true;
    //     } else {
    //         heading = screen->estimatedHeading(geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
    //         validHeading = !isnan(heading);
    //     }
    // }

    // If GPS is off, no need to display these parts
    if (strcmp(displayLine, "GPS off") != 0 && strcmp(displayLine, "No GPS") != 0) {
        // === Second Row: Last GPS Fix ===
        if (gpsStatus->getLastFixMillis() > 0) {
            uint32_t delta = (millis() - gpsStatus->getLastFixMillis()) / 1000; // seconds since last fix
            uint32_t days = delta / 86400;
            uint32_t hours = (delta % 86400) / 3600;
            uint32_t mins = (delta % 3600) / 60;
            uint32_t secs = delta % 60;

            char buf[32];
#if defined(USE_EINK)
            // E-Ink: skip seconds, show only days/hours/mins
            if (days > 0) {
                snprintf(buf, sizeof(buf), "Last: %ud %uh", days, hours);
            } else if (hours > 0) {
                snprintf(buf, sizeof(buf), "Last: %uh %um", hours, mins);
            } else {
                snprintf(buf, sizeof(buf), "Last: %um", mins);
            }
#else
            // Non E-Ink: include seconds where useful
            if (days > 0) {
                snprintf(buf, sizeof(buf), "Last: %ud %uh", days, hours);
            } else if (hours > 0) {
                snprintf(buf, sizeof(buf), "Last: %uh %um", hours, mins);
            } else if (mins > 0) {
                snprintf(buf, sizeof(buf), "Last: %um %us", mins, secs);
            } else {
                snprintf(buf, sizeof(buf), "Last: %us", secs);
            }
#endif

            display->drawString(0, getTextPositions(display)[line++] - highlightHeight, buf);
        } else {
            display->drawString(0, getTextPositions(display)[line++] - highlightHeight, "Last: ?");
        }

        // === Third Row: Line 1 GPS Info ===
    UIRenderer::drawGpsCoordinates(display, x, getTextPositions(display)[line++] - highlightHeight, gpsStatus, "line1");

        if (uiconfig.gps_format != meshtastic_DeviceUIConfig_GpsCoordinateFormat_OLC &&
            uiconfig.gps_format != meshtastic_DeviceUIConfig_GpsCoordinateFormat_MLS) {
            // === Fourth Row: Line 2 GPS Info ===
            UIRenderer::drawGpsCoordinates(display, x, getTextPositions(display)[line++] - highlightHeight, gpsStatus, "line2");
        }
    }
    // === Draw Compass ===
// #if !defined(M5STACK_UNITC6L)
//     // === Draw Compass if heading is valid ===
//     if (validHeading) {
//         // --- Compass Rendering: landscape (wide) screens use original side-aligned logic ---
//         if (SCREEN_WIDTH > SCREEN_HEIGHT) {
//             const int16_t topY = getTextPositions(display)[1];
//             const int16_t bottomY = SCREEN_HEIGHT - (FONT_HEIGHT_SMALL - 1); // nav row height
//             const int16_t usableHeight = bottomY - topY - 5;

//             int16_t compassRadius = usableHeight / 2;
//             if (compassRadius < 8)
//                 compassRadius = 8;
//             const int16_t compassDiam = compassRadius * 2;
//             const int16_t compassX = x + SCREEN_WIDTH - compassRadius - 8;

//             // Center vertically and nudge down slightly to keep "N" clear of header
//             const int16_t compassY = topY + (usableHeight / 2) + ((FONT_HEIGHT_SMALL - 1) / 2) + 2;

//             CompassRenderer::drawNodeHeading(display, compassX, compassY, compassDiam, -heading);
//             display->drawCircle(compassX, compassY, compassRadius);

//             // "N" label
//             float northAngle = 0;
//             if (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING)
//                 northAngle = -heading;
//             float radius = compassRadius;
//             int16_t nX = compassX + (radius - 1) * sin(northAngle);
//             int16_t nY = compassY - (radius - 1) * cos(northAngle);
//             int16_t nLabelWidth = display->getStringWidth("N") + 2;
//             int16_t nLabelHeightBox = FONT_HEIGHT_SMALL + 1;

//             display->setColor(BLACK);
//             display->fillRect(nX - nLabelWidth / 2, nY - nLabelHeightBox / 2, nLabelWidth, nLabelHeightBox);
//             display->setColor(WHITE);
//             display->setFont(FONT_SMALL);
//             display->setTextAlignment(TEXT_ALIGN_CENTER);
//             display->drawString(nX, nY - FONT_HEIGHT_SMALL / 2, "N");
//         } else {
//             // Portrait or square: put compass at the bottom and centered, scaled to fit available space
//             // For E-Ink screens, account for navigation bar at the bottom!
//             int yBelowContent = getTextPositions(display)[5] + FONT_HEIGHT_SMALL + 2;
//             const int margin = 4;
//             int availableHeight =
// #if defined(USE_EINK)
//                 SCREEN_HEIGHT - yBelowContent - 24; // Leave extra space for nav bar on E-Ink
// #else
//                 SCREEN_HEIGHT - yBelowContent - margin;
// #endif

//             if (availableHeight < FONT_HEIGHT_SMALL * 2)
//                 return;

//             int compassRadius = availableHeight / 2;
//             if (compassRadius < 8)
//                 compassRadius = 8;
//             if (compassRadius * 2 > SCREEN_WIDTH - 16)
//                 compassRadius = (SCREEN_WIDTH - 16) / 2;

//             int compassX = x + SCREEN_WIDTH / 2;
//             int compassY = yBelowContent + availableHeight / 2;

//             CompassRenderer::drawNodeHeading(display, compassX, compassY, compassRadius * 2, -heading);
//             display->drawCircle(compassX, compassY, compassRadius);

//             // "N" label
//             float northAngle = 0;
//             if (uiconfig.compass_mode != meshtastic_CompassMode_FIXED_RING)
//                 northAngle = -heading;
//             float radius = compassRadius;
//             int16_t nX = compassX + (radius - 1) * sin(northAngle);
//             int16_t nY = compassY - (radius - 1) * cos(northAngle);
//             int16_t nLabelWidth = display->getStringWidth("N") + 2;
//             int16_t nLabelHeightBox = FONT_HEIGHT_SMALL + 1;

//             display->setColor(BLACK);
//             display->fillRect(nX - nLabelWidth / 2, nY - nLabelHeightBox / 2, nLabelWidth, nLabelHeightBox);
//             display->setColor(WHITE);
//             display->setFont(FONT_SMALL);
//             display->setTextAlignment(TEXT_ALIGN_CENTER);
//             display->drawString(nX, nY - FONT_HEIGHT_SMALL / 2, "N");
//         }
//     }
// #endif
#endif // HAS_GPS
}

#ifdef USERPREFS_OEM_TEXT

void UIRenderer::drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw OEM image if provided, else fall back to default icon from images.h
#ifdef USERPREFS_OEM_IMAGE_DATA
    {
        static const uint8_t xbm[] = USERPREFS_OEM_IMAGE_DATA;
        if (isHighResolution) {
            display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                             y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2,
                             USERPREFS_OEM_IMAGE_WIDTH, USERPREFS_OEM_IMAGE_HEIGHT, xbm);
        } else {
            display->drawXbm(x + (SCREEN_WIDTH - USERPREFS_OEM_IMAGE_WIDTH) / 2,
                             y + (SCREEN_HEIGHT - USERPREFS_OEM_IMAGE_HEIGHT) / 2 + 2, USERPREFS_OEM_IMAGE_WIDTH,
                             USERPREFS_OEM_IMAGE_HEIGHT, xbm);
        }
    }
#else
    {
        // Fallback: use built-in icon_bits/icon_width/icon_height
        const int iconW = icon_width;
        const int iconH = icon_height;
        const int iconX = x + (SCREEN_WIDTH - iconW) / 2;
        const int iconY = isHighResolution ?
                              y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - iconH) / 2 + 2 :
                              y + (SCREEN_HEIGHT - iconH) / 2 + 2;
        display->drawXbm(iconX, iconY, iconW, iconH, icon_bits);
    }
#endif

    // Font size (optional macro); default to MEDIUM if not specified
#ifdef USERPREFS_OEM_FONT_SIZE
    switch (USERPREFS_OEM_FONT_SIZE) {
    case 0:
        display->setFont(FONT_SMALL);
        break;
    case 2:
        display->setFont(FONT_LARGE);
        break;
    default:
        display->setFont(FONT_MEDIUM);
        break;
    }
#else
    display->setFont(FONT_MEDIUM);
#endif

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const char *title = USERPREFS_OEM_TEXT;
    if (isHighResolution) {
        display->drawString(x + getStringCenteredX(title), y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, title);
    }
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // BEGIN TAG: VERSION_BADGE (OEM boot/system screen version + user info)
    // Build branded version string: replace any occurrence of Meshtastic/meshtastic with "Bagulan"
    char ver[48];
    snprintf(ver, sizeof(ver), "%s", xstr(APP_VERSION_SHORT));
    const char *brandUp = "Bagulan";
    const char *brandLo = "Bagulan";
    char finalVer[64];
    const char *found = strstr(ver, brandUp);
    size_t removeLen = found ? strlen(brandUp) : 0;
    if (!found) {
        found = strstr(ver, brandLo);
        removeLen = found ? strlen(brandLo) : 0;
    }
    if (found) {
        const char *rest = found + removeLen;
        while (*rest == ' ' || *rest == '-' || *rest == ':')
            rest++;
        snprintf(finalVer, sizeof(finalVer), "Bagulan %s", rest);
    } else {
        snprintf(finalVer, sizeof(finalVer), "Bagulan %s", ver);
    }

    // Prepare user info: prefer long_name (if available), otherwise short_name
    const char *shortName = haveGlyphs(owner.short_name) ? owner.short_name : "";
    char longNameBuf[64] = {0};
    const char *longName = nullptr;
    if (auto *meNode = nodeDB->getMeshNode(nodeDB->getNodeNum()); meNode && meNode->has_user && meNode->user.long_name[0]) {
        std::string sanitized = sanitizeString(meNode->user.long_name);
        strncpy(longNameBuf, sanitized.c_str(), sizeof(longNameBuf) - 1);
        if (haveGlyphs(longNameBuf))
            longName = longNameBuf;
    }

    // Compose multi-line badge: version + short (ID) + long name (if present)
    char buf[128];
    if (longName && longName[0])
        snprintf(buf, sizeof(buf), "%s\n%s\n%s", finalVer, shortName, longName);
    else
        snprintf(buf, sizeof(buf), "%s\n%s", finalVer, shortName);

    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + SCREEN_WIDTH, y + 0, buf);
    // END TAG: VERSION_BADGE
    screen->forceDisplay();

    display->setTextAlignment(TEXT_ALIGN_LEFT); // Restore left align, just to be kind to any other unsuspecting code
}

void UIRenderer::drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

#endif

// Navigation bar overlay implementation
static int8_t lastFrameIndex = -1;
static uint32_t lastFrameChangeTime = 0;
constexpr uint32_t ICON_DISPLAY_DURATION_MS = 2000;

void UIRenderer::drawNavigationBar(OLEDDisplay *display, OLEDDisplayUiState *state)
{
    // CHANGED: Navigation icons disabled (no icons drawn)
    (void)display;
    (void)state;
    return;
}

void UIRenderer::drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, const char *message)
{
    uint16_t x_offset = display->width() / 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x_offset + x, 26 + y, message);
}

std::string UIRenderer::drawTimeDelta(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds)
{
    std::string uptime;

    if (days > (HOURS_IN_MONTH * 6))
        uptime = "?";
    else if (days >= 2)
        uptime = std::to_string(days) + "d";
    else if (hours >= 2)
        uptime = std::to_string(hours) + "h";
    else if (minutes >= 1)
        uptime = std::to_string(minutes) + "m";
    else
        uptime = std::to_string(seconds) + "s";
    return uptime;
}

} // namespace graphics

#endif // HAS_SCREEN