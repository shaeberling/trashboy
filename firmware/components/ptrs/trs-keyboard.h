
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "bt_keyboard.hpp"

void process_key(BTKeyboard::KeyInfo& inf);
int trs_kb_mem_read(int address);
