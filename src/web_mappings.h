#ifndef WEB_MAPPINGS_H
#define WEB_MAPPINGS_H

#include <map>
#include <Arduino.h>
#include "web_content.h"

std::map<String, const char*> stringMap = {
    {"dn.htm", dn_htm},
    {"update.html", update_html},
    {"troubleshoot.html", troubleshoot_html},
    {"debug.html", debug_html},
    {"css/style_additional.css", css_style_additional_css},
    {"css/log.css", css_log_css},
    {"css/adminstyle.css", css_adminstyle_css},
    {"images/alert0.svg", images_alert0_svg},
    {"images/alert.svg", images_alert_svg},
    {"images/schedulePlay.svg", images_schedulePlay_svg},
    {"images/wifi_g.svg", images_wifi_g_svg},
    {"images/wifi_e.svg", images_wifi_e_svg},
    {"images/wifi_w.svg", images_wifi_w_svg},
    {"images/ethernet_icon.svg", images_ethernet_icon_svg},
    {"images/cloud_off.svg", images_cloud_off_svg},
    {"images/EVIQ_white.svg", images_EVIQ_white_svg},
    {"images/cloud_ok.svg", images_cloud_ok_svg},
    {"scripts/debugparsing.js", scripts_debugparsing_js},
    {"scripts/help.js", scripts_help_js},
};

struct BinaryFile {
    const unsigned char* data;
    unsigned int length;
};

std::map<String, BinaryFile> binaryMap = {
    {"images/wifi.png", {images_wifi_png, images_wifi_png_len}},
    {"images/ap.png", {images_ap_png, images_ap_png_len}},
    {"images/favicon.ico", {images_favicon_ico, images_favicon_ico_len}},
    {"images/settings.png", {images_settings_png, images_settings_png_len}},
    {"images/power.png", {images_power_png, images_power_png_len}},
    {"images/home.png", {images_home_png, images_home_png_len}},
    {"images/favicon.webp", {images_favicon_webp, images_favicon_webp_len}},
    {"images/logo.png", {images_logo_png, images_logo_png_len}},
    {"images/schedule.png", {images_schedule_png, images_schedule_png_len}},
    {"images/trash.png", {images_trash_png, images_trash_png_len}},
    {"images/reload.png", {images_reload_png, images_reload_png_len}},
    {"favicon.webp", {images_favicon_webp, images_favicon_webp_len}},
    {"favicon.ico", {images_favicon_ico, images_favicon_ico_len}},
};

// Gzipped files - served with Content-Encoding: gzip
std::map<String, BinaryFile> gzipMap = {
    {"index.html", {index_html_gz, index_html_gz_len}},
    {"mqttsetup.html", {mqttsetup_html_gz, mqttsetup_html_gz_len}},
    {"mqttsettings.html", {mqttsettings_html_gz, mqttsettings_html_gz_len}},
    {"login.html", {login_html_gz, login_html_gz_len}},
    {"energy.html", {energy_html_gz, energy_html_gz_len}},
    {"firstuse.html", {firstuse_html_gz, firstuse_html_gz_len}},
    {"certupload.html", {certupload_html_gz, certupload_html_gz_len}},
    {"log.html", {log_html_gz, log_html_gz_len}},
    {"techsetup.html", {techsetup_html_gz, techsetup_html_gz_len}},
    {"help_en.json", {help_en_json_gz, help_en_json_gz_len}},
    {"css/style.css", {css_style_css_gz, css_style_css_gz_len}},
    {"scripts/script_sch.js", {scripts_script_sch_js_gz, scripts_script_sch_js_gz_len}},
    {"scripts/script_additional.js", {scripts_script_additional_js_gz, scripts_script_additional_js_gz_len}},
    {"scripts/script_power.js", {scripts_script_power_js_gz, scripts_script_power_js_gz_len}},
    {"scripts/common_listing.js", {scripts_common_listing_js_gz, scripts_common_listing_js_gz_len}},
    {"scripts/logmessages.js", {scripts_logmessages_js_gz, scripts_logmessages_js_gz_len}},
    {"scripts/script.js", {scripts_script_js_gz, scripts_script_js_gz_len}},
    {"scripts/energy_logparsing.js", {scripts_energy_logparsing_js_gz, scripts_energy_logparsing_js_gz_len}},
    {"scripts/adminscript.js", {scripts_adminscript_js_gz, scripts_adminscript_js_gz_len}},
    {"scripts/validation.js", {scripts_validation_js_gz, scripts_validation_js_gz_len}},
    {"scripts/common.js", {scripts_common_js_gz, scripts_common_js_gz_len}},
    {"scripts/logparsing.js", {scripts_logparsing_js_gz, scripts_logparsing_js_gz_len}},
};

#endif // WEB_MAPPINGS_H
