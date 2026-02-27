
#include "trs.h"
#include "trs-keyboard.h"

#define ADD_SHIFT_KEY 0x100
#define REMOVE_SHIFT_KEY 0x200

typedef struct {
  uint8_t offset;
  uint16_t mask;
} TRSKey;


/*
Address   1     2     4     8     16     32     64     128    Hex Address
------- ----- ---,- ----- ----- -----  -----  -----   -----   -----------
14337     @     A     B     C      D      E      F      G        3801       1    1
14338     H     I     J     K      L      M      N      O        3802       2    2
14340     P     Q     R     S      T      U      V      W        3804       4    3
14344     X     Y     Z     ,      -      -      -      -        3808       8    4
14352     0     1!    2"    3#     4$     5%     6&     7'       3810      16    5
14368     8(    9)   *:    +;     <,     =-     >.     ?/        3820      32    6
14400  enter  clear break  up    down   left  right  space       3840      64    7
14464  shift    -     -     -  control    -      -      -        3880     128    8
*/

/* Full Model 4 keyboard:
Address   1     2     4     8     16     32     64     128    Hex Address
------- ----- ----- ----- ----- -----  -----  -----   -----   -----------
14337     @     A     B     C      D      E      F      G        3801       1    1
14338     H     I     J     K      L      M      N      O        3802       2    2
14340     P     Q     R     S      T      U      V      W        3804       4    3
14344     X     Y     Z     ,      -      -      -      -        3808       8    4
14352     0     1!    2"    3#     4$     5%     6&     7'       3810      16    5
14368     8(    9)   *:    +;     <,     =-     >.     ?/        3820      32    6
14400  enter  clear break  up    down   left  right  space       3840      64    7
14464  Lshift Rshft contrl caps   F1     F2     F3      -        3880     128    8

Additionally:
Shift-0 is now F4
Clear is on home,\
config is on F5
screenshot on F6
*/

// Virtual key indices for trsKeys[] array
#define VK_SPACE 0
#define VK_0 1
#define VK_1 2
#define VK_2 3
#define VK_3 4
#define VK_4 5
#define VK_5 6
#define VK_6 7
#define VK_7 8
#define VK_8 9
#define VK_9 10
#define VK_A 11
#define VK_B 12
#define VK_C 13
#define VK_D 14
#define VK_E 15
#define VK_F 16
#define VK_G 17
#define VK_H 18
#define VK_I 19
#define VK_J 20
#define VK_K 21
#define VK_L 22
#define VK_M 23
#define VK_N 24
#define VK_O 25
#define VK_P 26
#define VK_Q 27
#define VK_R 28
#define VK_S 29
#define VK_T 30
#define VK_U 31
#define VK_V 32
#define VK_W 33
#define VK_X 34
#define VK_Y 35
#define VK_Z 36
#define VK_QUOTE 37
#define VK_QUOTEDBL 38
#define VK_EQUALS 39
#define VK_MINUS 40
#define VK_KP_MINUS 41
#define VK_PLUS 42
#define VK_KP_PLUS 43
#define VK_KP_MULTIPLY 44
#define VK_ASTERISK 45
#define VK_BACKSLASH 46
#define VK_KP_DIVIDE 47
#define VK_SLASH 48
#define VK_KP_PERIOD 49
#define VK_PERIOD 50
#define VK_COLON 51
#define VK_COMMA 52
#define VK_SEMICOLON 53
#define VK_AMPERSAND 54
#define VK_HASH 55
#define VK_AT 56
#define VK_DOLLAR 57
#define VK_POUND 58
#define VK_PERCENT 59
#define VK_EXCLAIM 60
#define VK_QUESTION 61
#define VK_LEFTPAREN 62
#define VK_RIGHTPAREN 63
#define VK_LESS 64
#define VK_GREATER 65
#define VK_LSHIFT 66
#define VK_RSHIFT 67
#define VK_LCTRL 68
#define VK_RCTRL 69
#define VK_ESCAPE 70
#define VK_BACKSPACE 71
#define VK_HOME 72
#define VK_BREAK 73
#define VK_CAPSLOCK 74
#define VK_RETURN 75
#define VK_UP 76
#define VK_KP_UP 77
#define VK_DOWN 78
#define VK_LEFT 79
#define VK_RIGHT 80
#define VK_F1 81
#define VK_F2 82
#define VK_F3 83
#define VK_F4 84

