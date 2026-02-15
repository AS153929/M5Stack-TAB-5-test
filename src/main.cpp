#include <Arduino.h>
#include <M5Unified.h>
#include <SD_MMC.h>
#include <qrcode.h>
#include "logo.h"

namespace {
const char* kAppName = "M5Stack Tab 5 Adventure";
const char* kAppVersion = "0.1.0";
const char* kAuthor = "Yule Show";
const char* kGithubUrl = "https://github.com/yuleshow";

constexpr uint8_t kBrightness = 50;
constexpr int kRotationLandscape = 3;

enum class Screen {
  Welcome,
  Dashboard,
  App1,
  App2,
  App3,
  App4,
  App5,
  App6,
  App7,
  App8
};

struct Icon {
  const char* label;
  int x;
  int y;
  int w;
  int h;
};

Screen g_screen = Screen::Welcome;
bool g_needsRedraw = true;
bool g_sdMounted = false;
Icon g_icons[8];

// Photo frame state
String g_photoFiles[100];
int g_photoCount = 0;
int g_currentPhotoIndex = -1;
unsigned long g_lastPhotoChange = 0;
const unsigned long kPhotoInterval = 15000; // 15 seconds
bool g_forcePhotoRedraw = false;

// Calendar state
struct CalendarEvent {
  String summary;
  int year;
  int month;
  int day;
  String time;
  String rrule;  // Recurrence rule
};
CalendarEvent g_events[800];
int g_eventCount = 0;
int g_calendarYear = 2026;
int g_calendarMonth = 2; // February
int g_calendarDay = 9; // Current day for week view
int g_weekOffset = 0; // Week offset from current week

// Todo state
struct TodoTask {
  String title;
  bool completed;
};
TodoTask g_tasks[50];
int g_taskCount = 0;
int g_taskScrollOffset = 0;

// Custom fonts from SD card
bool g_fontsLoaded = false;

constexpr int kIconCount = 8;
constexpr int kIconDisplaySize = 200;  // Size to display icons on screen

const char* kIconLabels[kIconCount] = {
  "Calendar", "To-Do", "Photo Frame", "News",
  "Weather", "Demo", "Setup", "About"
};

char g_iconPath[64];
const char* getIconPath(int index) {
  snprintf(g_iconPath, sizeof(g_iconPath), "/M5Stack-Tab-5-Adventure/icons/icon-%d.png", index + 1);
  return g_iconPath;
}

// Load custom fonts from SD card
void loadCustomFonts() {
  // Disabled for now - use built-in fonts
  g_fontsLoaded = false;
}

// Helper to set font for calendar events (small size)
void setCalendarFont() {
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(1.5);
}

// Helper to set font for todo list (medium size)
void setTodoFont() {
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(2);
}

// Unload custom font
void unloadCustomFont() {
  M5.Display.setFont(&fonts::Font0);
}

// Parse date from YYYYMMDD format
void parseDate(const String& dateStr, int& year, int& month, int& day) {
  if (dateStr.length() >= 8) {
    year = dateStr.substring(0, 4).toInt();
    month = dateStr.substring(4, 6).toInt();
    day = dateStr.substring(6, 8).toInt();
  }
}

// Parse time from HHMMSS format
String parseTime(const String& timeStr) {
  if (timeStr.length() >= 6) {
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(2, 4).toInt();
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    return String(buf);
  }
  return "";
}

// Load calendar events from .ics file
void loadCalendarEvents() {
  g_eventCount = 0;
  if (!g_sdMounted) return;
  
  File file = SD_MMC.open("/M5Stack-Tab-5-Adventure/calendar/calendar.ics");
  if (!file) return;
  
  String currentSummary = "";
  String currentDate = "";
  String currentTime = "";
  String currentRRule = "";
  
  while (file.available() && g_eventCount < 800) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    if (line.startsWith("SUMMARY:")) {
      currentSummary = line.substring(8);
    } else if (line.startsWith("RRULE:")) {
      currentRRule = line.substring(6);
    } else if (line.startsWith("DTSTART")) {
      int colonPos = line.indexOf(':');
      if (colonPos > 0) {
        String datetime = line.substring(colonPos + 1);
        if (datetime.length() >= 8) {
          currentDate = datetime.substring(0, 8);
          if (datetime.length() >= 15 && datetime.charAt(8) == 'T') {
            currentTime = parseTime(datetime.substring(9, 15));
          }
        }
      }
    } else if (line.startsWith("END:VEVENT")) {
      if (currentSummary.length() > 0 && currentDate.length() >= 8) {
        int year, month, day;
        parseDate(currentDate, year, month, day);
        g_events[g_eventCount] = {currentSummary, year, month, day, currentTime, currentRRule};
        g_eventCount++;
      }
      currentSummary = "";
      currentDate = "";
      currentTime = "";
      currentRRule = "";
    }
  }
  file.close();
}

