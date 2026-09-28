#pragma once

namespace weatherbot
{
constexpr const char *LATITUDE = "42.9849";
constexpr const char *LONGITUDE = "-81.2453";
constexpr const char *LOCATION = "London";
constexpr const char *TIMEZONE = "America%2FToronto";
constexpr const char *POSIX_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr int MORNING_HOUR = 7;
constexpr int EVENING_HOUR = 19;
constexpr unsigned REPORT_GRACE_SECONDS = 30 * 60;
constexpr unsigned ALERT_INTERVAL_MS = 15 * 60 * 1000;
constexpr unsigned MIN_SEND_INTERVAL_MS = 60 * 1000;
constexpr unsigned MAX_MESSAGE_BYTES = 220;
constexpr unsigned MAX_ALERTS = 8;
constexpr const char *ALERT_URLS[] = {
    "https://api.weather.gc.ca/collections/weather-alerts/"
    "items?f=json&limit=8&skipGeometry=true&properties=alert_name_en,alert_text_en,alert_type,feature_name_en,feature_id,status_"
    "en,expiration_datetime,publication_datetime,risk_colour_en,impact_en,confidence_en&feature_name_en=London%20-%20Parkhill%20-"
    "%20Eastern%20Middlesex%20County",
    "https://api.weather.gc.ca/collections/weather-alerts/"
    "items?f=json&limit=8&skipGeometry=true&properties=alert_name_en,alert_text_en,alert_type,feature_name_en,feature_id,status_"
    "en,expiration_datetime,publication_datetime,risk_colour_en,impact_en,confidence_en&feature_name_en=Strathroy%20-%20Komoka%"
    "20-%20Western%20Middlesex%20County",
};
constexpr const char *ALERT_AREAS[] = {"London/E Middlesex", "W Middlesex"};
constexpr const char *FEED_TITLES[] = {"London - Parkhill - Eastern Middlesex County",
                                       "Strathroy - Komoka - Western Middlesex County"};
} // namespace weatherbot