static const TRSKey trsKeys[] = {
  /*  0 */ {7, 128}, // VK_SPACE
  /*  1 */ {5, 1}, //  VK_0
  /*  2 */ {5, 2}, //  VK_1
  /*  3 */ {5, 4}, //  VK_2
  /*  4 */ {5, 8}, //  VK_3
  /*  5 */ {5, 16}, //  VK_4
  /*  6 */ {5, 32}, //  VK_5
  /*  7 */ {5, 64}, //  VK_6
  /*  8 */ {5, 128}, //  VK_7
  /*  9 */ {6, 1}, //  VK_8
  /* 10 */ {6, 2}, //  VK_9
  /* 11 */ {1, 2}, //  VK_A
  /* 12 */ {1, 4}, //  VK_B
  /* 13 */ {1, 8}, //  VK_C
  /* 14 */ {1, 16}, //  VK_D
  /* 15 */ {1, 32}, //  VK_E
  /* 16 */ {1, 64}, //  VK_F
  /* 17 */ {1, 128}, //  VK_G
  /* 18 */ {2, 1}, //  VK_H
  /* 19 */ {2, 2}, //  VK_I
  /* 20 */ {2, 4}, //  VK_J
  /* 21 */ {2, 8}, //  VK_K
  /* 22 */ {2, 16}, //  VK_L
  /* 23 */ {2, 32}, //  VK_M
  /* 24 */ {2, 64}, //  VK_N
  /* 25 */ {2, 128}, //  VK_O
  /* 26 */ {3, 1}, //  VK_P
  /* 27 */ {3, 2}, //  VK_Q
  /* 28 */ {3, 4}, //  VK_R
  /* 29 */ {3, 8}, //  VK_S
  /* 30 */ {3, 16}, //  VK_T
  /* 31 */ {3, 32}, //  VK_U
  /* 32 */ {3, 64}, //  VK_V
  /* 33 */ {3, 128}, //  VK_W
  /* 34 */ {4, 1}, //  VK_X
  /* 35 */ {4, 2}, //  VK_Y
  /* 36 */ {4, 4}, //  VK_Z
  /* 37 */ {5, ADD_SHIFT_KEY | 128}, //  VK_QUOTE
  /* 38 */ {5, 4}, //  VK_QUOTEDBL
  /* 39 */ {6, ADD_SHIFT_KEY | 32}, //  VK_EQUALS
  /* 40 */ {6, 32}, //  VK_MINUS
  /* 41 */ {6, 32}, //  VK_KP_MINUS
  /* 42 */ {6, 8}, //  VK_PLUS
  /* 43 */ {6, 8}, //  VK_KP_PLUS
  /* 44 */ {6, 4}, //  VK_KP_MULTIPLY
  /* 45 */ {6, 4}, //  VK_ASTERISK
  /* 46 */ {7, 2}, //  VK_BACKSLASH
  /* 47 */ {6, 128}, //  VK_KP_DIVIDE
  /* 48 */ {6, 128}, //  VK_SLASH
  /* 49 */ {6, 64}, //  VK_KP_PERIOD
  /* 50 */ {6, 64}, //  VK_PERIOD
  /* 51 */ {6, REMOVE_SHIFT_KEY | 4}, //  VK_COLON
  /* 52 */ {6, 16}, //  VK_COMMA
  /* 53 */ {6, 8}, //  VK_SEMICOLON
  /* 54 */ {5, 64}, //  VK_AMPERSAND
  /* 55 */ {5, 8}, //  VK_HASH
  /* 56 */ {1, REMOVE_SHIFT_KEY | 1}, //  VK_AT
  /* 57 */ {5, 16}, //  VK_DOLLAR
  /* 58 */ {5, 8}, //  VK_POUND
  /* 59 */ {5, 32}, //  VK_PERCENT
  /* 60 */ {5, 2}, //  VK_EXCLAIM
  /* 61 */ {6, 128}, //  VK_QUESTION
  /* 62 */ {6, 1}, //  VK_LEFTPAREN
  /* 63 */ {6, 2}, //  VK_RIGHTPAREN
  /* 64 */ {6, 16}, //  VK_LESS
  /* 65 */ {6, 64}, //  VK_GREATER
  /* 66 */ {8, 1}, //  VK_LSHIFT
  /* 67 */ {8, 2}, //  VK_RSHIFT
  /* 68 */ {8, 4}, //  VK_LCTRL
  /* 69 */ {8, 4}, //  VK_RCTRL
  /* 70 */ {7, 4}, //  VK_ESCAPE
  /* 71 */ {7, 32}, //  VK_BACKSPACE
  /* 72 */ {7, 2}, //  VK_HOME
  /* 73 */ {7, 4}, //  VK_BREAK
  /* 74 */ {8, 8}, //  VK_CAPSLOCK
  /* 75 */ {7, 1}, //  VK_RETURN
  /* 76 */ {7, 8}, //  VK_UP
  /* 77 */ {7, 8}, //  VK_KP_UP
  /* 78 */ {7, 16}, //  VK_DOWN
  /* 79 */ {7, 32}, //  VK_LEFT
  /* 80 */ {7, 64}, //  VK_RIGHT
  /* 81 */ {8, 16}, //  VK_F1
  /* 82 */ {8, 32}, //  VK_F2
  /* 83 */ {8, 64}, //  VK_F3
  /* 84 */ {5, ADD_SHIFT_KEY | 1} //  VK_F4
};