// Load todo tasks from JSON
void loadTodoTasks() {
  g_taskCount = 0;
  if (!g_sdMounted) return;
  
  File file = SD_MMC.open("/M5Stack-Tab-5-Adventure/tasks/tasks.json");
  if (!file) return;
  
  // Read entire file as bytes to preserve UTF-8 encoding
  size_t fileSize = file.size();
  uint8_t* buffer = (uint8_t*)malloc(fileSize + 1);
  if (!buffer) {
    file.close();
    return;
  }
  
  file.read(buffer, fileSize);
  buffer[fileSize] = 0;
  String content = String((char*)buffer);
  free(buffer);
  file.close();
  
  // Simple JSON parsing for tasks - improved for UTF-8
  int pos = 0;
  while (pos < content.length() && g_taskCount < 50) {
    int titlePos = content.indexOf("\"title\"", pos);
    if (titlePos < 0) break;
    
    int valueStart = content.indexOf(":", titlePos);
    if (valueStart < 0) break;
    valueStart = content.indexOf("\"", valueStart);
    if (valueStart < 0) break;
    valueStart++;
    
    int valueEnd = valueStart;
    while (valueEnd < content.length()) {
      if (content.charAt(valueEnd) == '"' && content.charAt(valueEnd - 1) != '\\') {
        break;
      }
      valueEnd++;
    }
    
    String title = content.substring(valueStart, valueEnd);
    
    // Check if completed
    bool completed = false;
    int statusPos = content.indexOf("\"status\"", valueEnd);
    int nextTaskPos = content.indexOf("\"title\"", valueEnd + 1);
    if (statusPos > 0 && (nextTaskPos < 0 || statusPos < nextTaskPos)) {
      int statusStart = content.indexOf("\"", statusPos + 8);
      if (statusStart > 0) {
        statusStart++;
        int statusEnd = content.indexOf("\"", statusStart);
        if (statusEnd > 0) {
          String status = content.substring(statusStart, statusEnd);
          completed = (status == "completed");
        }
      }
    }
    
    if (title.length() > 0) {
      g_tasks[g_taskCount] = {title, completed};
      g_taskCount++;
    }
    
    pos = valueEnd + 1;
  }
}

// Calendar helper functions
int getDaysInMonth(int year, int month) {
  if (month == 2) {
    bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return isLeap ? 29 : 28;
  }
  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }
  return 31;
}

