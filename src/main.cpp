#include <Arduino.h>
#include <cstring>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

// Freenove 4.0" Display configuration - ST7796 with SPI
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7796   _panel_instance;
  lgfx::Bus_SPI        _bus_instance;
  lgfx::Light_PWM      _light_instance;
  lgfx::Touch_XPT2046  _touch_instance;

public:
  LGFX(void)
  {
    {   // SPI bus – display (HSPI, Freenove 4.0" ST7796)
      auto cfg = _bus_instance.config();
      cfg.spi_host    = HSPI_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = 1;

      cfg.pin_sclk    = 14;
      cfg.pin_mosi    = 13;
      cfg.pin_miso    = 12;
      cfg.pin_dc      = 2;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {   // ST7796 panel (320x480)
      auto cfg = _panel_instance.config();

      cfg.pin_cs           = 15;
      cfg.pin_rst          = -1;
      cfg.pin_busy         = -1;

      cfg.memory_width     = 320;
      cfg.memory_height    = 480;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;

      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;

      _panel_instance.config(cfg);
    }

    {   // Backlight - LCD_BL on GPIO27, active high via BSS138 driver
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 27;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {   // Touch - XPT2046 shares the LCD SPI bus (SCK14/MISO12/MOSI13), CS33, IRQ36
      auto cfg = _touch_instance.config();

      cfg.x_min           = 200;
      cfg.x_max           = 3600;
      cfg.y_min           = 200;
      cfg.y_max           = 3600;

      cfg.pin_int         = 36;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;

      cfg.spi_host        = HSPI_HOST;
      cfg.freq            = 2500000;

      cfg.pin_sclk        = 14;
      cfg.pin_mosi        = 13;
      cfg.pin_miso        = 12;
      cfg.pin_cs          = 33;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX display;

const uint16_t COLOR_BG = 0x080F;
const uint16_t COLOR_HEADER = 0x1D4E;
const uint16_t COLOR_CARD = 0x1D1D;
const uint16_t COLOR_MUTED = 0x6B6D;
const uint16_t COLOR_RED = 0xB104;
const uint16_t COLOR_DARK_RED = 0x6800;
const uint16_t COLOR_AMBER = 0xF9A0;
const uint16_t COLOR_GREEN = 0x3A9A;
const uint16_t COLOR_BLUE = 0x2E8B;
const uint16_t COLOR_DARK_GRAY = 0x4208;
const uint16_t COLOR_DARK_BLUE = 0x0010;
const uint16_t COLOR_DARK_GREEN = 0x0320;
const uint16_t COLOR_WHITE = 0xFFFF;
const uint16_t COLOR_CYAN = 0x07FF;
const char* NTP_SERVER = "pool.ntp.org";
const char* NTP_SERVER_BACKUP = "time.nist.gov";
const char* NTP_SERVER_SECOND_BACKUP = "time.google.com";
const char* TIME_ZONE = "EST5EDT,M3.2.0/2,M11.1.0/2"; // U.S. Eastern with DST
const char* INCIDENT_FEED_URL = "http://webcad.lcwc911.us/Pages/Public/LiveIncidentsFeed.aspx";
const char* INCIDENT_FEED_HOST = "webcad.lcwc911.us";
const char* INCIDENT_FEED_PATH = "/Pages/Public/LiveIncidentsFeed.aspx";
const unsigned long INCIDENT_FEED_REFRESH_MS = 60000;
const int MAX_DISPLAY_INCIDENTS = 64;

struct Incident
{
  char title[48];
  char township[64];
  char street[96];
  char unit[128];
  char company[64];
  uint16_t color;
  int minutes;
  time_t eventEpoch;
};

Incident incidents[MAX_DISPLAY_INCIDENTS] = {
  {"Structure Fire", "Lancaster Township", "214 Oak Ave", "Engine 12 / Ladder 3", "Unknown", COLOR_RED, 8},
  {"Medical Alert", "Lancaster Township", "44 Cedar Ln", "Medic 4 / Rescue 1", "Unknown", COLOR_AMBER, 4},
  {"Vehicle Accident", "Lancaster Township", "I-95 Exit 17", "Units 7 / 9", "Unknown", COLOR_BLUE, 12},
  {"Gas Leak", "Lancaster Township", "880 Pine St", "HazMat 2 / Fire 6", "Unknown", COLOR_GREEN, 6}
};

int incidentCount = sizeof(incidents) / sizeof(incidents[0]);
int activeIncidentCount = 0;
int respondingUnitCount = 0;
int accidentIncidentCount = 0;
int fireIncidentCount = 0;
int medicalIncidentCount = 0;
bool feedHealthy = false;
time_t lastSuccessfulFeedEpoch = 0;
const unsigned long INCIDENT_CYCLE_MS = 5000;
unsigned long lastIncidentFeedUpdate = 0;
const int COUNTY_OUTLINE_X = 240;
const int COUNTY_OUTLINE_Y = 0;
const int ROTATION_BUTTON_X = 340;
const int ROTATION_BUTTON_Y = 5;
const int ROTATION_BUTTON_W = 44;
const int ROTATION_BUTTON_H = 30;
bool incidentRotationPaused = false;
bool unitsHelpVisible = false;
bool wifiInfoVisible = false;
bool unitDetailVisible = false;
String unitDetailText = "";
bool countyMapVisible = false;
enum class IncidentFilter
{
  All,
  Fire,
  Medical,
  Vehicle
};
IncidentFilter incidentFilter = IncidentFilter::All;
bool companyDirectoryVisible = false;
int companyDirectoryPage = 0;
const int COMPANIES_PER_PAGE = 8;
struct FireCompany
{
  const char* station;
  const char* name;
};
const FireCompany fireCompanies[] = {
  {"01", "Ephrata Ambulance"}, {"02", "Warwick EMS"},
  {"03", "Martindale Fire Co."}, {"04", "Wellspan EMS"},
  {"05", "Strasburg Fire Co."}, {"06", "Lancaster EMS"},
  {"07", "Mountville Fire Co."}, {"09", "Reinholds Ambulance"},
  {"10", "Marietta Fire Co."}, {"11", "Adamstown Fire Co."},
  {"12", "Akron Fire Co."}, {"13", "Denver Fire Co."},
  {"14", "Durlach & Mount Airy Fire Co."},
  {"15", "Ephrata Fire Co."}, {"16", "Lincoln Fire Co."},
  {"17", "Reamstown Ambulance"}, {"18", "Reinholds Fire Co."},
  {"19", "Schoeneck Fire Co."}, {"20", "Manheim Twp. Fire & Rescue"},
  {"21", "Brickerville Fire Co."}, {"22", "Brunnerville Fire Co."},
  {"23", "East Petersburg Fire Co."}, {"24", "Rothsville Fire Co."},
  {"25", "Lititz Fire Co."},
  {"26", "Manheim Fire Co."}, {"27", "Mastersonville Fire Co."},
  {"28", "Penryn Fire Co."}, {"29", "West Earl Fire Co."},
  {"30", "Weaverland Valley Fire Dept."}, {"31", "Bareville Fire Co."},
  {"32", "Fivepointville Fire Co."}, {"33", "Bowmansville Fire Co."},
  {"34", "Caernarvon Fire Co."}, {"35", "Farmersville Fire Co."},
  {"36", "Fivepointville Ambulance"}, {"37", "New Holland Ambulance"},
  {"39", "Garden Spot Fire Rescue"}, {"40", "Pequea Valley Fire Dept."},
  {"41", "Bird-in-Hand Fire Co."}, {"42", "Gap Fire Co."},
  {"43", "Gordonville Fire Co."}, {"44", "Intercourse Fire Co."},
  {"45", "Kinzer Fire Co."}, {"46", "Christiana Ambulance"},
  {"47", "Paradise Fire Co."}, {"48", "Ronks Fire Co."},
  {"49", "White Horse Fire Co."}, {"50", "Willow Street Fire Co."},
  {"51", "Bart Twp. Fire Co."}, {"52", "Christiana Fire Co."},
  {"53", "Conestoga Fire Co."}, {"54", "Lampeter Fire Co."},
  {"55", "New Danville Fire Co."}, {"56", "Lancaster EMS"},
  {"57", "Quarryville Fire Company"}, {"58", "Rawlinsville Fire Co."},
  {"59", "Refton Fire Co."}, {"60", "West Willow Fire Company"},
  {"61", "Upper Leacock Fire Co."}, {"62", "Witmer Fire Co."},
  {"63", "Lafayette Fire Co."}, {"64", "Lancaster City Fire Dept."},
  {"66", "Lancaster Twp. Fire Dept."}, {"67", "Rohrerstown Fire Co."},
  {"69", "Hempfield Fire Co."}, {"70", "Rheems Fire Co."},
  {"71", "Bainbridge Fire Co."}, {"73", "Franklin & Marshall College QRS"},
  {"74", "Elizabethtown Fire Co."},
  {"75", "Fire Dept. Mount Joy"}, {"76", "West Hempfield Fire & Rescue"},
  {"77", "Penn State Health EMS"},
  {"79", "Maytown-E. Donegal Fire Co."}, {"80", "Columbia Borough Fire Dept."},
  {"82", "Manheim Twp. Ambulance"}, {"85", "Warwick Ambulance"},
  {"86", "MESA"}, {"87", "Lancaster Co. EMA"}, {"88", "Wakefield Ambulance"},
  {"89", "Robert Fulton Fire Co."}, {"90", "Blue Rock Fire & Rescue"},
  {"91", "Lancaster County-Wide Communications"}, {"92", "Northern Lancaster County Forest Fire Co."},
  {"93", "Mt. Joy Twp. Forest Fire Crew"}, {"94", "Middle Creek S.A.R."},
  {"95", "PA Canine S.A.R."}, {"96", "PA Wilderness S.A.R."},
  {"97", "Lancaster Airport"}, {"98", "Arconic Mill Products FD"},
  {"99", "Lanc. Co. Public Safety Training Ctr."}
};

// Approximate schematic placement within the county outline's coordinate space (x:0-29 west-east, y:0-28 north-south).
// Not GPS-accurate; intended only to show roughly where a responding company is based.
struct CompanyLocation
{
  const char* company;
  uint8_t x;
  uint8_t y;
};
const CompanyLocation companyLocations[] = {
  {"Warwick EMS", 15, 8}, {"Wellspan EMS", 17, 6}, {"Lancaster EMS", 14, 15},
  {"New Holland Ambulance", 21, 9}, {"MESA", 5, 6}, {"Reinholds Ambulance", 13, 3},
  {"Ephrata Ambulance", 17, 6}, {"Martindale Fire Co.", 19, 5}, {"Strasburg Fire Co.", 17, 20},
  {"Mountville Fire Co.", 8, 14}, {"Marietta Fire Co.", 5, 10}, {"Adamstown Fire Co.", 22, 3},
  {"Akron Fire Co.", 16, 5}, {"Denver Fire Co.", 20, 3}, {"Durlach & Mount Airy Fire Co.", 18, 5},
  {"Ephrata Fire Co.", 17, 6}, {"Lincoln Fire Co.", 19, 4}, {"Reamstown Fire Co.", 18, 4},
  {"Smokestown Fire Co.", 19, 5}, {"Stevens Fire Co.", 20, 4}, {"Reinholds Fire Co.", 13, 3},
  {"Schoeneck Fire Co.", 15, 5}, {"Manheim Twp. Fire & Rescue", 13, 12}, {"Brickerville Fire Co.", 11, 4},
  {"Brunnerville Fire Co.", 12, 5}, {"East Petersburg Fire Co.", 13, 11}, {"Rothsville Ambulance", 15, 7},
  {"Lititz Fire Co.", 14, 7}, {"Manheim Fire Co.", 10, 6}, {"Mastersonville Fire Co.", 11, 7},
  {"Penryn Fire Co.", 12, 6}, {"West Earl Fire Co.", 17, 7}, {"Weaverland Valley Fire Dept.", 22, 5},
  {"Bareville Fire Co.", 19, 8}, {"Fivepointville Fire Co.", 24, 4}, {"Bowmansville Fire Co.", 25, 5},
  {"Caernarvon Fire Co.", 28, 9}, {"Farmersville Fire Co.", 18, 6}, {"Fivepointville Ambulance", 24, 4},
  {"Garden Spot Fire Rescue", 21, 9}, {"Pequea Valley Fire Dept.", 23, 12}, {"Bird-in-Hand Fire Co.", 21, 11},
  {"Gap Fire Co.", 27, 13}, {"Gordonville Fire Co.", 23, 13}, {"Intercourse Fire Co.", 22, 11},
  {"Kinzer Fire Co.", 24, 13}, {"Christiana Ambulance", 26, 17}, {"Paradise Fire Co.", 24, 12},
  {"Ronks Fire Co.", 21, 13}, {"White Horse Fire Co.", 25, 15}, {"Willow Street Fire Co.", 15, 17},
  {"Bart Twp. Fire Co.", 23, 19}, {"Christiana Fire Co.", 26, 17}, {"Conestoga Fire Co.", 16, 21},
  {"Lampeter Fire Co.", 17, 16}, {"New Danville Fire Co.", 15, 18}, {"Quarryville Fire Company", 17, 23},
  {"Rawlinsville Fire Co.", 15, 24}, {"Refton Fire Co.", 18, 20}, {"West Willow Fire Company", 14, 19},
  {"Upper Leacock Fire Co.", 19, 9}, {"Witmer Fire Co.", 19, 13}, {"Lafayette Fire Co.", 18, 12},
  {"Lancaster City Fire Dept.", 14, 15}, {"Lancaster Twp. Fire Dept.", 13, 14}, {"Rohrerstown Fire Co.", 11, 12},
  {"Hempfield Fire Co.", 8, 11}, {"Rheems Fire Co.", 4, 8}, {"Bainbridge Fire Co.", 3, 6},
  {"Franklin & Marshall College QRS", 14, 15}, {"Elizabethtown Fire Co.", 5, 6}, {"Fire Dept. Mount Joy", 6, 5},
  {"West Hempfield Fire & Rescue", 6, 10}, {"Penn State Health EMS", 10, 8}, {"Maytown-E. Donegal Fire Co.", 4, 4},
  {"Columbia Borough Fire Dept.", 2, 13}, {"Manheim Twp. Ambulance", 13, 12}, {"Warwick Ambulance", 14, 8},
  {"Lancaster Co. EMA", 14, 15}, {"Wakefield Ambulance", 17, 26}, {"Robert Fulton Fire Co.", 17, 27},
  {"Blue Rock Fire & Rescue", 19, 25}, {"Lancaster County-Wide Communications", 14, 15},
  {"Northern Lancaster County Forest Fire Co.", 17, 2}, {"Mt. Joy Twp. Forest Fire Crew", 6, 5},
  {"Middle Creek S.A.R.", 12, 2}, {"PA Canine S.A.R.", 14, 15}, {"PA Wilderness S.A.R.", 14, 15},
  {"Lancaster Airport", 13, 9}, {"Arconic Mill Products FD", 14, 14},
  {"Lanc. Co. Public Safety Training Ctr.", 12, 11}
};

bool findCompanyLocation(const char* company, int& outX, int& outY)
{
  const int locationCount = sizeof(companyLocations) / sizeof(companyLocations[0]);
  for (int index = 0; index < locationCount; ++index)
  {
    if (strcmp(company, companyLocations[index].company) == 0)
    {
      outX = companyLocations[index].x;
      outY = companyLocations[index].y;
      return true;
    }
  }
  return false;
}

const char* TOUCH_PREFERENCES_NAMESPACE = "touch";
const char* TOUCH_CALIBRATION_KEY = "calibration";

void copyIncidentText(char* destination, size_t destinationSize, const String& value)
{
  String cleaned = value;
  cleaned.trim();
  cleaned.toCharArray(destination, destinationSize);
}

void normalizeUnitText(char* units, size_t unitsSize)
{
  String normalized = units;
  normalized.replace("<br>", "\n");
  normalized.replace("<br/>", "\n");
  normalized.replace("<br />", "\n");
  normalized.replace("<BR>", "\n");
  normalized.replace("<BR/>", "\n");
  normalized.replace("<BR />", "\n");
  normalized.replace("&amp;", "&");
  normalized.replace("&nbsp;", " ");
  normalized.replace(";", "\n");
  normalized.replace("  ", " ");

  while (true)
  {
    const int tagStart = normalized.indexOf('<');
    if (tagStart < 0)
    {
      break;
    }
    const int tagEnd = normalized.indexOf('>', tagStart);
    if (tagEnd < 0)
    {
      normalized.remove(tagStart);
      break;
    }
    normalized.remove(tagStart, tagEnd - tagStart + 1);
  }

  for (unsigned int index = 0; index < normalized.length(); ++index)
  {
    const char character = normalized[index];
    if (character != '\n' && (character < 32 || character > 126))
    {
      normalized.setCharAt(index, ' ');
    }
  }
  normalized.trim();
  normalized.toCharArray(units, unitsSize);
}

String rssTagValue(const String& item, const char* tag)
{
  const String openTag = String("<") + tag + ">";
  const String closeTag = String("</") + tag + ">";
  const int start = item.indexOf(openTag);
  if (start < 0)
  {
    return String();
  }

  const int valueStart = start + openTag.length();
  const int end = item.indexOf(closeTag, valueStart);
  if (end < 0)
  {
    return String();
  }
  return item.substring(valueStart, end);
}

String cleanIncidentDescription(String description)
{
  description.replace("<br>", ";");
  description.replace("<br/>", ";");
  description.replace("<br />", ";");
  description.replace("&amp;lt;", "<");
  description.replace("&amp;gt;", ">");
  description.replace("&amp;", "&");
  description.replace("&lt;", "<");
  description.replace("&gt;", ">");
  description.replace("&apos;", "'");
  description.replace("&quot;", "\"");
  description.replace("<br>", ";");
  description.replace("<br/>", ";");
  description.replace("<br />", ";");

  while (true)
  {
    const int tagStart = description.indexOf('<');
    if (tagStart < 0)
    {
      break;
    }
    const int tagEnd = description.indexOf('>', tagStart);
    if (tagEnd < 0)
    {
      description.remove(tagStart);
      break;
    }
    description.remove(tagStart, tagEnd - tagStart + 1);
  }
  description.trim();
  return description;
}

uint16_t incidentColor(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  if (upperTitle.indexOf("FIRE") >= 0 || upperTitle.indexOf("ALARM") >= 0)
  {
    return COLOR_RED;
  }
  if (upperTitle.indexOf("MEDICAL") >= 0 || upperTitle.indexOf("EMS") >= 0)
  {
    return COLOR_AMBER;
  }
  if (upperTitle.indexOf("ACCIDENT") >= 0 || upperTitle.indexOf("CRASH") >= 0)
  {
    return COLOR_BLUE;
  }
  return COLOR_GREEN;
}

bool isVehicleAccident(const String& title)
{
  String normalized = title;
  normalized.toUpperCase();
  normalized.replace("-", " ");
  normalized.replace("_", " ");
  normalized.replace("/", " ");
  while (normalized.indexOf("  ") >= 0)
  {
    normalized.replace("  ", " ");
  }
  normalized.trim();

  return normalized.indexOf("VEHICLE ACCIDENT") >= 0 ||
         normalized.indexOf("VEHICLE CRASH") >= 0 ||
         normalized.indexOf("MOTOR VEHICLE ACCIDENT") >= 0 ||
         normalized.indexOf("MVA") >= 0 ||
         normalized.indexOf("TRAFFIC ACCIDENT") >= 0 ||
         normalized.indexOf("ACCIDENT") >= 0 ||
         normalized.indexOf("CRASH") >= 0;
}

bool isClassOneIncident(const char* title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("CLASS 1") >= 0;
}

bool hasFireApparatusUnit(const char* units)
{
  String upperUnits = units;
  upperUnits.toUpperCase();
  String remaining = upperUnits;
  while (remaining.length() > 0)
  {
    const int separator = remaining.indexOf('\n');
    String token = separator >= 0 ? remaining.substring(0, separator) : remaining;
    token.trim();
    if (token.startsWith("ENGINE") || token.startsWith("LADDER") ||
        token.startsWith("SQUAD") || token.startsWith("RESCUE") ||
        token.startsWith("TRUCK"))
    {
      return true;
    }
    remaining = separator >= 0 ? remaining.substring(separator + 1) : String();
  }
  return false;
}

bool isFireIncident(const String& title, const char* units)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  if (upperTitle.indexOf("FIRE") >= 0 || upperTitle.indexOf("ALARM") >= 0)
  {
    return true;
  }
  return hasFireApparatusUnit(units);
}

bool isMedicalIncident(const String& title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("MEDICAL") >= 0 || upperTitle.indexOf("EMS") >= 0 ||
         upperTitle.indexOf("TRANSFER") >= 0;
}

bool isMedicalEmergency(const char* title)
{
  String upperTitle = title;
  upperTitle.toUpperCase();
  return upperTitle.indexOf("MEDICAL EMERGENCY") >= 0;
}

int countAssignedUnits(const char* units)
{
  if (units[0] == '\0')
  {
    return 0;
  }

  int count = 1;
  for (const char* character = units; *character != '\0'; ++character)
  {
    if (*character == ';')
    {
      ++count;
    }
  }
  return count;
}

struct StationCompany
{
  const char* code;
  const char* company;
};

const StationCompany stationCompanies[] = {
  {" 02-", "Warwick EMS"}, {" 2-", "Warwick EMS"},
  {" 04-", "Wellspan EMS"}, {" 4-", "Wellspan EMS"},
  {" 06-", "Lancaster EMS"}, {" 6-", "Lancaster EMS"},
  {" 37-", "New Holland Ambulance"},
  {" 56-", "Lancaster EMS"}, {" 77-", "Penn State Health EMS"},
  {" 86-", "MESA"}, {" 09-", "Reinholds Ambulance"},
  {" 1", "Ephrata Ambulance"},
  {" 2-1", "Warwick EMS"}, {" 2-2", "Warwick EMS"},
  {" 2-3", "Warwick EMS"}, {" 2-4", "Warwick EMS"},
  {" 3", "Martindale Fire Co."},
  {" 04-1", "Wellspan EMS"}, {" 04-3", "Wellspan EMS"},
  {" 04-5", "Wellspan EMS"}, {" 04-6", "Wellspan EMS"},
  {" 5", "Strasburg Fire Co."},
  {" 06-1", "Lancaster EMS"}, {" 06-2", "Lancaster EMS"},
  {" 06-3", "Lancaster EMS"}, {" 06-4", "Lancaster EMS"},
  {" 06-5", "Lancaster EMS"}, {" 06-6", "Lancaster EMS"},
  {" 06-7", "Lancaster EMS"}, {" 06-8", "Lancaster EMS"},
  {" 06-9", "Lancaster EMS"}, {" 7", "Mountville Fire Co."},
  {" 9", "Reinholds Ambulance"}, {" 10", "Marietta Fire Co."},
  {" 11", "Adamstown Fire Co."}, {" 12", "Akron Fire Co."},
  {" 13", "Denver Fire Co."}, {" 14", "Durlach & Mount Airy Fire Co."},
  {" 15", "Ephrata Fire Co."}, {" 16", "Lincoln Fire Co."},
  {" 17-1", "Reamstown Fire Co."}, {" 17-2", "Smokestown Fire Co."},
  {" 17-3", "Stevens Fire Co."}, {" 18", "Reinholds Fire Co."},
  {" 19", "Schoeneck Fire Co."}, {" 20", "Manheim Twp. Fire & Rescue"},
  {" 21", "Brickerville Fire Co."}, {" 22", "Brunnerville Fire Co."},
  {" 23", "East Petersburg Fire Co."}, {" 24", "Rothsville Ambulance"},
  {" 25", "Lititz Fire Co."}, {" 26", "Manheim Fire Co."},
  {" 27", "Mastersonville Fire Co."}, {" 28", "Penryn Fire Co."},
  {" 29", "West Earl Fire Co."}, {" 30", "Weaverland Valley Fire Dept."},
  {" 31", "Bareville Fire Co."}, {" 32", "Fivepointville Fire Co."},
  {" 33", "Bowmansville Fire Co."}, {" 34", "Caernarvon Fire Co."},
  {" 35", "Farmersville Fire Co."}, {" 36", "Fivepointville Ambulance"},
  {" 37-1", "New Holland Ambulance"}, {" 37-8", "New Holland Ambulance"},
  {" 39", "Garden Spot Fire Rescue"}, {" 40", "Pequea Valley Fire Dept."},
  {" 41", "Bird-in-Hand Fire Co."}, {" 42", "Gap Fire Co."},
  {" 43", "Gordonville Fire Co."}, {" 44", "Intercourse Fire Co."},
  {" 45", "Kinzer Fire Co."}, {" 46", "Christiana Ambulance"},
  {" 47", "Paradise Fire Co."}, {" 48", "Ronks Fire Co."},
  {" 49", "White Horse Fire Co."}, {" 50", "Willow Street Fire Co."},
  {" 51", "Bart Twp. Fire Co."}, {" 52", "Christiana Fire Co."},
  {" 53", "Conestoga Fire Co."}, {" 54", "Lampeter Fire Co."},
  {" 55", "New Danville Fire Co."},
  {" 56-1", "Lancaster EMS"}, {" 56-2", "Lancaster EMS"},
  {" 56-3", "Lancaster EMS"}, {" 56-4", "Lancaster EMS"},
  {" 56-5", "Lancaster EMS"}, {" 56-6", "Lancaster EMS"},
  {" 56-7", "Lancaster EMS"}, {" 56-8", "Lancaster EMS"},
  {" 56-9", "Lancaster EMS"}, {" 57", "Quarryville Fire Company"},
  {" 58", "Rawlinsville Fire Co."}, {" 59", "Refton Fire Co."},
  {" 60", "West Willow Fire Company"}, {" 61", "Upper Leacock Fire Co."},
  {" 62", "Witmer Fire Co."}, {" 63", "Lafayette Fire Co."},
  {" 64-", "Lancaster City Fire Dept."}, {" 64", "Lancaster City Fire Dept."},
  {" 66", "Lancaster Twp. Fire Dept."},
  {" 67", "Rohrerstown Fire Co."}, {" 69", "Hempfield Fire Co."},
  {" 70", "Rheems Fire Co."}, {" 71", "Bainbridge Fire Co."},
  {" 73", "Franklin & Marshall College QRS"},
  {" 74", "Elizabethtown Fire Co."}, {" 75", "Fire Dept. Mount Joy"},
  {" 76", "West Hempfield Fire & Rescue"},
  {" 77-1", "Penn State Health EMS"}, {" 77-2", "Penn State Health EMS"},
  {" 77-3", "Penn State Health EMS"}, {" 77-4", "Penn State Health EMS"},
  {" 77-5", "Penn State Health EMS"}, {" 77-6", "Penn State Health EMS"},
  {" 77-7", "Penn State Health EMS"}, {" 77-8", "Penn State Health EMS"},
  {" 77-9", "Penn State Health EMS"}, {" 79", "Maytown-E. Donegal Fire Co."},
  {" 80", "Columbia Borough Fire Dept."}, {" 82", "Manheim Twp. Ambulance"},
  {" 85", "Warwick Ambulance"}, {" 86-1", "MESA"}, {" 86-2", "MESA"},
  {" 86-3", "MESA"}, {" 86-4", "MESA"}, {" 86-5", "MESA"},
  {" 87", "Lancaster Co. EMA"}, {" 88", "Wakefield Ambulance"},
  {" 88-1", "Wakefield Ambulance"}, {" 89", "Robert Fulton Fire Co."},
  {" 90", "Blue Rock Fire & Rescue"},
  {" 91", "Lancaster County-Wide Communications"},
  {" 92", "Northern Lancaster County Forest Fire Co."},
  {" 93", "Mt. Joy Twp. Forest Fire Crew"}, {" 94", "Middle Creek S.A.R."},
  {" 95", "PA Canine S.A.R."}, {" 96", "PA Wilderness S.A.R."},
  {" 97", "Lancaster Airport"}, {" 98", "Arconic Mill Products FD"},
  {" 99", "Lanc. Co. Public Safety Training Ctr."}
};

const char* stationCompanyForUnits(const char* units)
{
  String upperUnits = units;
  upperUnits.toUpperCase();
  const int stationCount = sizeof(stationCompanies) / sizeof(stationCompanies[0]);
  for (int index = 0; index < stationCount; ++index)
  {
    const char* match = upperUnits.c_str();
    while ((match = strstr(match, stationCompanies[index].code)) != nullptr)
    {
      const size_t codeLength = strlen(stationCompanies[index].code);
      const char after = match[codeLength];
        const bool prefixCode = stationCompanies[index].code[codeLength - 1] == '-';
      const bool plainStationCode = strchr(stationCompanies[index].code + 1, '-') == nullptr;
      const bool hasStationSuffix = plainStationCode && after == '-' &&
                                    match[codeLength + 1] >= '0' &&
                                    match[codeLength + 1] <= '9';
        const bool hasPrefixNumber = prefixCode && after >= '0' && after <= '9';
        if (after == '\0' || after == ' ' || after == '\n' || after == ';' ||
          hasStationSuffix || hasPrefixNumber)
      {
        return stationCompanies[index].company;
      }
      ++match;
    }
  }
  return "Unknown";
}

long daysFromCivil(int year, int month, int day)
{
  year -= month <= 2;
  const long era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// Feed timestamps are GMT (e.g. "Fri, 28 Aug 2026 02:02:33 GMT"); parse without relying on TZ-sensitive mktime.
time_t parseRssPubDate(const String& pubDate)
{
  const int firstSpace = pubDate.indexOf(' ');
  if (firstSpace < 0)
  {
    return 0;
  }
  String rest = pubDate.substring(firstSpace + 1);
  rest.trim();

  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  char monthText[4] = {0};
  if (sscanf(rest.c_str(), "%d %3s %d %d:%d:%d", &day, monthText, &year, &hour, &minute, &second) != 6)
  {
    return 0;
  }

  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int month = -1;
  for (int index = 0; index < 12; ++index)
  {
    if (strncmp(monthText, months[index], 3) == 0)
    {
      month = index + 1;
      break;
    }
  }
  if (month < 0)
  {
    return 0;
  }

  const long days = daysFromCivil(year, month, day);
  return static_cast<time_t>(days) * 86400 + hour * 3600 + minute * 60 + second;
}

bool parseLiveIncident(const String& item, Incident& incident)
{
  const String title = rssTagValue(item, "title");
  String description = cleanIncidentDescription(rssTagValue(item, "description"));
  if (title.length() == 0 || description.length() == 0)
  {
    return false;
  }

  const int firstSeparator = description.indexOf(';');
  const int secondSeparator = firstSeparator < 0 ? -1 : description.indexOf(';', firstSeparator + 1);
  String township = description;
  String street = description;
  String units;

  if (firstSeparator >= 0)
  {
    township = description.substring(0, firstSeparator);
    street = description.substring(firstSeparator + 1);
  }
  if (secondSeparator >= 0)
  {
    units = description.substring(secondSeparator + 1);
    street = description.substring(firstSeparator + 1, secondSeparator);
  }

  copyIncidentText(incident.title, sizeof(incident.title), title);
  copyIncidentText(incident.township, sizeof(incident.township), township);
  copyIncidentText(incident.street, sizeof(incident.street), street);
  copyIncidentText(incident.unit, sizeof(incident.unit), units);
  normalizeUnitText(incident.unit, sizeof(incident.unit));
  copyIncidentText(incident.company, sizeof(incident.company), stationCompanyForUnits(incident.unit));
  incident.color = incidentColor(title);
  incident.eventEpoch = parseRssPubDate(rssTagValue(item, "pubDate"));
  incident.minutes = incident.eventEpoch > 0
                          ? static_cast<int>(difftime(time(nullptr), incident.eventEpoch) / 60)
                          : 0;
  return true;
}

bool fetchLiveIncidents()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  WiFiClient client;
  if (!client.connect(INCIDENT_FEED_HOST, 80))
  {
    Serial.println("Incident feed: HTTPS connection setup failed");
    return false;
  }

  client.print("GET ");
  client.print(INCIDENT_FEED_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(INCIDENT_FEED_HOST);
  client.println("Connection: close");
  client.println();

  const unsigned long responseStart = millis();
  while (!client.available() && client.connected() && millis() - responseStart < 10000)
  {
    delay(10);
    yield();
  }

  if (!client.available())
  {
    Serial.println("Incident feed: response timeout");
    client.stop();
    return false;
  }

  const String statusLine = client.readStringUntil('\n');
  if (statusLine.indexOf(" 200 ") < 0)
  {
    Serial.print("Incident feed: ");
    Serial.println(statusLine);
    client.stop();
    return false;
  }

  const unsigned long headerStart = millis();
  while (client.connected() && millis() - headerStart < 10000)
  {
    const String headerLine = client.readStringUntil('\n');
    if (headerLine == "\r" || headerLine.length() == 0)
    {
      break;
    }
    yield();
  }

  String response;
  response.reserve(12000);
  const unsigned long bodyStart = millis();
  while ((client.connected() || client.available()) && millis() - bodyStart < 10000)
  {
    if (client.available())
    {
      response += client.readString();
    }
    else
    {
      delay(10);
    }
    yield();
  }
  client.stop();

  static Incident liveIncidents[MAX_DISPLAY_INCIDENTS];
  int liveCount = 0;
  int totalCount = 0;
  int respondingCount = 0;
  int accidentCount = 0;
  int fireCount = 0;
  int medicalCount = 0;
  int searchFrom = 0;
  while (true)
  {
    const int itemStart = response.indexOf("<item>", searchFrom);
    if (itemStart < 0)
    {
      break;
    }
    const int itemEnd = response.indexOf("</item>", itemStart);
    if (itemEnd < 0)
    {
      break;
    }

    Incident parsed = {};
    if (parseLiveIncident(response.substring(itemStart, itemEnd), parsed))
    {
      ++totalCount;
      if (parsed.unit[0] != '\0')
      {
        respondingCount += countAssignedUnits(parsed.unit);
      }
      if (isVehicleAccident(parsed.title))
      {
        ++accidentCount;
      }
      if (isFireIncident(parsed.title, parsed.unit))
      {
        ++fireCount;
      }
      if (isMedicalIncident(parsed.title))
      {
        ++medicalCount;
      }
      if (liveCount < MAX_DISPLAY_INCIDENTS)
      {
        liveIncidents[liveCount++] = parsed;
      }
    }
    searchFrom = itemEnd + 7;
  }

  if (liveCount == 0)
  {
    Serial.println("Incident feed: no usable incidents");
    return false;
  }

  for (int index = 0; index < liveCount; ++index)
  {
    incidents[index] = liveIncidents[index];
  }
  incidentCount = liveCount;
  activeIncidentCount = totalCount;
  respondingUnitCount = respondingCount;
  accidentIncidentCount = accidentCount;
  fireIncidentCount = fireCount;
  medicalIncidentCount = medicalCount;
  Serial.printf("Incident feed: loaded %d live incidents\n", incidentCount);
  Serial.printf("Incident totals: active=%d units=%d accidents=%d\n",
                activeIncidentCount, respondingUnitCount, accidentIncidentCount);
  Serial.printf("Incident types: fire=%d medical=%d\n", fireIncidentCount, medicalIncidentCount);
  lastSuccessfulFeedEpoch = time(nullptr);
  return true;
}

const int countyOutlinePoints[][2] = {
  {0, 9}, {2, 7}, {6, 5}, {10, 4}, {14, 3},
  {20, 0}, {24, 4}, {29, 10}, {26, 12}, {26, 17},
  {24, 20}, {23, 24}, {20, 27}, {16, 28}, {13, 25},
  {10, 22}, {9, 18}, {6, 16}, {5, 13}, {2, 12}, {0, 9}
};
const int COUNTY_OUTLINE_POINT_COUNT = sizeof(countyOutlinePoints) / sizeof(countyOutlinePoints[0]);

void drawCountyOutlineShape(int originX, int originY, int scale, uint16_t lineColor)
{
  for (int y = 0; y <= 28; ++y)
  {
    int left = 100;
    int right = -1;

    for (int point = 0; point < COUNTY_OUTLINE_POINT_COUNT - 1; ++point)
    {
      const int x1 = countyOutlinePoints[point][0];
      const int y1 = countyOutlinePoints[point][1];
      const int x2 = countyOutlinePoints[point + 1][0];
      const int y2 = countyOutlinePoints[point + 1][1];

      if ((y >= y1 && y < y2) || (y >= y2 && y < y1))
      {
        const int x = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
        left = min(left, x);
        right = max(right, x);
      }
    }

    if (right >= left)
    {
      for (int row = 0; row < scale; ++row)
      {
        display.drawLine(originX + left * scale, originY + y * scale + row,
                         originX + right * scale, originY + y * scale + row, lineColor);
      }
    }
  }
}

void drawLancasterCountyOutline()
{
  drawCountyOutlineShape(COUNTY_OUTLINE_X, COUNTY_OUTLINE_Y, 1, COLOR_DARK_BLUE);
}

// Chaikin corner-cutting rounds the coarse header polygon into a smoother, less blocky boundary for the map popup.
void drawSmoothCountyOutline(int originX, int originY, float scale, uint16_t lineColor)
{
  const int MAX_SMOOTHED_POINTS = 220;
  static float smoothX[MAX_SMOOTHED_POINTS];
  static float smoothY[MAX_SMOOTHED_POINTS];
  int pointCount = COUNTY_OUTLINE_POINT_COUNT;
  for (int index = 0; index < pointCount; ++index)
  {
    smoothX[index] = countyOutlinePoints[index][0];
    smoothY[index] = countyOutlinePoints[index][1];
  }

  const int smoothingIterations = 3;
  static float nextX[MAX_SMOOTHED_POINTS];
  static float nextY[MAX_SMOOTHED_POINTS];
  for (int iteration = 0; iteration < smoothingIterations; ++iteration)
  {
    int nextCount = 0;
    for (int point = 0; point < pointCount - 1 && nextCount + 2 < MAX_SMOOTHED_POINTS; ++point)
    {
      const float x0 = smoothX[point];
      const float y0 = smoothY[point];
      const float x1 = smoothX[point + 1];
      const float y1 = smoothY[point + 1];
      nextX[nextCount] = x0 + 0.25f * (x1 - x0);
      nextY[nextCount] = y0 + 0.25f * (y1 - y0);
      ++nextCount;
      nextX[nextCount] = x0 + 0.75f * (x1 - x0);
      nextY[nextCount] = y0 + 0.75f * (y1 - y0);
      ++nextCount;
    }
    nextX[nextCount] = nextX[0];
    nextY[nextCount] = nextY[0];
    ++nextCount;
    memcpy(smoothX, nextX, nextCount * sizeof(float));
    memcpy(smoothY, nextY, nextCount * sizeof(float));
    pointCount = nextCount;
  }

  // Rasterize at full output resolution (not the coarse source grid) so the boundary reads as a smooth curve, not stair-stepped blocks.
  const int maxOutputY = static_cast<int>(28 * scale);
  for (int y = 0; y <= maxOutputY; ++y)
  {
    const float sampleY = y / scale;
    float left = 1e6f;
    float right = -1e6f;

    for (int point = 0; point < pointCount - 1; ++point)
    {
      const float y1 = smoothY[point];
      const float y2 = smoothY[point + 1];
      if ((sampleY >= y1 && sampleY < y2) || (sampleY >= y2 && sampleY < y1))
      {
        const float x1 = smoothX[point];
        const float x2 = smoothX[point + 1];
        const float x = x1 + (sampleY - y1) * (x2 - x1) / (y2 - y1);
        left = min(left, x);
        right = max(right, x);
      }
    }

    if (right >= left)
    {
      display.drawLine(originX + static_cast<int>(left * scale), originY + y,
                       originX + static_cast<int>(right * scale), originY + y, lineColor);
    }
  }
}

void drawWifiSignalBars()
{
  const int barX = COUNTY_OUTLINE_X + 58;
  const int barBottom = COUNTY_OUTLINE_Y + 30;
  const int barWidth = 4;
  const int barGap = 2;
  int bars = 0;

  if (WiFi.status() == WL_CONNECTED)
  {
    const int rssi = WiFi.RSSI();
    bars = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -80 ? 2 : 1;
  }

  for (int index = 0; index < 4; ++index)
  {
    const int height = 4 + index * 4;
    const uint16_t color = index < bars ? COLOR_CYAN : COLOR_DARK_GRAY;
    display.fillRect(barX + index * (barWidth + barGap),
                     barBottom - height, barWidth, height, color);
  }
}

void drawRotationButton()
{
  display.fillRoundRect(ROTATION_BUTTON_X, ROTATION_BUTTON_Y,
                        ROTATION_BUTTON_W, ROTATION_BUTTON_H, 4, COLOR_HEADER);
  display.drawRoundRect(ROTATION_BUTTON_X, ROTATION_BUTTON_Y,
                        ROTATION_BUTTON_W, ROTATION_BUTTON_H, 4, COLOR_CYAN);

  if (incidentRotationPaused)
  {
    const int left = ROTATION_BUTTON_X + 15;
    display.fillTriangle(left, ROTATION_BUTTON_Y + 9,
               left, ROTATION_BUTTON_Y + 21,
               left + 10, ROTATION_BUTTON_Y + 15, COLOR_CYAN);
  }
  else
  {
    display.fillRect(ROTATION_BUTTON_X + 15, ROTATION_BUTTON_Y + 9, 4, 13, COLOR_CYAN);
    display.fillRect(ROTATION_BUTTON_X + 24, ROTATION_BUTTON_Y + 9, 4, 13, COLOR_CYAN);
  }
}

uint16_t touchX = 0;
uint16_t touchY = 0;
unsigned long lastIncidentCycle = 0;
int incidentOffset = 0;
bool clockSynced = false;
const int WIFI_SETUP_VERSION = 2;

void markWifiConfigured()
{
  Preferences preferences;
  preferences.begin("wifi", false);
  preferences.putBool("configured", true);
  preferences.end();
  Serial.println("Wi-Fi credentials saved");
}

void drawWifiSetupScreen()
{
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, display.width(), 40, COLOR_HEADER);
  display.setTextColor(COLOR_WHITE, COLOR_HEADER);
  display.setTextSize(1.5f, 1.5f);
  display.setCursor(15, 13);
  display.print("Lancaster Fire & EMS LIVE");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(33, 83);
  display.print("Wi-Fi setup");

  display.setTextSize(1);
  display.setCursor(33, 131);
  display.print("Connect your phone's Wi-Fi to:");
  display.setTextColor(COLOR_CYAN, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(33, 157);
  display.print("LANCO-FIRE-EMS SETUP");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(1);
  display.setCursor(33, 205);
  display.print("A setup page will open");
  display.setCursor(33, 227);
  display.print("to choose your Wi-Fi.");
  display.setCursor(33, 248);
  display.print("Use a 2.4 GHz Wi-Fi network.");
}

bool loadTouchCalibration()
{
  uint16_t calibration[8];
  Preferences preferences;
  preferences.begin(TOUCH_PREFERENCES_NAMESPACE, true);
  const size_t calibrationSize = preferences.getBytes(TOUCH_CALIBRATION_KEY,
                                                      calibration, sizeof(calibration));
  preferences.end();
  if (calibrationSize != sizeof(calibration))
  {
    return false;
  }

  display.setTouchCalibrate(calibration);
  return true;
}

bool shouldCalibrateTouch()
{
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(1);
  display.setCursor(28, 84);
  display.print("Hold screen to calibrate");
  display.setCursor(28, 102);
  display.print("or wait for dashboard");

  const unsigned long promptStart = millis();
  while (millis() - promptStart < 3000)
  {
    uint16_t rawX = 0;
    uint16_t rawY = 0;
    if (display.getTouch(&rawX, &rawY))
    {
      const unsigned long holdStart = millis();
      while (millis() - holdStart < 1200)
      {
        if (!display.getTouch(&rawX, &rawY))
        {
          return false;
        }
        delay(25);
      }
      return true;
    }
    delay(25);
  }
  return false;
}

void calibrateTouchAtBoot()
{
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_WHITE, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(28, 70);
  display.print("Touch calibration");
  display.setTextSize(1);
  display.setCursor(28, 105);
  display.print("Touch each target as it appears");

  uint16_t calibration[8] = {};
  display.calibrateTouch(calibration, COLOR_CYAN, COLOR_BG, 10);

  Preferences preferences;
  preferences.begin(TOUCH_PREFERENCES_NAMESPACE, false);
  preferences.putBytes(TOUCH_CALIBRATION_KEY, calibration, sizeof(calibration));
  preferences.end();
  display.setTouchCalibrate(calibration);
  display.fillScreen(COLOR_BG);
}

void connectAndSyncTime()
{
  drawWifiSetupScreen();

  Serial.println("Starting Wi-Fi connection...");
  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(15);
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setSaveConfigCallback(markWifiConfigured);

  const bool connected = wifiManager.autoConnect("LANCO-FIRE-EMS SETUP");

  if (connected && WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Wi-Fi SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("Wi-Fi IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("Wi-Fi mode: ");
    Serial.println(WiFi.getMode());
    configTzTime(TIME_ZONE, NTP_SERVER, NTP_SERVER_BACKUP, NTP_SERVER_SECOND_BACKUP);

    struct tm localTime;
    clockSynced = false;
    for (int attempt = 0; attempt < 3 && !clockSynced; ++attempt)
    {
      clockSynced = getLocalTime(&localTime, 5000);
      if (!clockSynced)
      {
        Serial.printf("NTP sync attempt %d failed\n", attempt + 1);
      }
    }

    if (clockSynced)
    {
      Serial.printf("Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
                    localTime.tm_year + 1900, localTime.tm_mon + 1,
                    localTime.tm_mday, localTime.tm_hour, localTime.tm_min,
                    localTime.tm_sec);
      Serial.printf("Time zone: U.S. Eastern (%s), DST=%s\n", TIME_ZONE,
                    localTime.tm_isdst > 0 ? "active" : "inactive");
    }
    else
    {
      Serial.println("NTP sync failed; displaying uptime until time is available");
    }
  }
  else
  {
    Serial.println("Wi-Fi setup portal active: LANCO-FIRE-EMS SETUP");
    Serial.print("Setup portal IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void drawMetricCard(int x, int y, int w, int h,
                    const char* labelLineOne, const char* labelLineTwo,
                    const char* value, uint16_t accentColor)
{
  display.fillRoundRect(x, y, w, h, 8, COLOR_CARD);
  display.fillRect(x + 8, y + 5, w - 16, 6, accentColor);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setTextSize(1);
  const int firstLabelWidth = display.textWidth(labelLineOne);
  display.setCursor(x + (w - firstLabelWidth) / 2, y + 17);
  display.print(labelLineOne);

  const int secondLabelWidth = display.textWidth(labelLineTwo);
  display.setCursor(x + (w - secondLabelWidth) / 2, y + 29);
  display.print(labelLineTwo);

  display.setTextSize(2);
  const int valueWidth = display.textWidth(value);
  display.setCursor(x + (w - valueWidth) / 2, y + 45);
  display.print(value);
}

void drawActiveCard(int x, int y)
{
  display.fillRoundRect(x, y, 225, 68, 8, COLOR_CARD);
  display.fillRect(x + 8, y + 5, 209, 5, COLOR_RED);
  display.fillRect(x + 112, y + 22, 2, 38, COLOR_MUTED);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setTextSize(1);
  const int activeWidth = display.textWidth("ACTIVE");
  display.setCursor(x + (225 - activeWidth) / 2, y + 12);
  display.print("ACTIVE");

  const char* filterText = incidentFilter == IncidentFilter::Fire
             ? "Fire only"
             : incidentFilter == IncidentFilter::Medical
               ? "Medical only"
               : incidentFilter == IncidentFilter::Vehicle
                 ? "Vehicle only"
                 : "All";
  const int filterWidth = display.textWidth(filterText);
  display.setCursor(x + (225 - filterWidth) / 2, y + 24);
  display.print(filterText);

  const int fireWidth = display.textWidth("Fire");
  display.setCursor(x + (112 - fireWidth) / 2 - 5, y + 33);
  display.print("Fire");
  const int medicalWidth = display.textWidth("Medical");
  display.setCursor(x + 112 + (112 - medicalWidth) / 2 + 10, y + 33);
  display.print("Medical");

  display.setTextSize(2);
  const int fireCountWidth = display.textWidth(String(fireIncidentCount).c_str());
  display.setCursor(x + (112 - fireCountWidth) / 2 - 5, y + 45);
  display.print(fireIncidentCount);
  const int medicalCountWidth = display.textWidth(String(medicalIncidentCount).c_str());
  display.setCursor(x + 112 + (112 - medicalCountWidth) / 2 + 10, y + 45);
  display.print(medicalIncidentCount);
}

void drawClippedText(const char* text, int maxWidth)
{
  String clipped = text;
  const bool truncated = display.textWidth(clipped.c_str()) > maxWidth;
  while (clipped.length() > 3 && display.textWidth((clipped + "...").c_str()) > maxWidth)
  {
    clipped.remove(clipped.length() - 1);
  }
  if (truncated)
  {
    clipped += "...";
  }
  display.print(clipped);
}

void drawWrappedText(const char* text, int x, int y, int maxWidth, int maxLines, int lineHeight)
{
  String remainingText = text;
  remainingText.trim();

  for (int lineIndex = 0; lineIndex < maxLines && remainingText.length() > 0; ++lineIndex)
  {
    int breakIndex = remainingText.length();
    if (display.textWidth(remainingText.c_str()) > maxWidth)
    {
      int lastSpace = -1;
      int searchFrom = 0;
      while (true)
      {
        const int nextSpace = remainingText.indexOf(' ', searchFrom);
        const int candidateEnd = nextSpace < 0 ? remainingText.length() : nextSpace;
        if (display.textWidth(remainingText.substring(0, candidateEnd).c_str()) > maxWidth)
        {
          break;
        }
        lastSpace = candidateEnd;
        if (nextSpace < 0)
        {
          break;
        }
        searchFrom = nextSpace + 1;
      }

      breakIndex = lastSpace > 0 ? lastSpace : remainingText.length();
      if (lastSpace <= 0)
      {
        while (breakIndex > 1 && display.textWidth(remainingText.substring(0, breakIndex).c_str()) > maxWidth)
        {
          --breakIndex;
        }
      }
    }

    String line = remainingText.substring(0, breakIndex);
    remainingText = remainingText.substring(breakIndex);
    remainingText.trim();

    if (lineIndex == maxLines - 1 && remainingText.length() > 0)
    {
      while (line.length() > 3 && display.textWidth((line + "...").c_str()) > maxWidth)
      {
        line.remove(line.length() - 1);
      }
      line += "...";
    }

    display.setCursor(x, y + lineIndex * lineHeight);
    display.print(line);
  }
}

void drawUnitLines(const char* units, int x, int y, int maxWidth)
{
  String remaining = units;
  for (int line = 0; line < 2 && remaining.length() > 0; ++line)
  {
    String lineText;
    while (remaining.length() > 0)
    {
      const int separator = remaining.indexOf('\n');
      String unit = separator >= 0 ? remaining.substring(0, separator) : remaining;
      unit.trim();
      const String candidate = lineText.length() == 0 ? unit : lineText + " / " + unit;

      if (lineText.length() > 0 && display.textWidth(candidate.c_str()) > maxWidth)
      {
        break;
      }

      lineText = candidate;
      remaining = separator >= 0 ? remaining.substring(separator + 1) : String();
      remaining.trim();
    }

    display.setCursor(x, y + line * 13);
    if (remaining.length() > 0 && line == 1)
    {
      drawClippedText((lineText + "...").c_str(), maxWidth);
    }
    else
    {
      display.print(lineText);
    }
  }
}

void drawIncidentCard(int index, int x, int y)
{
  const Incident& incident = incidents[index];
  const int cardWidth = 450;
  const int cardHeight = 142;
  time_t newestEpoch = 0;
  for (int incidentIndex = 0; incidentIndex < incidentCount; ++incidentIndex)
  {
    newestEpoch = max(newestEpoch, incidents[incidentIndex].eventEpoch);
  }
  const bool newestIncident = incident.eventEpoch > 0 && incident.eventEpoch == newestEpoch;
  char ageText[24];
  const long ageSeconds = incident.eventEpoch > 0
                              ? static_cast<long>(difftime(time(nullptr), incident.eventEpoch))
                              : 0;
  if (newestIncident)
  {
    snprintf(ageText, sizeof(ageText), "NEWEST");
  }
  else if (ageSeconds < 60)
  {
    snprintf(ageText, sizeof(ageText), "Just now");
  }
  else if (ageSeconds < 3600)
  {
    snprintf(ageText, sizeof(ageText), "%ldm Ago", ageSeconds / 60);
  }
  else
  {
    snprintf(ageText, sizeof(ageText), "%ldh %ldm Ago", ageSeconds / 3600,
             (ageSeconds % 3600) / 60);
  }

  display.fillRoundRect(x, y, cardWidth, cardHeight, 8, COLOR_CARD);
  const bool fireBanner = isFireIncident(incident.title, incident.unit);
  const bool vehicleAccidentBanner = isVehicleAccident(incident.title);
  const bool classOneBanner = isClassOneIncident(incident.title);
  const bool medicalBanner = isMedicalEmergency(incident.title);
  const int bannerWidth = fireBanner || vehicleAccidentBanner || classOneBanner ? 5 : medicalBanner ? 2 : 1;
  const uint16_t borderColor = fireBanner || classOneBanner ? COLOR_DARK_RED : incident.color;
  for (int border = 0; border < bannerWidth; ++border)
  {
    display.drawRoundRect(x + border, y + border,
                          cardWidth - border * 2, cardHeight - border * 2,
                          8 - border, borderColor);
  }

  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(1);
  display.setCursor(x + 20, y + 17);
  drawClippedText(incident.title, 330);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  const int ageWidth = display.textWidth(ageText);
  display.setCursor(x + cardWidth - ageWidth - 16, y + 17);
  display.print(ageText);

  display.setTextColor(0x0000, COLOR_CARD);
  display.setCursor(x + 20, y + 49);
  display.print("LOCATION: ");
  drawClippedText(incident.township, 340);

  display.setCursor(x + 20, y + 70);
  display.print("STREET: ");
  drawClippedText(incident.street, 370);

  display.setCursor(x + 20, y + 95);
  display.print("UNITS: ");
  const int unitsX = x + 80;
  display.setCursor(unitsX, y + 95);
  drawUnitLines(incident.unit, unitsX, y + 95, 380);

  display.setCursor(x + 20, y + 122);
  display.print("COMPANY: ");
  drawClippedText(incident.company, 350);
}

bool matchesIncidentFilter(const Incident& incident)
{
  switch (incidentFilter)
  {
    case IncidentFilter::Fire:
      return isFireIncident(incident.title, incident.unit);
    case IncidentFilter::Medical:
      return isMedicalIncident(incident.title);
    case IncidentFilter::Vehicle:
      return isVehicleAccident(incident.title);
    case IncidentFilter::All:
    default:
      return true;
  }
}

int nextFilteredIncidentIndex(int currentIndex)
{
  for (int offset = 1; offset <= incidentCount; ++offset)
  {
    const int candidate = (currentIndex + offset) % incidentCount;
    if (matchesIncidentFilter(incidents[candidate]))
    {
      return candidate;
    }
  }
  return currentIndex;
}

int firstFilteredIncidentIndex()
{
  for (int index = 0; index < incidentCount; ++index)
  {
    if (matchesIncidentFilter(incidents[index]))
    {
      return index;
    }
  }
  return -1;
}

bool hasFilteredIncidents()
{
  return firstFilteredIncidentIndex() >= 0;
}

void selectIncidentFilter(IncidentFilter filter)
{
  incidentFilter = incidentFilter == filter ? IncidentFilter::All : filter;
  incidentOffset = firstFilteredIncidentIndex();
  if (incidentOffset < 0)
  {
    incidentOffset = 0;
  }
  lastIncidentCycle = millis();
}

String resolveUnitDescription(const String& unitText)
{
  String units = unitText;
  units.replace("<br>", " ");
  units.replace("<br/>", " ");
  units.replace("<br />", " ");
  units.replace(";", " ");
  units.replace("/", " ");
  units.replace("\n", " ");
  while (units.indexOf("  ") >= 0)
  {
    units.replace("  ", " ");
  }
  units.trim();

  if (units.length() == 0)
  {
    return "No unit assignment provided.";
  }

  String firstToken = units;
  const int firstSpace = firstToken.indexOf(' ');
  if (firstSpace >= 0)
  {
    firstToken = firstToken.substring(0, firstSpace);
  }
  firstToken.toUpperCase();

  struct UnitCodeDescription
  {
    const char* prefix;
    const char* description;
  };
  // Ordered longest-prefix-first so e.g. "MICU" is matched before the shorter "MU"/"INT" checks.
  static const UnitCodeDescription unitCodeDescriptions[] = {
    {"UTILITY", "Utility: support and equipment unit."},
    {"ENGINE", "Engine: fire suppression and initial attack unit."},
    {"LADDER", "Ladder: aerial and elevated access support."},
    {"RESCUE", "Rescue: technical rescue and support operations."},
    {"TANKER", "Tanker: water supply apparatus."},
    {"DEPUTY", "Deputy: fire department chief officer vehicle."},
    {"MEDIC", "Medic Unit: ALS/BLS ambulance response."},
    {"TRUCK", "Truck: aerial and elevated access support."},
    {"BRUSH", "Brush unit: wildland/grass fire apparatus."},
    {"SQUAD", "Squad: multi-role support apparatus."},
    {"MICU", "MICU: mobile intensive care ambulance."},
    {"AMB", "Ambulance: BLS patient transport unit."},
    {"OPS", "Operations: incident command support officer."},
    {"INT", "Intercept: ambulance intercept or medical support."},
    {"STA", "Station: assigned fire station / apparatus support."},
    {"SQ", "Squad: multi-role support apparatus."},
    {"MU", "Medic Unit: ALS/BLS ambulance response."},
    {"PT", "Patient Transport: non-emergency transport unit."},
    {"FP", "Fire Police: traffic and scene control."},
    {"E", "Engine: fire suppression and initial attack unit."},
    {"R", "Rescue: technical rescue and support operations."},
    {"L", "Ladder: aerial and elevated access support."}
  };
  const int unitCodeCount = sizeof(unitCodeDescriptions) / sizeof(unitCodeDescriptions[0]);
  for (int index = 0; index < unitCodeCount; ++index)
  {
    if (firstToken.startsWith(unitCodeDescriptions[index].prefix))
    {
      return unitCodeDescriptions[index].description;
    }
  }

  return "Unit assignment: " + units + ". Determine assignment from call notes.";
}

void drawUnitsHelpPopup()
{
  const int popupX = 30;
  const int popupY = 56;
  const int popupWidth = 420;
  const int popupHeight = 205;
  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 22, popupY + 18);
  display.print("UNIT CODES");
  display.setTextSize(1);
  display.setCursor(popupX + 22, popupY + 59);
  display.print("MU / M   Medic unit");
  display.setCursor(popupX + 22, popupY + 80);
  display.print("E       Engine");
  display.setCursor(popupX + 22, popupY + 101);
  display.print("R       Rescue");
  display.setCursor(popupX + 210, popupY + 59);
  display.print("L       Ladder");
  display.setCursor(popupX + 210, popupY + 80);
  display.print("SQ      Squad");
  display.setCursor(popupX + 210, popupY + 101);
  display.print("STA     Station");
  display.setCursor(popupX + 22, popupY + 133);
  display.print("INT     Intercept medic");
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 22, popupY + 173);
  display.print("Tap anywhere to close");
}

void drawUnitDetailPopup()
{
  const int popupX = 9;
  const int popupY = 50;
  const int popupWidth = 462;
  const int popupHeight = 230;
  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 20, popupY + 18);
  display.print("UNIT INFO");
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 20, popupY + 54);
  display.print("Description:");
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setCursor(popupX + 20, popupY + 74);
  drawWrappedText(unitDetailText.c_str(), popupX + 20, popupY + 74, popupWidth - 40, 8, 15);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 20, popupY + 208);
  display.print("Tap anywhere to close");
}

void drawWifiInfoPopup()
{
  const int popupX = 52;
  const int popupY = 92;
  const int popupWidth = 375;
  const int popupHeight = 136;
  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 22, popupY + 18);
  display.print("WI-FI STATUS");
  display.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED)
  {
    char rssiText[24];
    snprintf(rssiText, sizeof(rssiText), "Signal: %d dBm", WiFi.RSSI());
    display.setCursor(popupX + 22, popupY + 62);
    display.print("Connected");
    display.setCursor(popupX + 22, popupY + 82);
    display.print(rssiText);
  }
  else
  {
    display.setCursor(popupX + 22, popupY + 62);
    display.print("Not connected");
  }
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 22, popupY + 112);
  display.print("Tap anywhere to close");
}