static const uint8_t hidToVK[] = {
  0xff, // 0x00
  0xff, // 0x01
  0xff, // 0x02 (reserved)
  0xff, // 0x03 (reserved)
  VK_A, // 0x04 - A
  VK_B, // 0x05 - B
  VK_C, // 0x06 - C
  VK_D, // 0x07 - D
  VK_E, // 0x08 - E
  VK_F, // 0x09 - F
  VK_G, // 0x0A - G
  VK_H, // 0x0B - H
  VK_I, // 0x0C - I
  VK_J, // 0x0D - J
  VK_K, // 0x0E - K
  VK_L, // 0x0F - L
  VK_M, // 0x10 - M
  VK_N, // 0x11 - N
  VK_O, // 0x12 - O
  VK_P, // 0x13 - P
  VK_Q, // 0x14 - Q
  VK_R, // 0x15 - R
  VK_S, // 0x16 - S
  VK_T, // 0x17 - T
  VK_U, // 0x18 - U
  VK_V, // 0x19 - V
  VK_W, // 0x1A - W
  VK_X, // 0x1B - X
  VK_Y, // 0x1C - Y
  VK_Z, // 0x1D - Z
  VK_1, // 0x1E - 1
  VK_2, // 0x1F - 2
  VK_3, // 0x20 - 3
  VK_4, // 0x21 - 4
  VK_5, // 0x22 - 5
  VK_6, // 0x23 - 6
  VK_7, // 0x24 - 7
  VK_8, // 0x25 - 8
  VK_9, // 0x26 - 9
  VK_0, // 0x27 - 0
  VK_RETURN, // 0x28 - Enter
  VK_ESCAPE, // 0x29 - Escape
  VK_BACKSPACE, // 0x2A - Backspace
  0xff, // 0x2B - Tab
  VK_SPACE, // 0x2C - Space
  VK_MINUS, // 0x2D - Minus
  VK_EQUALS, // 0x2E - Equals (=)
  0xff, // 0x2F - Left bracket [
  0xff, // 0x30 - Right bracket ]
  VK_BACKSLASH, // 0x31 - Backslash
  0xff, // 0x32 - Non-US # and ~
  VK_SEMICOLON, // 0x33 - Semicolon (;) - shift+semicolon is colon (:) = TRS *: key
  VK_QUOTE, // 0x34 - Quote (') = TRS shift+7 - shift+quote is doublequote (") = TRS shift+2
  0xff, // 0x35 - Grave accent `
  VK_COMMA, // 0x36 - Comma (,) - shift+comma is < = TRS shift+comma
  VK_PERIOD, // 0x37 - Period (.) - shift+period is > = TRS shift+period
  VK_SLASH, // 0x38 - Slash (/) - shift+slash is ? = TRS shift+slash
  VK_CAPSLOCK, // 0x39 - Caps Lock
  VK_F1, // 0x3A - F1
  VK_F2, // 0x3B - F2
  VK_F3, // 0x3C - F3
  VK_F4, // 0x3D - F4 (Shift+0)
  0xff, // 0x3E - F5
  0xff, // 0x3F - F6
  0xff, // 0x40 - F7
  0xff, // 0x41 - F8
  0xff, // 0x42 - F9
  0xff, // 0x43 - F10
  0xff, // 0x44 - F11
  0xff, // 0x45 - F12
  0xff, // 0x46 - Print Screen
  0xff, // 0x47 - Scroll Lock
  0xff, // 0x48 - Pause
  0xff, // 0x49 - Insert
  0xff, // 0x4A - Home
  0xff, // 0x4B - Page Up
  0xff, // 0x4C - Delete
  0xff, // 0x4D - End
  0xff, // 0x4E - Page Down
  VK_RIGHT, // 0x4F - Right Arrow
  VK_LEFT, // 0x50 - Left Arrow
  VK_DOWN, // 0x51 - Down Arrow
  VK_UP, // 0x52 - Up Arrow
  0xff, // 0x53 - Num Lock
  0xff, // 0x54 - Keypad /
  0xff, // 0x55 - Keypad *
  0xff, // 0x56 - Keypad -
  0xff, // 0x57 - Keypad +
  0xff, // 0x58 - Keypad Enter
  0xff, // 0x59 - Keypad 1
  0xff, // 0x5A - Keypad 2
  0xff, // 0x5B - Keypad 3
  0xff, // 0x5C - Keypad 4
  0xff, // 0x5D - Keypad 5
  0xff, // 0x5E - Keypad 6
  0xff, // 0x5F - Keypad 7
  0xff, // 0x60 - Keypad 8
  0xff, // 0x61 - Keypad 9
  0xff, // 0x62 - Keypad 0
  0xff, // 0x63 - Keypad .
  0xff, // 0x64 - Non-US \ and |
  0xff  // 0x65 - Application
};