int getFirstDayOfMonth(int year, int month) {
  // Zeller's congruence for day of week (0=Sun, 1=Mon, etc.)
  if (month < 3) {
    month += 12;
    year -= 1;
  }
  int q = 1; // First day
  int m = month;
  int k = year % 100;
  int j = year / 100;
  int h = (q + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;
  // Convert to 0=Sun, 1=Mon, etc.
  return (h + 6) % 7;
}

int getDayOfWeek(int year, int month, int day) {
  // Zeller's congruence
  if (month < 3) {
    month += 12;
    year -= 1;
  }
  int q = day;
  int m = month;
  int k = year % 100;
  int j = year / 100;
  int h = (q + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;
  return (h + 6) % 7;
}

void advanceDate(int& year, int& month, int& day, int days) {
  day += days;
  while (day > getDaysInMonth(year, month)) {
    day -= getDaysInMonth(year, month);
    month++;
    if (month > 12) {
      month = 1;
      year++;
    }
  }
  while (day < 1) {
    month--;
    if (month < 1) {
      month = 12;
      year--;
    }
    day += getDaysInMonth(year, month);
  }
}

// Check if an event occurs on a specific date (considering recurrence)
bool eventOccursOnDate(const CalendarEvent& event, int year, int month, int day) {
  // Direct date match
  if (event.year == year && event.month == month && event.day == day) {
    return true;
  }
  
  // Check recurrence rule
  if (event.rrule.length() > 0) {
    // Check UNTIL clause if present
    int untilPos = event.rrule.indexOf("UNTIL=");
    if (untilPos >= 0) {
      String untilStr = event.rrule.substring(untilPos + 6);
      int semicolon = untilStr.indexOf(';');
      if (semicolon > 0) {
        untilStr = untilStr.substring(0, semicolon);
      }
      // Parse UNTIL date (format: YYYYMMDD)
      if (untilStr.length() >= 8) {
        int untilYear = untilStr.substring(0, 4).toInt();
        int untilMonth = untilStr.substring(4, 6).toInt();
        int untilDay = untilStr.substring(6, 8).toInt();
        
        // Check if current date is after UNTIL date
        if (year > untilYear || 
            (year == untilYear && month > untilMonth) ||
            (year == untilYear && month == untilMonth && day > untilDay)) {
          return false; // Event has expired
        }
      }
    }
    
    // Handle FREQ=YEARLY (most common in the data)
    if (event.rrule.indexOf("FREQ=YEARLY") >= 0) {
      // Event recurs yearly on same month/day
      if (event.month == month && event.day == day) {
        // Only show if current date is on or after the original event date
        if (year > event.year || (year == event.year && month >= event.month)) {
          return true;
        }
      }
    }
    // Can add more FREQ types here (DAILY, WEEKLY, MONTHLY)
  }
  
  return false;
}

void drawCalendar() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  
  // Calculate week start
  int year = g_calendarYear;
  int month = g_calendarMonth;
  int day = g_calendarDay;
  
  // Add week offset
  advanceDate(year, month, day, g_weekOffset * 7);
  
  int startDow = getDayOfWeek(year, month, day);
  int startYear = year, startMonth = month, startDay = day;
  
  // Go back to Sunday of this week
  advanceDate(startYear, startMonth, startDay, -startDow);
  
  // Header with date range
  int endYear = startYear, endMonth = startMonth, endDay = startDay;
  advanceDate(endYear, endMonth, endDay, 6);
  
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(TC_DATUM);
  const char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char header[64];
  if (startMonth == endMonth) {
    snprintf(header, sizeof(header), "%s %d-%d, %d", monthNames[startMonth], startDay, endDay, startYear);
  } else {
    snprintf(header, sizeof(header), "%s %d - %s %d, %d", 
             monthNames[startMonth], startDay, monthNames[endMonth], endDay, endYear);
  }
  M5.Display.drawString(header, M5.Display.width() / 2, 10);
  
  // Day names
  const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  
  // Draw 7 horizontal blocks for the week
  int w = M5.Display.width();
  int headerHeight = 50;
  int footerHeight = 25;
  int availableHeight = M5.Display.height() - headerHeight - footerHeight;
  int cellH = availableHeight / 7;
  int gridStartY = headerHeight;
  
  for (int dow = 0; dow < 7; dow++) {
    int dayYear = startYear;
    int dayMonth = startMonth;
    int dayDay = startDay;
    advanceDate(dayYear, dayMonth, dayDay, dow);
    
    int y = gridStartY + (dow * cellH);
    
    // Highlight today
    bool isToday = (dayYear == 2026 && dayMonth == 2 && dayDay == 9);
    if (isToday) {
      M5.Display.fillRect(2, y + 2, w - 4, cellH - 4, TFT_DARKGREY);
    }
    
    // Draw day name and date on the left
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(2);
    char dayLabel[32];
    snprintf(dayLabel, sizeof(dayLabel), "%s %d/%d", dayNames[dow], dayMonth, dayDay);
    M5.Display.drawString(dayLabel, 10, y + 5);
    
    // Show events for this day - arranged horizontally with wrapping
    int eventX = 200;  // Start events after the date
    int eventY = y + 10;
    int lineHeight = 30;
    setCalendarFont();
    M5.Display.setTextSize(1);
    
    for (int i = 0; i < g_eventCount; i++) {
      // Use recurrence-aware date matching
      if (eventOccursOnDate(g_events[i], dayYear, dayMonth, dayDay)) {
        
        // Check if we need to wrap to next line
        if (eventX > w - 300 && eventY < y + cellH - lineHeight - 5) {
          eventX = 200;
          eventY += lineHeight;
        }
        
        // Stop if we run out of vertical space
        if (eventY > y + cellH - lineHeight - 5) {
          break;
        }
        
        // Show time if available
        if (g_events[i].time.length() > 0) {
          M5.Display.setTextColor(TFT_CYAN);
          M5.Display.drawString(g_events[i].time, eventX, eventY);
          eventX += 80;
        }
        
        // Show event name
        M5.Display.setTextColor(TFT_YELLOW);
        String eventText = g_events[i].summary;
        // Limit event text width
        if (eventText.length() > 20) {
          eventText = eventText.substring(0, 20) + "...";
        }
        M5.Display.drawString(eventText, eventX, eventY);
        M5.Display.setTextColor(TFT_WHITE);
        
        // Move to next event position
        eventX += 250;
      }
    }
    unloadCustomFont();
    
    // Cell border
    M5.Display.drawRect(0, y, w, cellH, TFT_DARKGREY);
  }
  
  // Navigation hint
  M5.Display.setTextDatum(BC_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.drawString("< Prev Week | Next Week > | Tap top-left to exit", 
                        M5.Display.width() / 2, M5.Display.height() - 5);
  M5.Display.setTextDatum(TL_DATUM);
}

void drawTodoList() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  
  // Header
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.drawString("To-Do List", M5.Display.width() / 2, 10);
  
  // Task list
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(TL_DATUM);
  int y = 50;
  int taskHeight = 80;
  int visibleTasks = (M5.Display.height() - 80) / taskHeight;
  
  if (g_taskCount == 0) {
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("No tasks found", M5.Display.width() / 2, M5.Display.height() / 2);
    M5.Display.setTextDatum(TL_DATUM);
  } else {
    setTodoFont();
    for (int i = g_taskScrollOffset; i < g_taskCount && i < g_taskScrollOffset + visibleTasks; i++) {
      int taskY = y + (i - g_taskScrollOffset) * taskHeight;
      
      // Checkbox
      int cbSize = 40;
      int cbX = 20;
      int cbY = taskY + 10;
      unloadCustomFont();
      M5.Display.drawRect(cbX, cbY, cbSize, cbSize, TFT_WHITE);
      if (g_tasks[i].completed) {
        M5.Display.fillRect(cbX + 3, cbY + 3, cbSize - 6, cbSize - 6, TFT_GREEN);
      }
      
      // Task text
      setTodoFont();
      String title = g_tasks[i].title;
      if (title.length() > 28) {
        title = title.substring(0, 28) + "...";
      }
      uint32_t textColor = g_tasks[i].completed ? TFT_DARKGREY : TFT_WHITE;
      M5.Display.setTextColor(textColor);
      M5.Display.drawString(title, cbX + cbSize + 10, taskY + 15);
      M5.Display.setTextColor(TFT_WHITE);
      
      // Separator line
      unloadCustomFont();
      M5.Display.drawLine(10, taskY + taskHeight - 2, M5.Display.width() - 10, 
                         taskY + taskHeight - 2, TFT_DARKGREY);
    }
    unloadCustomFont();
  }
  
  // Scroll indicator and navigation hint
  M5.Display.setTextDatum(BC_DATUM);
  if (g_taskCount > visibleTasks) {
    char scrollInfo[32];
    snprintf(scrollInfo, sizeof(scrollInfo), "Task %d-%d of %d", 
             g_taskScrollOffset + 1, 
             min(g_taskScrollOffset + visibleTasks, g_taskCount),
             g_taskCount);
    M5.Display.drawString(scrollInfo, M5.Display.width() / 2, M5.Display.height() - 20);
  }
  M5.Display.drawString("Left: Scroll Up | Right: Scroll Down | Top-left: Exit",
                        M5.Display.width() / 2, M5.Display.height() - 5);
  M5.Display.setTextDatum(TL_DATUM);
}

void drawQRCode(const char* text, int x, int y, int size) {
  const int version = 4;  // 33x33 modules
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, text);

  int scale = size / qrcode.size;
  if (scale < 1) {
    scale = 1;
  }
  int drawSize = qrcode.size * scale;
  M5.Display.fillRect(x, y, drawSize, drawSize, TFT_WHITE);
  for (int row = 0; row < qrcode.size; ++row) {
    for (int col = 0; col < qrcode.size; ++col) {
      if (qrcode_getModule(&qrcode, col, row)) {
        M5.Display.fillRect(x + col * scale, y + row * scale, scale, scale, TFT_BLACK);
      }
    }
  }
  M5.Display.drawRect(x, y, drawSize, drawSize, TFT_BLACK);
}