void drawCountyMapPopup()
{
  const int popupX = 40;
  const int popupY = 14;
  const int popupWidth = 400;
  const int popupHeight = 292;
  const int mapScale = 7;
  const int mapOriginX = popupX + (popupWidth - 29 * mapScale) / 2;
  const int mapOriginY = popupY + 46;

  display.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CARD);
  display.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 8, COLOR_CYAN);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  display.setCursor(popupX + 22, popupY + 16);
  display.print("INCIDENT MAP");

  drawSmoothCountyOutline(mapOriginX, mapOriginY, mapScale, COLOR_DARK_BLUE);

  const Incident& incident = incidents[incidentOffset % incidentCount];
  int companyX = 0;
  int companyY = 0;
  const bool hasLocation = findCompanyLocation(incident.company, companyX, companyY);

  display.setTextSize(1);
  if (hasLocation)
  {
    const int dotX = mapOriginX + companyX * mapScale;
    const int dotY = mapOriginY + companyY * mapScale;
    display.fillCircle(dotX, dotY, 5, COLOR_RED);
    display.drawCircle(dotX, dotY, 5, COLOR_WHITE);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.setCursor(popupX + 22, popupY + 250);
    drawClippedText(incident.company, popupWidth - 44);
  }
  else
  {
    display.setTextColor(COLOR_AMBER, COLOR_CARD);
    display.setCursor(popupX + 22, popupY + 250);
    display.print("Location unknown for this company");
  }

  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setCursor(popupX + 22, popupY + 270);
  display.print("Tap anywhere to close");
}