static const uint8_t shiftedHidToVK[] = {
    0xff, // 0x00
    0xff, // 0x01
    0xff, // 0x02
    0xff, // 0x03
    0xff, // 0x04 - Shift+A
    0xff, // 0x05 - Shift+B
    0xff, // 0x06 - Shift+C
    0xff, // 0x07 - Shift+D
    0xff, // 0x08 - Shift+E
    0xff, // 0x09 - Shift+F
    0xff, // 0x0A - Shift+G
    0xff, // 0x0B - Shift+H
    0xff, // 0x0C - Shift+I
    0xff, // 0x0D - Shift+J
    0xff, // 0x0E - Shift+K
    0xff, // 0x0F - Shift+L
    0xff, // 0x10 - Shift+M
    0xff, // 0x11 - Shift+N
    0xff, // 0x12 - Shift+O
    0xff, // 0x13 - Shift+P
    0xff, // 0x14 - Shift+Q
    0xff, // 0x15 - Shift+R
    0xff, // 0x16 - Shift+S
    0xff, // 0x17 - Shift+T
    0xff, // 0x18 - Shift+U
    0xff, // 0x19 - Shift+V
    0xff, // 0x1A - Shift+W
    0xff, // 0x1B - Shift+X
    0xff, // 0x1C - Shift+Y
    0xff, // 0x1D - Shift+Z
    VK_EXCLAIM, // 0x1E - Shift+1 = !
    VK_AT, // 0x1F - Shift+2 = @
    VK_HASH, // 0x20 - Shift+3 = #
    VK_DOLLAR, // 0x21 - Shift+4 = $
    VK_PERCENT, // 0x22 - Shift+5 = %
    0xff, // 0x23 - Shift+6
    VK_AMPERSAND, // 0x24 - Shift+7 = &
    VK_ASTERISK, // 0x25 - Shift+8 = *
    VK_LEFTPAREN, // 0x26 - Shift+9 = (
    VK_RIGHTPAREN, // 0x27 - Shift+0 = )
    0xff, // 0x28 - Shift+Enter
    0xff, // 0x29 - Shift+Escape
    0xff, // 0x2A - Shift+Backspace
    0xff, // 0x2B - Shift+Tab
    0xff, // 0x2C - Shift+Space
    VK_KP_MINUS, // 0x2D - Shift+Minus = _
    VK_PLUS, // 0x2E - Shift+Equals = +
    0xff, // 0x2F
    0xff, // 0x30
    VK_BACKSLASH, // 0x31 - Shift+Backslash = |
    0xff, // 0x32
    VK_COLON, // 0x33 - Shift+Semicolon = :
    VK_QUOTEDBL, // 0x34 - Shift+Quote = "
    0xff, // 0x35
    VK_LESS, // 0x36 - Shift+Comma = <
    VK_GREATER, // 0x37 - Shift+Period = >
    VK_QUESTION, // 0x38 - Shift+Slash = ?
    0xff, // 0x39 - Shift+Caps Lock
    0xff, // 0x3A - Shift+F1
    0xff, // 0x3B - Shift+F2
    0xff, // 0x3C - Shift+F3
    0xff, // 0x3D - Shift+F4
    0xff, // 0x3E - Shift+F5
    0xff, // 0x3F - Shift+F6
    0xff, // 0x40
    0xff, // 0x41
    0xff, // 0x42
    0xff, // 0x43
    0xff, // 0x44
    0xff, // 0x45
    0xff, // 0x46
    0xff, // 0x47
    0xff, // 0x48
    0xff, // 0x49
    0xff, // 0x4A
    0xff, // 0x4B
    0xff, // 0x4C
    0xff, // 0x4D
    0xff, // 0x4E
    0xff, // 0x4F
    0xff, // 0x50
    0xff, // 0x51
    0xff, // 0x52
    0xff, // 0x53
    0xff, // 0x54
    0xff, // 0x55
    0xff, // 0x56
    0xff, // 0x57
    0xff, // 0x58
    0xff, // 0x59
    0xff, // 0x5A
    0xff, // 0x5B
    0xff, // 0x5C
    0xff, // 0x5D
    0xff, // 0x5E
    0xff, // 0x5F
    0xff, // 0x60
    0xff, // 0x61
    0xff, // 0x62
    0xff, // 0x63
    0xff, // 0x64
    0xff   // 0x65
};

