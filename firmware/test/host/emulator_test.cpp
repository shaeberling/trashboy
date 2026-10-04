// The emulator core on the build machine: the Model III ROM on the firmware's
// Z80, memory map and keyboard, driven by keyboard reports like the ones a
// Bluetooth keyboard sends, and checked by what is on the TRS-80's screen.

#include "host.h"

#include "trs.h"
#include "trs-keyboard.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define SCREEN_START 0x3c00
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 16

// HID usage IDs
#define HID_A 0x04
#define HID_1 0x1e
#define HID_0 0x27
#define HID_ENTER 0x28
#define HID_SPACE 0x2c
#define HID_MINUS 0x2d
#define HID_EQUALS 0x2e
#define HID_SEMICOLON 0x33
#define HID_QUOTE 0x34
#define HID_COMMA 0x36
#define HID_PERIOD 0x37
#define HID_SLASH 0x38

#define MODIFIER_LEFT_SHIFT 0x02

static int failures = 0;

//----------------------------------------------------------------
// The machine

// Emulated time: trs.cpp paces the Z80 against the clock, and on the host
// the clock only advances by what the Z80 has to wait.
static double now()
{
  return esp_timer_get_time() / 1000000.0;
}

static void run(double seconds)
{
  const double end = now() + seconds;
  while (now() < end) {
    for (int i = 0; i < 1000; i++) {
      z80_run();
    }
  }
}

static char screen_char(int pos)
{
  const uint8_t ch = peek_mem(SCREEN_START + pos);
  return (ch >= 0x20 && ch < 0x7f) ? (char) ch : (ch == 0 ? ' ' : '#');
}

// The lines of the screen, each ending in a newline
static std::string screen()
{
  std::string text;
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    std::string line;
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      line += screen_char(y * SCREEN_WIDTH + x);
    }
    line.erase(line.find_last_not_of(' ') + 1);
    text += line + "\n";
  }
  return text;
}

static bool on_screen(const char* text)
{
  return screen().find(text) != std::string::npos;
}

// Runs until the text is on the screen. False if it is not there after
// that many seconds.
static bool wait_for(const char* text, double seconds = 5)
{
  const double end = now() + seconds;
  while (now() < end) {
    if (on_screen(text)) {
      return true;
    }
    run(0.02);
  }
  return on_screen(text);
}

//----------------------------------------------------------------
// The keyboard

static void report(uint8_t modifiers, uint8_t key)
{
  BTKeyboard::KeyInfo inf;
  memset(&inf, 0, sizeof(inf));
  inf.size = 8;
  inf.keys[0] = modifiers;
  inf.keys[2] = key;
  inf.modifier = (BTKeyboard::KeyModifier) modifiers;
  process_key(inf);
}

// Long enough for the ROM's keyboard scan to see the key, and to see it
// released again
#define KEY_SECONDS 0.06

static void press(uint8_t key, bool shift = false)
{
  if (shift) {
    report(MODIFIER_LEFT_SHIFT, 0);
    run(KEY_SECONDS);
  }
  report(shift ? MODIFIER_LEFT_SHIFT : 0, key);
  run(KEY_SECONDS);
  report(shift ? MODIFIER_LEFT_SHIFT : 0, 0);
  run(KEY_SECONDS);
  if (shift) {
    report(0, 0);
    run(KEY_SECONDS);
  }
}

// Types the text on a US keyboard
static void type(const char* text)
{
  for (const char* p = text; *p != '\0'; p++) {
    const char c = *p;
    if (c >= 'a' && c <= 'z') {
      press(HID_A + (c - 'a'));
    } else if (c >= 'A' && c <= 'Z') {
      // The Model III starts in capitals: unshifted letters are capitals
      press(HID_A + (c - 'A'));
    } else if (c >= '1' && c <= '9') {
      press(HID_1 + (c - '1'));
    } else {
      switch (c) {
      case '0': press(HID_0); break;
      case '\n': press(HID_ENTER); break;
      case ' ': press(HID_SPACE); break;
      case '-': press(HID_MINUS); break;
      case '=': press(HID_EQUALS); break;
      case '+': press(HID_EQUALS, true); break;
      case ';': press(HID_SEMICOLON); break;
      case ':': press(HID_SEMICOLON, true); break;
      case '"': press(HID_QUOTE, true); break;
      case ',': press(HID_COMMA); break;
      case '.': press(HID_PERIOD); break;
      case '/': press(HID_SLASH); break;
      case '?': press(HID_SLASH, true); break;
      case '!': press(HID_1, true); break;
      case '$': press(HID_1 + 3, true); break;
      case '*': press(HID_1 + 7, true); break;
      case '(': press(HID_1 + 8, true); break;
      case ')': press(HID_0, true); break;
      default:
        fprintf(stderr, "type(): no key for '%c'\n", c);
        exit(2);
      }
    }
  }
}

//----------------------------------------------------------------
// Checking

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      failures++; \
      printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
  } while (0)

#define CHECK_SCREEN(text) \
  do { \
    if (!wait_for(text)) { \
      failures++; \
      printf("  FAILED %s:%d: \"%s\" is not on the screen:\n%s", __FILE__, __LINE__, text, screen().c_str()); \
    } \
  } while (0)

