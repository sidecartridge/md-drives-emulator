#include "blink.h"

static MorseCode morseAlphabet[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"},
    {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'\0', NULL}  // Sentinel value to mark end of array
};

static bool blinkState = false;
static absolute_time_t blinkTime = {0};

void blink_morse(char chr) {
  void blink_morse_container() {
    const char *morseCode = NULL;
    // Search for character's Morse code
    for (int i = 0; morseAlphabet[i].character != '\0'; i++) {
      if (morseAlphabet[i].character == chr) {
        morseCode = morseAlphabet[i].morse;
        break;
      }
    }

    // If character not found in Morse alphabet, return
    if (!morseCode) return;

    // Blink pattern
    for (int i = 0; morseCode[i] != '\0'; i++) {
#if defined(CYW43_WL_GPIO_LED_PIN)
      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#else
      gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif
      if (morseCode[i] == '.') {
        // Short blink for dot
        sleep_ms(DOT_DURATION_MS);
      } else {
        // Long blink for dash
        sleep_ms(DASH_DURATION_MS);
      }
#if defined(CYW43_WL_GPIO_LED_PIN)
      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
#else
      gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif
      // Gap between symbols
      sleep_ms(SYMBOL_GAP_MS);
    }
  }
#if defined(CYW43_WL_GPIO_LED_PIN)
#ifdef NETWORK_H
  network_initChipOnly();
#endif
#endif
  blink_morse_container();
}

void blink_error() {
  // If we are here, something went wrong. Flash 'E' in morse code forever
  while (1) {
    blink_morse('E');
    sleep_ms(CHARACTER_GAP_MS);
  }
}

void blink_on() {
#if defined(CYW43_WL_GPIO_LED_PIN)
  network_initChipOnly();
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#else
  gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif
}

void blink_off() {
#if defined(CYW43_WL_GPIO_LED_PIN)
  network_initChipOnly();
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
#else
  gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif
}

void blink_toogle() {
  // Blink when timeout
  if ((absolute_time_diff_us(get_absolute_time(), blinkTime) < 0)) {
    blinkState = !blinkState;
    blinkTime = make_timeout_time_ms(CHARACTER_GAP_MS);
    if (blinkState) {
      blink_on();
    } else {
      blink_off();
    }
  }
}
