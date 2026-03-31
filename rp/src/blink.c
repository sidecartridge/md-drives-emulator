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
static bool blinkSequenceActive = false;
static bool blinkSequenceLedOn = false;
static uint8_t blinkSequenceRemaining = 0;
static absolute_time_t blinkSequenceNext = {0};
static bool blinkActivityLedOn = false;
static absolute_time_t blinkActivityOffTime = {0};

static void blink_set_hw(bool on) {
#if defined(CYW43_WL_GPIO_LED_PIN)
  network_initChipOnly();
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#else
  gpio_put(PICO_DEFAULT_LED_PIN, on ? 1 : 0);
#endif
}

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
  blink_set_hw(true);
}

void blink_off() {
  blink_set_hw(false);
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

void blink_activityPulse(void) {
  if (blinkSequenceActive) {
    return;
  }

  blinkActivityLedOn = true;
  blink_on();
  blinkActivityOffTime = make_timeout_time_us(BLINK_ACTIVITY_ON_US);
}

void blink_startCountSequence(uint8_t count) {
  if (count == 0) {
    return;
  }

  blinkSequenceActive = true;
  blinkSequenceLedOn = true;
  blinkSequenceRemaining = count;
  blink_on();
  blinkSequenceNext = make_timeout_time_ms(BLINK_SEQUENCE_ON_MS);
}

void blink_poll(void) {
  if (blinkSequenceActive) {
    if (absolute_time_diff_us(get_absolute_time(), blinkSequenceNext) >= 0) {
      return;
    }

    if (blinkSequenceLedOn) {
      blink_off();
      blinkSequenceLedOn = false;
      if (blinkSequenceRemaining > 0) {
        blinkSequenceRemaining--;
      }
      if (blinkSequenceRemaining == 0) {
        blinkSequenceActive = false;
        return;
      }
      blinkSequenceNext = make_timeout_time_ms(BLINK_SEQUENCE_OFF_MS);
      return;
    }

    blink_on();
    blinkSequenceLedOn = true;
    blinkSequenceNext = make_timeout_time_ms(BLINK_SEQUENCE_ON_MS);
    return;
  }

  if (!blinkActivityLedOn) {
    return;
  }

  if (absolute_time_diff_us(get_absolute_time(), blinkActivityOffTime) > 0) {
    return;
  }

  blink_off();
  blinkActivityLedOn = false;
}

bool blink_isSequenceActive(void) { return blinkSequenceActive; }