static uint8_t keyb_buffer[8] = {0};

// Mapping for shifted HID keys that differ from TRS-80 layout
// Modern keyboard shift+key produces different characters than TRS-80 shift+key
// This table maps: [HID scan code when shifted] = {TRS offset, TRS mask with flags}
typedef struct {
  uint8_t hid_code;
  TRSKey trs_key;
} ShiftedKeyMap;

static const ShiftedKeyMap shiftedHidToTrs[] = {
  {0x1F, {1, REMOVE_SHIFT_KEY | 1}},    // Shift+2 = @ (modern) → @ key (TRS unshifted)
  {0x24, {5, 64}},                       // Shift+7 = & (modern) → 6& key (TRS shift+6) [keep shift]
  {0x25, {6, REMOVE_SHIFT_KEY | 4}},    // Shift+8 = * (modern) → *: key (TRS unshifted)
  {0x26, {6, REMOVE_SHIFT_KEY | 1}},    // Shift+9 = ( (modern) → 8( key (TRS unshifted)
  {0x27, {6, REMOVE_SHIFT_KEY | 2}},    // Shift+0 = ) (modern) → 9) key (TRS unshifted)
  {0x2E, {6, REMOVE_SHIFT_KEY | 8}},    // Shift+= = + (modern) → +; key (TRS unshifted)
  {0x33, {6, 4}},                        // Shift+; = : (modern) → *: key (TRS shift+*) [keep shift]
  {0x34, {5, 4}},                        // Shift+' = " (modern) → 2" key (TRS shift+2) [keep shift]
};

// Special HID key codes for modifiers (0xE0-0xE7)
static const uint8_t HID_LCTRL = 0xE0;
static const uint8_t HID_LSHIFT = 0xE1;
static const uint8_t HID_LALT = 0xE2;
static const uint8_t HID_LGUI = 0xE3;
static const uint8_t HID_RCTRL = 0xE4;
static const uint8_t HID_RSHIFT = 0xE5;
static const uint8_t HID_RALT = 0xE6;
static const uint8_t HID_RGUI = 0xE7;

int trs_kb_mem_read(int address)
{
  for (int i = 0; i < sizeof(keyb_buffer); i++) {
    if (address & 1) {
      return keyb_buffer[i];
    }
    address >>= 1;
  }
  return 0;
}