void drawCompanyDirectory()
{
  const int companyCount = sizeof(fireCompanies) / sizeof(fireCompanies[0]);
  const int pageCount = (companyCount + COMPANIES_PER_PAGE - 1) / COMPANIES_PER_PAGE;
  const int firstCompany = companyDirectoryPage * COMPANIES_PER_PAGE;
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, display.width(), 40, COLOR_HEADER);
  display.setTextColor(COLOR_WHITE, COLOR_HEADER);
  display.setTextSize(1);
  display.setCursor(18, 14);
  display.print("LANCASTER FIRE COMPANIES");
  char pageText[8];
  snprintf(pageText, sizeof(pageText), "%d/%d", companyDirectoryPage + 1, pageCount);
  const int pageWidth = display.textWidth(pageText);
  display.setCursor(405 - pageWidth, 14);
  display.print(pageText);
  display.drawRoundRect(430, 5, 36, 32, 5, COLOR_CYAN);
  display.setCursor(442, 14);
  display.print("X");

  display.setTextColor(COLOR_WHITE, COLOR_BG);
  for (int row = 0; row < COMPANIES_PER_PAGE && firstCompany + row < companyCount; ++row)
  {
    const int y = 58 + row * 20;
    const FireCompany& company = fireCompanies[firstCompany + row];
    display.setCursor(30, y);
    display.print(company.station);
    display.setCursor(78, y);
    drawClippedText(company.name, 340);
  }

  display.drawRoundRect(30, 274, 60, 36, 5, COLOR_CYAN);
  display.drawRoundRect(390, 274, 60, 36, 5, COLOR_CYAN);
  display.setTextColor(COLOR_CYAN, COLOR_BG);
  display.setTextSize(2);
  display.setCursor(52, 282);
  display.print("<");
  display.setCursor(412, 282);
  display.print(">");
}