void layoutIcons() {
  int w = M5.Display.width();
  int h = M5.Display.height();
  int margin = 20;
  int gap = 12;
  int rows = 2;
  int cols = 4;

  int iconW = (w - margin * 2 - gap * (cols - 1)) / cols;
  int iconH = (h - margin * 2 - gap * (rows - 1)) / rows;

  for (int i = 0; i < kIconCount; ++i) {
    int row = i / cols;
    int col = i % cols;
    int x = margin + col * (iconW + gap);
    int y = margin + row * (iconH + gap);
    g_icons[i] = {kIconLabels[i], x, y, iconW, iconH};
  }
}

void drawWelcome() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);

  int w = M5.Display.width();
  int h = M5.Display.height();

  // Large centered app name - use smooth FreeSans font
  M5.Display.setFont(&fonts::FreeSans24pt7b);
  M5.Display.setTextSize(1);  // No scaling for smooth font
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.drawString(kAppName, w / 2, h / 2 - 50);
  
  // Chinese subtitle centered
  M5.Display.setFont(&fonts::efontTW_24);
  M5.Display.setTextSize(1.5);  // Slightly larger than normal
  M5.Display.drawString("有趣的ESP32之旅", w / 2, h / 2 + 10);
  M5.Display.setFont(&fonts::Font0);

  // Left-aligned version info - larger text
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, h / 2 + 50);
  M5.Display.print("Version: ");
  M5.Display.println(kAppVersion);
  M5.Display.setCursor(20, h / 2 + 75);
  M5.Display.print("Author: ");
  M5.Display.println(kAuthor);
  M5.Display.setCursor(20, h / 2 + 100);
  M5.Display.print("Build: ");
  M5.Display.print(__DATE__);
  M5.Display.print(" ");
  M5.Display.println(__TIME__);
  
  // Show SD card status
  M5.Display.setCursor(20, h / 2 + 125);
  M5.Display.print("SD Card: ");
  if (g_sdMounted) {
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.println("OK");
    M5.Display.setTextColor(TFT_WHITE);
  } else {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.println("Not Found");
    M5.Display.setTextColor(TFT_WHITE);
  }

  int qrSize = 140;
  int logoX = 150;
  int logoY = 150;
  int qrX = w - qrSize - 20;
  int qrY = h - qrSize - 20;

  // Draw logo
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(logoX, logoY, kLogoWidth, kLogoHeight, kLogoData);
  M5.Display.setSwapBytes(false);

  // Draw QR code
  drawQRCode(kGithubUrl, qrX, qrY, qrSize);
  M5.Display.setCursor(qrX, qrY - 14);
  M5.Display.print("GitHub");
}

