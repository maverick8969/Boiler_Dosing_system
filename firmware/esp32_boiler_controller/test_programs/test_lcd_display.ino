/**
 * @file test_lcd_display.ino
 * @brief Test program for 20x4 I2C LCD display
 *
 * Tests:
 * - I2C communication
 * - Display contrast/backlight
 * - Character display
 * - Custom characters
 * - Screen layouts
 *
 * Hardware:
 * - 20x4 LCD with I2C backpack (PCF8574)
 * - Common addresses: 0x27 or 0x3F
 *
 * Usage:
 * - Upload to ESP32
 * - Open Serial Monitor at 115200 baud
 * - Follow menu prompts
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_FREQ            100000

// ============================================================================
// LCD CONFIGURATION
// ============================================================================

#define LCD_ADDR            0x27    // Try 0x3F if 0x27 doesn't work
#define LCD_COLS            20
#define LCD_ROWS            4

// ============================================================================
// CUSTOM CHARACTERS
// ============================================================================

// Thermometer icon
byte thermometerChar[] = {
    B00100,
    B01010,
    B01010,
    B01010,
    B01110,
    B11111,
    B11111,
    B01110
};

// Drop icon (for water)
byte dropChar[] = {
    B00100,
    B00100,
    B01010,
    B01010,
    B10001,
    B10001,
    B10001,
    B01110
};

// Pump icon
byte pumpChar[] = {
    B11111,
    B10001,
    B10001,
    B11111,
    B00100,
    B00100,
    B01110,
    B01110
};

// Warning icon
byte warningChar[] = {
    B00100,
    B00100,
    B01110,
    B01110,
    B11111,
    B11111,
    B00100,
    B00100
};

// Progress bar characters
byte progressEmpty[] = {
    B11111,
    B10001,
    B10001,
    B10001,
    B10001,
    B10001,
    B10001,
    B11111
};

byte progressFull[] = {
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111
};

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  LCD DISPLAY TEST PROGRAM");
    Serial.println("========================================");
    Serial.println();

    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ);

    Serial.println("Scanning I2C bus...");
    scanI2C();

    // Initialize LCD
    Serial.println();
    Serial.printf("Initializing LCD at address 0x%02X...\n", LCD_ADDR);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    // Create custom characters
    lcd.createChar(0, thermometerChar);
    lcd.createChar(1, dropChar);
    lcd.createChar(2, pumpChar);
    lcd.createChar(3, warningChar);
    lcd.createChar(4, progressEmpty);
    lcd.createChar(5, progressFull);

    // Show startup message
    lcd.setCursor(0, 0);
    lcd.print("   LCD Test Ready   ");
    lcd.setCursor(0, 1);
    lcd.print("  Boiler Controller ");
    lcd.setCursor(0, 2);
    lcd.print("                    ");
    lcd.setCursor(0, 3);
    lcd.print(" Check Serial Menu  ");

    Serial.println("LCD initialized!");
    Serial.println();

    printMenu();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        processCommand(cmd);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void processCommand(char cmd) {
    switch (cmd) {
        case 'c':
            testClear();
            break;

        case 'b':
            toggleBacklight();
            break;

        case 't':
            testAllCharacters();
            break;

        case 'u':
            testCustomCharacters();
            break;

        case 'p':
            testProgressBar();
            break;

        case 'm':
            testMainScreen();
            break;

        case 'a':
            testAlarmScreen();
            break;

        case 's':
            testScrolling();
            break;

        case 'r':
            testRandomData();
            break;

        case 'l':
            testLineByLine();
            break;

        case 'i':
            scanI2C();
            break;

        case 'h':
        case '?':
            printMenu();
            break;

        case '\n':
        case '\r':
            break;

        default:
            Serial.printf("Unknown command: '%c'\n", cmd);
            break;
    }
}

// ============================================================================
// TEST FUNCTIONS
// ============================================================================

void testClear() {
    Serial.println("Clearing display...");
    lcd.clear();
    Serial.println("Display cleared");
}

void toggleBacklight() {
    static bool backlightOn = true;
    backlightOn = !backlightOn;

    if (backlightOn) {
        lcd.backlight();
        Serial.println("Backlight ON");
    } else {
        lcd.noBacklight();
        Serial.println("Backlight OFF");
    }
}

void testAllCharacters() {
    Serial.println("Testing all ASCII characters...");
    lcd.clear();

    int charCode = 32;  // Start with space
    for (int row = 0; row < LCD_ROWS; row++) {
        lcd.setCursor(0, row);
        for (int col = 0; col < LCD_COLS && charCode < 128; col++) {
            lcd.write(charCode++);
        }
    }

    Serial.println("Displaying characters 32-127");
    Serial.println("Press any key to continue...");
    while (!Serial.available()) delay(10);
    Serial.read();

    // Show next set
    lcd.clear();
    charCode = 128;
    for (int row = 0; row < LCD_ROWS; row++) {
        lcd.setCursor(0, row);
        for (int col = 0; col < LCD_COLS && charCode < 256; col++) {
            lcd.write(charCode++);
        }
    }

    Serial.println("Displaying characters 128-255");
}

void testCustomCharacters() {
    Serial.println("Testing custom characters...");
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Custom Characters:");

    lcd.setCursor(0, 1);
    lcd.write(0);  // Thermometer
    lcd.print(" Thermometer");

    lcd.setCursor(0, 2);
    lcd.write(1);  // Drop
    lcd.print(" Water Drop");

    lcd.setCursor(0, 3);
    lcd.write(2);  // Pump
    lcd.print(" Pump ");
    lcd.write(3);  // Warning
    lcd.print(" Warning");

    Serial.println("Displaying custom characters");
}

void testProgressBar() {
    Serial.println("Testing progress bar...");
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Progress Bar Test:");

    for (int percent = 0; percent <= 100; percent += 5) {
        lcd.setCursor(0, 2);
        lcd.print("Progress: ");
        lcd.print(percent);
        lcd.print("%  ");

        // Draw progress bar on line 3
        int filled = (percent * LCD_COLS) / 100;
        lcd.setCursor(0, 3);
        for (int i = 0; i < LCD_COLS; i++) {
            if (i < filled) {
                lcd.write(5);  // Full block
            } else {
                lcd.write(4);  // Empty block
            }
        }

        delay(100);
    }

    Serial.println("Progress bar test complete");
}

void testMainScreen() {
    Serial.println("Testing main screen layout...");
    lcd.clear();

    // Line 0: Status
    lcd.setCursor(0, 0);
    lcd.print("BOILER CTRL  [AUTO]");

    // Line 1: Readings
    lcd.setCursor(0, 1);
    lcd.write(0);  // Thermometer
    lcd.print("42.3C  ");
    lcd.write(1);  // Drop
    lcd.print("2847 uS/cm");

    // Line 2: Pump status
    lcd.setCursor(0, 2);
    lcd.print("H2SO3:-- NaOH:RUN ");

    // Line 3: Water meter
    lcd.setCursor(0, 3);
    lcd.print("WM: 1,247gal 1.2GPM");

    Serial.println("Main screen displayed");
    Serial.println();

    // Simulate updates
    Serial.println("Simulating value updates for 10 seconds...");

    for (int i = 0; i < 20; i++) {
        // Update temperature
        float temp = 42.0 + random(0, 20) / 10.0;
        lcd.setCursor(1, 1);
        lcd.printf("%.1fC", temp);

        // Update conductivity
        int cond = 2800 + random(-100, 100);
        lcd.setCursor(10, 1);
        lcd.printf("%4d", cond);

        delay(500);
    }

    Serial.println("Update simulation complete");
}

void testAlarmScreen() {
    Serial.println("Testing alarm screen...");
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("*** ALARM ***");

    lcd.setCursor(0, 1);
    lcd.write(3);  // Warning
    lcd.print(" HIGH CONDUCTIVITY");

    lcd.setCursor(0, 2);
    lcd.print("Value: 3500 uS/cm");

    lcd.setCursor(0, 3);
    lcd.print("Press ENTER to ACK");

    // Blink the display
    Serial.println("Blinking alarm for 5 seconds...");
    for (int i = 0; i < 10; i++) {
        lcd.noBacklight();
        delay(250);
        lcd.backlight();
        delay(250);
    }

    Serial.println("Alarm screen test complete");
}

void testScrolling() {
    Serial.println("Testing scrolling text...");
    lcd.clear();

    String longText = "This is a long scrolling message for the boiler controller display test.";

    lcd.setCursor(0, 0);
    lcd.print("Scrolling Text:");

    for (int i = 0; i < longText.length() - LCD_COLS + 1; i++) {
        lcd.setCursor(0, 2);
        lcd.print(longText.substring(i, i + LCD_COLS));
        delay(200);
    }

    Serial.println("Scroll test complete");
}

void testRandomData() {
    Serial.println("Displaying random data for 30 seconds...");
    lcd.clear();

    uint32_t startTime = millis();

    while (millis() - startTime < 30000) {
        float temp = 35.0 + random(0, 200) / 10.0;
        int cond = 2000 + random(0, 2000);
        int wm = random(1000, 9999);
        float flow = random(0, 30) / 10.0;

        lcd.setCursor(0, 0);
        lcd.printf("Temp: %.1f C        ", temp);

        lcd.setCursor(0, 1);
        lcd.printf("Cond: %d uS/cm   ", cond);

        lcd.setCursor(0, 2);
        lcd.printf("Water: %d gal    ", wm);

        lcd.setCursor(0, 3);
        lcd.printf("Flow: %.1f GPM      ", flow);

        delay(500);
    }

    Serial.println("Random data test complete");
}

void testLineByLine() {
    Serial.println("Testing line by line...");

    for (int line = 0; line < LCD_ROWS; line++) {
        lcd.clear();
        lcd.setCursor(0, line);
        lcd.printf("Line %d (row %d)", line + 1, line);

        Serial.printf("Showing line %d\n", line);
        delay(1000);
    }

    // Test cursor positions
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Cursor Test:");

    for (int col = 0; col < LCD_COLS; col++) {
        lcd.setCursor(col, 2);
        lcd.print("X");
        delay(100);
        lcd.setCursor(col, 2);
        lcd.print(" ");
    }

    Serial.println("Line test complete");
}

void scanI2C() {
    Serial.println("Scanning I2C bus...");
    Serial.println();

    int found = 0;
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        int error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x27 || addr == 0x3F) {
                Serial.print(" (likely LCD)");
            }
            Serial.println();
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found!");
        Serial.println("  Check wiring and pull-up resistors");
    } else {
        Serial.printf("\nFound %d device(s)\n", found);
    }
}

// ============================================================================
// HELP
// ============================================================================

void printMenu() {
    Serial.println();
    Serial.println("=== LCD DISPLAY TEST MENU ===");
    Serial.println();
    Serial.println("Basic:");
    Serial.println("  c - Clear display");
    Serial.println("  b - Toggle backlight");
    Serial.println();
    Serial.println("Character Tests:");
    Serial.println("  t - Test all ASCII characters");
    Serial.println("  u - Test custom characters");
    Serial.println();
    Serial.println("Screen Tests:");
    Serial.println("  m - Main screen layout");
    Serial.println("  a - Alarm screen (with blink)");
    Serial.println("  p - Progress bar animation");
    Serial.println("  s - Scrolling text");
    Serial.println("  r - Random data (30 sec)");
    Serial.println("  l - Line by line test");
    Serial.println();
    Serial.println("Diagnostics:");
    Serial.println("  i - Scan I2C bus");
    Serial.println();
    Serial.println("Other:");
    Serial.println("  h - Show this menu");
    Serial.println();
}