void drawNoFilteredIncidents(int x, int y)
{
  const char* message = incidentFilter == IncidentFilter::Vehicle
                            ? "No accident incidents"
                            : incidentFilter == IncidentFilter::Fire
                                  ? "No fire incidents"
                                  : "No medical incidents";
  display.fillRoundRect(x, y, 450, 142, 8, COLOR_CARD);
  display.drawRoundRect(x, y, 450, 142, 8, COLOR_DARK_GRAY);
  display.setTextColor(COLOR_WHITE, COLOR_CARD);
  display.setTextSize(2);
  const int messageWidth = display.textWidth(message);
  display.setCursor(x + (450 - messageWidth) / 2, y + 46);
  display.print(message);
  display.setTextColor(COLOR_CYAN, COLOR_CARD);
  display.setTextSize(1);
  const char* detail = "Tap the selected filter again for all";
  const int detailWidth = display.textWidth(detail);
  display.setCursor(x + (450 - detailWidth) / 2, y + 92);
  display.print(detail);
}

void drawDashboard(bool fullRedraw = true)
{
  struct tm localTime;
  const bool hasCurrentTime = clockSynced && getLocalTime(&localTime, 0);
  const unsigned long epochSeconds = millis() / 1000UL;
  const int minutes = hasCurrentTime ? localTime.tm_min : (epochSeconds / 60) % 60;
  const int hours = hasCurrentTime ? localTime.tm_hour : (epochSeconds / 3600) % 24;
  char dateText[9] = "--/--/--";
  char respondingText[8];
  char accidentText[8];

  snprintf(respondingText, sizeof(respondingText), "%d", respondingUnitCount);
  snprintf(accidentText, sizeof(accidentText), "%d", accidentIncidentCount);

  if (hasCurrentTime)
  {
    strftime(dateText, sizeof(dateText), "%m/%d/%y", &localTime);
  }

  display.startWrite();
  if (fullRedraw)
  {
    display.fillScreen(COLOR_BG);
    display.fillRect(0, 0, display.width(), 40, COLOR_HEADER);
    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.setTextSize(1.5f, 1.5f);
    display.setCursor(15, 13);
    display.print("Lancaster Fire & EMS LIVE");
  }
  else
  {
    display.fillRect(390, 0, 90, 40, COLOR_HEADER);
  }

  display.setTextColor(COLOR_CYAN, COLOR_HEADER);
  display.setTextSize(1);
  display.setCursor(420, 14);
  display.print(hours < 10 ? "0" : "");
  display.print(hours);
  display.print(":");
  display.print(minutes < 10 ? "0" : "");
  display.print(minutes);

  display.setCursor(402, 26);
  display.print(dateText);

  if (fullRedraw)
  {
    drawLancasterCountyOutline();
  }
  else
  {
    display.fillRect(338, 0, 45, 40, COLOR_HEADER);
  }
  drawWifiSignalBars();
  drawRotationButton();

  drawActiveCard(15, 54);
  drawMetricCard(255, 54, 98, 68, "Responding", "Units", respondingText, COLOR_DARK_BLUE);
  drawMetricCard(368, 54, 98, 68, "Vehicle", "Accidents", accidentText, COLOR_AMBER);

  if (fullRedraw)
  {
    display.fillRoundRect(15, 133, 450, 24, 6, 0x1A1A);
    display.setTextColor(COLOR_WHITE, 0x1A1A);
    display.setTextSize(1);
    const char* incidentHeader = "LANCASTER COUNTY INCIDENTS";
    display.setCursor(22, 140);
    drawClippedText(incidentHeader, 293);
  }
  else
  {
    display.fillRect(338, 133, 127, 24, 0x1A1A);
  }

  char feedStatus[20];
  if (feedHealthy && lastSuccessfulFeedEpoch > 0)
  {
    struct tm feedTime;
    localtime_r(&lastSuccessfulFeedEpoch, &feedTime);
    snprintf(feedStatus, sizeof(feedStatus), "Feed OK %02d:%02d",
             feedTime.tm_hour, feedTime.tm_min);
  }
  else
  {
    snprintf(feedStatus, sizeof(feedStatus), "Feed Error");
  }
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN, 0x1A1A);
  const int feedStatusWidth = display.textWidth(feedStatus);
  display.setCursor(465 - feedStatusWidth, 140);
  display.print(feedStatus);

  if (incidentFilter != IncidentFilter::All && !hasFilteredIncidents())
  {
    drawNoFilteredIncidents(15, 168);
  }
  else
  {
    drawIncidentCard(incidentOffset % incidentCount, 15, 168);
  }

  if (unitsHelpVisible)
  {
    drawUnitsHelpPopup();
  }
  if (unitDetailVisible)
  {
    drawUnitDetailPopup();
  }
  if (wifiInfoVisible)
  {
    drawWifiInfoPopup();
  }
  if (countyMapVisible)
  {
    drawCountyMapPopup();
  }
  if (companyDirectoryVisible)
  {
    drawCompanyDirectory();
  }

  display.endWrite();
}