void drawDashboard() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  layoutIcons();
  
  for (int i = 0; i < kIconCount; ++i) {
    const auto& icon = g_icons[i];
    
    bool iconDrawn = false;
    
    // Try to load icon from SD card if available
    if (g_sdMounted) {
      const char* path = getIconPath(i);
      if (SD_MMC.exists(path)) {
        // Read PNG file into memory buffer and draw scaled
        auto file = SD_MMC.open(path);
        if (file) {
          size_t fileSize = file.size();
          uint8_t* buffer = (uint8_t*)malloc(fileSize);
          if (buffer) {
            size_t bytesRead = file.read(buffer, fileSize);
            if (bytesRead == fileSize) {
              // Draw PNG directly to display, then get dimensions
              // First, determine PNG dimensions by decoding header
              M5Canvas canvas(&M5.Display);
              canvas.createSprite(512, 512); // Max expected size
              
              if (canvas.drawPng(buffer, fileSize, 0, 0)) {
                int srcWidth = canvas.width();
                int srcHeight = canvas.height();
                
                // Center the scaled icon in the grid cell
                int imgX = icon.x + (icon.w - kIconDisplaySize) / 2;
                int imgY = icon.y + (icon.h - kIconDisplaySize) / 2;
                
                // Calculate scale to fit within 200x200
                float scaleX = (float)kIconDisplaySize / srcWidth;
                float scaleY = (float)kIconDisplaySize / srcHeight;
                float scale = min(scaleX, scaleY);
                
                // Calculate actual display size maintaining aspect ratio
                int displayW = srcWidth * scale;
                int displayH = srcHeight * scale;
                
                // Center in the 200x200 area
                int finalX = imgX + (kIconDisplaySize - displayW) / 2;
                int finalY = imgY + (kIconDisplaySize - displayH) / 2;
                
                // Push sprite scaled
                canvas.pushRotateZoom(finalX + displayW / 2, finalY + displayH / 2,
                                     0, scale, scale);
                iconDrawn = true;
              }
              canvas.deleteSprite();
            }
            free(buffer);
          }
          file.close();
        }
      }
    }
    
    // Always draw label below the icon - use smooth Chinese font
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&fonts::efontTW_24);  // Smooth anti-aliased font
    M5.Display.setTextSize(1);  // Keep font smooth without scaling
    M5.Display.setTextDatum(TC_DATUM); // Top center
    int labelY = icon.y + icon.h - 35; // Position label near bottom of cell
    M5.Display.drawString(icon.label, icon.x + icon.w / 2, labelY);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setFont(&fonts::Font0);
    
    // Draw border if icon not loaded
    if (!iconDrawn) {
      M5.Display.drawRect(icon.x, icon.y, icon.w, icon.h, TFT_WHITE);
    }
  }
}