static void process_virtual_key(int vk, bool down)
{
  if (trs_model != 4 && vk == VK_RSHIFT) {
    vk = VK_LSHIFT; // Map right shift to left shift for non-Model 4 TRS-80s
  }
  
  static bool shiftPressed = false;
  if (vk == VK_LSHIFT || vk == VK_RSHIFT) {
    shiftPressed = down;
  }
  
  int offset = trsKeys[vk].offset;

  if (offset != 0) {
    bool addShiftKey = trsKeys[vk].mask & ADD_SHIFT_KEY;
    bool removeShiftKey = trsKeys[vk].mask & REMOVE_SHIFT_KEY;
    uint8_t mask = trsKeys[vk].mask & 0xff;
    if (down) {
      keyb_buffer[offset - 1] |= mask;
      if (addShiftKey) {
        keyb_buffer[7] |= 1;
      }
      if (removeShiftKey) {
        keyb_buffer[7] &= ~1;
      }
    } else {
      keyb_buffer[offset - 1] &= ~mask;
      if (addShiftKey && !shiftPressed) {
        keyb_buffer[7] &= ~1;
      }
      if (removeShiftKey && shiftPressed) {
        keyb_buffer[7] |= 1;
      }
    }
  }
}

void process_key(BTKeyboard::KeyInfo& inf)
{
  static uint8_t previousKeys[BTKeyboard::MAX_KEY_DATA_SIZE] = {0};
  static uint8_t previousModifier = 0;
  
  // Current modifier byte
  uint8_t currentModifier = inf.keys[0];

  printf("Modifier: 0x%02x\n", currentModifier);
  
  // Process modifier key changes (Shift and Ctrl only, as mapped in VK)
  const uint8_t modifierBits[] = {0x01, 0x02, 0x10, 0x20}; // Left Ctrl, Left Shift, Right Ctrl, Right Shift
  const uint8_t modifierVKs[] = {VK_LCTRL, VK_LSHIFT, VK_RCTRL, VK_RSHIFT};
  
  for (int i = 0; i < 4; i++) {
    bool wasPressed = (previousModifier & modifierBits[i]) != 0;
    bool isPressed = (currentModifier & modifierBits[i]) != 0;
    
    if (wasPressed != isPressed) {
      process_virtual_key(modifierVKs[i], isPressed);
    }
  }
  
  // Build sets of current and previous keys for comparison
  bool currentKeySet[256] = {0};
  bool previousKeySet[256] = {0};
  
  // Mark all currently pressed keys
  for (int i = 0; i < inf.size; i++) {
    uint8_t code = inf.keys[i];
    if (code > 0 && code < 0xE0) {  // Valid key code, not modifier
      currentKeySet[code] = true;
    }
  }
  
  // Mark all previously pressed keys
  for (int i = 0; i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
    uint8_t code = previousKeys[i];
    if (code > 0 && code < 0xE0) {
      previousKeySet[code] = true;
    }
  }
  
  bool shiftPressed = (currentModifier & 0x22) != 0; // Left or Right shift
  bool prevShiftPressed = (previousModifier & 0x22) != 0;
  
  // Process key releases (keys that were pressed but are no longer)
  for (int code = 1; code < 0xE0; code++) {
    if (previousKeySet[code] && !currentKeySet[code]) {
      // Key was released
      if (code < sizeof(hidToVK)/sizeof(hidToVK[0])) {
        uint8_t vk;
        
        // Use the same shift state as when the key was pressed (prevShiftPressed)
        if (prevShiftPressed && code < sizeof(shiftedHidToVK)/sizeof(shiftedHidToVK[0])) {
          uint8_t shifted_vk = shiftedHidToVK[code];
          if (shifted_vk != 0xff) {
            vk = shifted_vk;
          } else {
            vk = hidToVK[code];
          }
        } else {
          vk = hidToVK[code];
        }
        
        if (vk != 0xff) {
          process_virtual_key(vk, false);
        }
      }
    }
  }
  
  // Process key presses (keys that are pressed now but weren't before)
  for (int code = 1; code < 0xE0; code++) {
    if (currentKeySet[code] && !previousKeySet[code]) {
      // Key was pressed
      if (code < sizeof(hidToVK)/sizeof(hidToVK[0])) {
        uint8_t vk;
        
        // Check if we should use shifted mapping when shift is pressed
        if (shiftPressed && code < sizeof(shiftedHidToVK)/sizeof(shiftedHidToVK[0])) {
          uint8_t shifted_vk = shiftedHidToVK[code];
          if (shifted_vk != 0xff) {
            vk = shifted_vk;
          } else {
            vk = hidToVK[code];
          }
        } else {
          vk = hidToVK[code];
        }
        
        if (vk != 0xff) {
          process_virtual_key(vk, true);
        }
      }
    }
  }
  
  // Save current state for next call
  for (int i = 0; i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
    previousKeys[i] = (i < inf.size) ? inf.keys[i] : 0;
  }
  previousModifier = currentModifier;
}