void refreshDashboardOnTouch()
{
  static bool wasTouched = false;
  static uint16_t touchStartX = 0;
  static uint16_t touchStartY = 0;

  if (display.getTouch(&touchX, &touchY))
  {
    if (!wasTouched)
    {
      touchStartX = touchX;
      touchStartY = touchY;
      if (unitDetailVisible)
      {
        unitDetailVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (unitsHelpVisible)
      {
        unitsHelpVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (wifiInfoVisible)
      {
        wifiInfoVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (countyMapVisible)
      {
        countyMapVisible = false;
        incidentRotationPaused = false;
        drawDashboard();
      }
      else if (companyDirectoryVisible)
      {
        const int companyCount = sizeof(fireCompanies) / sizeof(fireCompanies[0]);
        const int pageCount = (companyCount + COMPANIES_PER_PAGE - 1) / COMPANIES_PER_PAGE;
        if (touchX >= 430 && touchY < 40)
        {
          companyDirectoryVisible = false;
          incidentRotationPaused = false;
          drawDashboard();
        }
        else if (touchX >= 30 && touchX < 90 && touchY >= 274)
        {
          companyDirectoryPage = companyDirectoryPage == 0 ? pageCount - 1 : companyDirectoryPage - 1;
          drawCompanyDirectory();
        }
        else if (touchX >= 390 && touchX < 450 && touchY >= 274)
        {
          companyDirectoryPage = (companyDirectoryPage + 1) % pageCount;
          drawCompanyDirectory();
        }
      }
      else if (touchX >= COUNTY_OUTLINE_X + 55 && touchX < COUNTY_OUTLINE_X + 85 &&
               touchY >= COUNTY_OUTLINE_Y && touchY < COUNTY_OUTLINE_Y + 40)
      {
        wifiInfoVisible = true;
        incidentRotationPaused = true;
        drawDashboard();
      }
      else if (touchX >= COUNTY_OUTLINE_X && touchX < COUNTY_OUTLINE_X + 45 &&
               touchY >= COUNTY_OUTLINE_Y && touchY < COUNTY_OUTLINE_Y + 40)
      {
        companyDirectoryVisible = true;
        companyDirectoryPage = 0;
        incidentRotationPaused = true;
        drawCompanyDirectory();
      }
      else if (touchX >= 15 && touchX < 87 && touchY >= 168 && touchY < 310)
      {
        countyMapVisible = true;
        incidentRotationPaused = true;
        drawDashboard();
      }
      else if (touchX >= 87 && touchX < 450 && touchY >= 168 && touchY < 310)
      {
        unitDetailVisible = true;
        unitDetailText = resolveUnitDescription(incidents[incidentOffset % incidentCount].unit);
        incidentRotationPaused = true;
        drawDashboard();
      }
      else if (touchX >= 15 && touchX < 128 && touchY >= 54 && touchY < 122)
      {
        selectIncidentFilter(IncidentFilter::Fire);
        drawDashboard();
      }
      else if (touchX >= 128 && touchX < 240 && touchY >= 54 && touchY < 122)
      {
        selectIncidentFilter(IncidentFilter::Medical);
        drawDashboard();
      }
      else if (touchX >= 368 && touchX < 466 && touchY >= 54 && touchY < 122)
      {
        selectIncidentFilter(IncidentFilter::Vehicle);
        drawDashboard();
      }
      else if (touchX >= ROTATION_BUTTON_X &&
          touchX < ROTATION_BUTTON_X + ROTATION_BUTTON_W &&
          touchY >= ROTATION_BUTTON_Y &&
          touchY < ROTATION_BUTTON_Y + ROTATION_BUTTON_H)
      {
        incidentRotationPaused = !incidentRotationPaused;
        lastIncidentCycle = millis();
        display.startWrite();
        drawRotationButton();
        display.endWrite();
        Serial.println(incidentRotationPaused ? "Incident rotation paused" : "Incident rotation resumed");
      }
    }
    Serial.printf("Touch: X=%d Y=%d\n", touchX, touchY);
    wasTouched = true;
    return;
  }

  if (wasTouched)
  {
    wasTouched = false;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("Fire & EMS Incident Dashboard starting...");

  display.init();
  display.setColorDepth(16);
  display.setRotation(1);
  if (shouldCalibrateTouch())
  {
    calibrateTouchAtBoot();
  }
  else
  {
    loadTouchCalibration();
  }
  connectAndSyncTime();
  feedHealthy = fetchLiveIncidents();
  lastIncidentFeedUpdate = millis();
  drawDashboard();

  Serial.println("Dashboard initialized");
}

void loop()
{
  refreshDashboardOnTouch();

  if (!incidentRotationPaused && millis() - lastIncidentCycle >= INCIDENT_CYCLE_MS)
  {
    lastIncidentCycle = millis();
    if (incidentFilter == IncidentFilter::All)
    {
      incidentOffset = (incidentOffset + 1) % incidentCount;
    }
    else
    {
      incidentOffset = nextFilteredIncidentIndex(incidentOffset);
    }
    drawDashboard(false);
  }

  if (millis() - lastIncidentFeedUpdate >= INCIDENT_FEED_REFRESH_MS)
  {
    lastIncidentFeedUpdate = millis();
    feedHealthy = fetchLiveIncidents();
    if (feedHealthy)
    {
      incidentOffset = incidentFilter == IncidentFilter::All ? 0 : firstFilteredIncidentIndex();
      if (incidentOffset < 0)
      {
        incidentOffset = 0;
      }
    }
    drawDashboard(false);
  }

  delay(50);
}