void loadPhotoList() {
  g_photoCount = 0;
  if (!g_sdMounted) return;
  
  File dir = SD_MMC.open("/M5Stack-Tab-5-Adventure/photo-frame");
  if (!dir || !dir.isDirectory()) return;
  
  File file = dir.openNextFile();
  while (file && g_photoCount < 100) {
    String name = file.name();
    name.toLowerCase();
    if (!file.isDirectory() && (name.endsWith(".jpg") || name.endsWith(".png") || name.endsWith(".jpeg"))) {
      g_photoFiles[g_photoCount++] = String(file.path());
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
}

void drawPhotoFrame() {
  if (g_photoCount == 0) {
    M5.Display.clear(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&fonts::efontTW_16);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("No photos found in", M5.Display.width() / 2, M5.Display.height() / 2 - 20);
    M5.Display.drawString("/M5Stack-Tab-5-Adventure/photo-frame", M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.setTextDatum(TL_DATUM);
    return;
  }
  
  // Draw new photo if first time, timer elapsed, or manual navigation
  unsigned long now = millis();
  bool shouldChange = (g_currentPhotoIndex < 0) || 
                      (now - g_lastPhotoChange >= kPhotoInterval) ||
                      g_forcePhotoRedraw;
  
  if (shouldChange) {
    g_forcePhotoRedraw = false;
    
    // Random photo only for auto-advance
    if (g_currentPhotoIndex < 0 || (now - g_lastPhotoChange >= kPhotoInterval)) {
      g_currentPhotoIndex = random(g_photoCount);
    }
    g_lastPhotoChange = now;
    
    // Clear screen
    M5.Display.clear(TFT_BLACK);
    
    // Load and display the pre-sized image (1200 width)
    const char* photoPath = g_photoFiles[g_currentPhotoIndex].c_str();
    File file = SD_MMC.open(photoPath);
    if (file) {
      size_t fileSize = file.size();
      uint8_t* buffer = (uint8_t*)malloc(fileSize);
      if (buffer) {
        size_t bytesRead = file.read(buffer, fileSize);
        if (bytesRead == fileSize) {
          bool isPng = String(photoPath).endsWith(".png");
          
          // Use canvas for instant display without top-to-bottom effect
          M5Canvas canvas(&M5.Display);
          canvas.createSprite(1200, 675); // Full image size
          
          // Draw to canvas first
          if (isPng) {
            canvas.drawPng(buffer, fileSize, 0, 0);
          } else {
            canvas.drawJpg(buffer, fileSize, 0, 0);
          }
          
          // Push canvas to display all at once (centered)
          int x = (M5.Display.width() - 1200) / 2;
          canvas.pushSprite(x, 0);
          canvas.deleteSprite();
        }
        free(buffer);
      }
      file.close();
    }
  }
}

void drawAppScreen(int index) {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 20);
  M5.Display.print("App ");
  M5.Display.println(index + 1);

  M5.Display.setTextSize(1);
  M5.Display.setCursor(20, 60);
  M5.Display.println("Tap anywhere to return");
  M5.Display.drawRect(10, 10, 80, 30, TFT_WHITE);
  M5.Display.setCursor(18, 18);
  M5.Display.println("Back");
}

bool hitTest(const Icon& icon, int x, int y) {
  return x >= icon.x && x <= (icon.x + icon.w) && y >= icon.y && y <= (icon.y + icon.h);
}
}

void setup() {
  // put your setup code here, to run once:
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kRotationLandscape);
  M5.Display.setBrightness(kBrightness);
  
  // Initialize SD card with M5Stack Tab 5 pins (SD_MMC)
  SD_MMC.setPins(43, 44, 39, 40, 41, 42); // CLK, CMD, D0, D1, D2, D3
  g_sdMounted = SD_MMC.begin("/sdcard", true); // One bit mode
  
  // Load custom fonts from SD card
  loadCustomFonts();
  
  // Initialize random seed
  randomSeed(esp_random());
  
  g_screen = Screen::Welcome;
  g_needsRedraw = true;
}