static void test(const char* name)
{
  printf("%s\n", name);
}

// Power on, and through the ROM's two questions into BASIC
static void boot_basic()
{
  host_clear_screen();
  z80_reset(0);
  CHECK_SCREEN("Cass?");
  press(HID_ENTER);
  CHECK_SCREEN("Memory Size?");
  press(HID_ENTER);
  CHECK_SCREEN("READY");
}

//----------------------------------------------------------------

static void test_boot()
{
  test("The ROM boots into BASIC");
  boot_basic();
  CHECK_SCREEN("Model III Basic");
}

static void test_basic()
{
  test("BASIC computes and prints");
  boot_basic();
  type("PRINT 6*7\n");
  CHECK_SCREEN(" 42");

  type("10 FOR I=1 TO 3\n20 PRINT I*I;\n30 NEXT\nRUN\n");
  CHECK_SCREEN(" 1  4  9");
}

static void test_keyboard()
{
  test("Shifted keys of a US keyboard arrive as the TRS-80's");
  boot_basic();
  // Quotes are SHIFT-2 on the TRS-80, the asterisk SHIFT-colon, ...
  type("PRINT \"A+B=(C*D)/2, $1: OK!\"\n");
  // Once in the command, once printed
  CHECK_SCREEN("\nA+B=(C*D)/2, $1: OK!\n");

  test("A read of several keyboard rows sees the keys of all of them");
  report(0, HID_A);
  CHECK((peek_mem(0x3801) & 0x02) != 0);  // row 0: @ A B C D E F G
  CHECK(peek_mem(0x3840) == 0);           // row 6: ENTER CLEAR BREAK arrows SPACE
  CHECK((peek_mem(0x38ff) & 0x02) != 0);
  report(0, HID_ENTER);
  CHECK(peek_mem(0x3801) == 0);
  CHECK((peek_mem(0x3840) & 0x01) != 0);
  CHECK((peek_mem(0x38ff) & 0x01) != 0);
  report(0, 0);
  CHECK(peek_mem(0x38ff) == 0);
}

// The seconds of the ROM's clock, as BASIC prints them after the label
static int clock_seconds(const char* label)
{
  const std::string marker = std::string("\n") + label + "=";
  type((std::string("PRINT \"") + label + "=\";RIGHT$(TIME$,2)\n").c_str());
  if (!wait_for(marker.c_str())) {
    return -1;
  }
  run(0.2);
  const std::string text = screen();
  return atoi(text.substr(text.find(marker) + marker.size(), 2).c_str());
}

static void test_timer()
{
  test("The timer interrupt runs the ROM's clock");
  boot_basic();
  const int before = clock_seconds("T");
  run(5);
  const int after = clock_seconds("U");
  CHECK(before >= 0 && after >= 0);
  // 5 seconds, and the time it takes to type the second command
  const int elapsed = (after - before + 60) % 60;
  CHECK(elapsed >= 5 && elapsed <= 20);
  if (elapsed < 5 || elapsed > 20) {
    printf("  the clock went from %d to %d seconds\n", before, after);
  }
}

// A CMD file: the bytes at an address, then the address to start at
static std::string cmd_block(uint16_t address, const std::string& data)
{
  std::string block;
  block += (char) 0x01;
  // The length counts the two address bytes, and 256 and more wrap around
  block += (char) ((data.size() + 2) & 0xff);
  block += (char) (address & 0xff);
  block += (char) (address >> 8);
  return block + data;
}

static std::string cmd_entry(uint16_t address)
{
  std::string block;
  block += (char) 0x02;
  block += (char) 0x02;
  block += (char) (address & 0xff);
  block += (char) (address >> 8);
  return block;
}

static void test_cmd()
{
  test("A CMD file is loaded and runs");
  boot_basic();

  // LD HL,3C00H; LD (HL),'O'; INC HL; LD (HL),'K'; JR $
  const std::string program("\x21\x00\x3c\x36\x4f\x23\x36\x4b\x18\xfe", 10);
  // Blocks of 254, 255 and 256 bytes have the lengths 0, 1 and 2
  const std::string a(254, 'a'), b(255, 'b'), c(256, 'c');
  const std::string cmd = cmd_block(0x7000, program) +
                          cmd_block(0x8000, a) +
                          cmd_block(0x9000, b) +
                          cmd_block(0xa000, c) +
                          cmd_entry(0x7000);

  const uint16_t entry = trs_load_cmd((const uint8_t*) cmd.data(), cmd.size());
  CHECK(entry == 0x7000);
  CHECK(peek_mem(0x7000) == 0x21 && peek_mem(0x7009) == 0xfe);
  CHECK(peek_mem(0x80fd) == 'a' && peek_mem(0x80fe) == 0);
  CHECK(peek_mem(0x90fe) == 'b' && peek_mem(0x90ff) == 0);
  CHECK(peek_mem(0xa0ff) == 'c' && peek_mem(0xa100) == 0);

  z80_set_pc(entry);
  run(0.1);
  CHECK(screen().rfind("OK", 0) == 0);
}

int main()
{
  host_init();

  test_boot();
  test_basic();
  test_keyboard();
  test_timer();
  test_cmd();

  if (failures != 0) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("All checks passed\n");
  return 0;
}