void loop() {
  // put your main code here, to run repeatedly:
  M5.update();

  if (g_needsRedraw) {
    g_needsRedraw = false;
    switch (g_screen) {
      case Screen::Welcome:
        drawWelcome();
        break;
      case Screen::Dashboard:
        drawDashboard();
        break;
      case Screen::App1:
        drawCalendar();
        break;
      case Screen::App2:
        drawTodoList();
        break;
      case Screen::App3:
        drawPhotoFrame();
        break;
      case Screen::App4:
        drawAppScreen(3);
        break;
      case Screen::App5:
        drawAppScreen(4);
        break;
      case Screen::App6:
        drawAppScreen(5);
        break;
      case Screen::App7:
        drawAppScreen(6);
        break;
      case Screen::App8:
        drawAppScreen(7);
        break;
    }
  }
  
  // Keep updating photo frame for slideshow
  if (g_screen == Screen::App3) {
    drawPhotoFrame();
  }

  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    int tx = t.x;
    int ty = t.y;

    if (g_screen == Screen::Welcome) {
      g_screen = Screen::Dashboard;
      g_needsRedraw = true;
      return;
    }

    if (g_screen == Screen::Dashboard) {
      for (int i = 0; i < 8; ++i) {
        if (hitTest(g_icons[i], tx, ty)) {
          if (i == 7) {
            g_screen = Screen::Welcome;
          } else {
            g_screen = static_cast<Screen>(static_cast<int>(Screen::App1) + i);
            // Load calendar events when entering calendar
            if (i == 0) {
              loadCalendarEvents();
            }
            // Load tasks when entering todo
            if (i == 1) {
              loadTodoTasks();
              g_taskScrollOffset = 0;
            }
            // Load photo list when entering photo frame
            if (i == 2) {
              loadPhotoList();
              g_currentPhotoIndex = -1;
            }
          }
          g_needsRedraw = true;
          return;
        }
      }
      return;
    }

    // Calendar navigation
    if (g_screen == Screen::App1) {
      // Top-left corner (100x100 area) = back to dashboard
      if (tx < 100 && ty < 100) {
        g_screen = Screen::Dashboard;
        g_needsRedraw = true;
        return;
      }
      
      // Left half = previous week
      if (tx < M5.Display.width() / 2) {
        g_weekOffset--;
        g_needsRedraw = true;
        return;
      }
      
      // Right half = next week
      if (tx >= M5.Display.width() / 2) {
        g_weekOffset++;
        g_needsRedraw = true;
        return;
      }
      return;
    }

    // Todo list navigation
    if (g_screen == Screen::App2) {
      // Top-left corner (100x100 area) = back to dashboard
      if (tx < 100 && ty < 100) {
        g_screen = Screen::Dashboard;
        g_needsRedraw = true;
        return;
      }
      
      int visibleTasks = (M5.Display.height() - 80) / 40;
      
      // Left half = scroll up
      if (tx < M5.Display.width() / 2) {
        if (g_taskScrollOffset > 0) {
          g_taskScrollOffset--;
          g_needsRedraw = true;
        }
        return;
      }
      
      // Right half = scroll down
      if (tx >= M5.Display.width() / 2) {
        if (g_taskScrollOffset + visibleTasks < g_taskCount) {
          g_taskScrollOffset++;
          g_needsRedraw = true;
        }
        return;
      }
      return;
    }

    // Photo frame navigation
    if (g_screen == Screen::App3) {
      // Top-left corner (100x100 area) = back to dashboard
      if (tx < 100 && ty < 100) {
        g_screen = Screen::Dashboard;
        g_needsRedraw = true;
        return;
      }
      
      // Left half = previous photo
      if (tx < M5.Display.width() / 2) {
        if (g_photoCount > 0) {
          g_currentPhotoIndex--;
          if (g_currentPhotoIndex < 0) {
            g_currentPhotoIndex = g_photoCount - 1;
          }
          g_lastPhotoChange = millis(); // Reset timer
          g_forcePhotoRedraw = true;
        }
        return;
      }
      
      // Right half = next photo
      if (tx >= M5.Display.width() / 2) {
        if (g_photoCount > 0) {
          g_currentPhotoIndex++;
          if (g_currentPhotoIndex >= g_photoCount) {
            g_currentPhotoIndex = 0;
          }
          g_lastPhotoChange = millis(); // Reset timer
          g_forcePhotoRedraw = true;
        }
        return;
      }
      return;
    }

    // Other apps: tap anywhere to go back to dashboard
    if (g_screen != Screen::Welcome && g_screen != Screen::Dashboard) {
      g_screen = Screen::Dashboard;
      g_needsRedraw = true;
      return;
    }
  }
}
