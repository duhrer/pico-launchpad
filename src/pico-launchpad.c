// Adapted from https://github.com/infovore/pico-example-midi

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

// Needed for Pico W / Pico 2 W / Pimoroni Plus 2W
// #include "pico/cyw43_arch.h"

// Common state variables

// We only work with the most recently connected MIDI device, if we add hub support we'll have to flesh this out further.
uint8_t client_device_idx = 0;

// Module-specific state
int x_offset = 0;
int y_offset = 0;

// Set the "data plus" pin for PIO USB to 16, which is what the Adafruit feather rp2040 with USB Host uses.
// TODO: Make this configurable

// Adafruit RP2040 with Type A Host
//#define PIO_USB_DP_PIN      16

// Pico W and similar boards
//#define LED_PIN CYW43_WL_GPIO_LED_PIN

// Pico and similar boards
#define LED_PIN PICO_DEFAULT_LED_PIN

// RP2 wired to OGX conventions
// #define PIO_USB_DP_PIN  0

// RP2 wired to avoid taking over the UART pins
#define PIO_USB_DP_PIN 6

#define PIO_USB_CONFIG { \
    PIO_USB_DP_PIN, \
    PIO_USB_TX_DEFAULT, \
    PIO_SM_USB_TX_DEFAULT, \
    PIO_USB_DMA_TX_DEFAULT, \
    PIO_USB_RX_DEFAULT, \
    PIO_SM_USB_RX_DEFAULT, \
    PIO_SM_USB_EOP_DEFAULT, \
    NULL, \
    PIO_USB_DEBUG_PIN_NONE, \
    PIO_USB_DEBUG_PIN_NONE, \
    false, \
    PIO_USB_PINOUT_DPDM \
}

#include "pio_usb.h"
#include "tusb.h"

void midi_client_task(void);
void midi_host_task(void);

void midi_initialise_launchpad(void);
void midi_paint_launchpad(void);

void core1_main() {
  sleep_ms(10);

  pio_usb_configuration_t pio_cfg = PIO_USB_CONFIG;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  tuh_init(BOARD_TUH_RHPORT);

  while (true) {
    tuh_task();
  }
}

static bool led_on = true;
void toggle_led (void) {
  led_on = !led_on;
  gpio_put(LED_PIN, led_on);

  // Alternate GPIO functions for wireless chipset  
  // cyw43_arch_gpio_put(LED_PIN, led_on);
}

int main() {
  // TODO: Extract the common setup for reuse in multiple modules

  // Light for Pico W/ Pico 2 W and derivatives like the Pimoroni Pico Plus 2W
  // TODO: This keeps us from being able to work with the Launchpad as a client.
  // cyw43_arch_init();
  // cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  // Version for boards that just have a "regular" LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, led_on);

  // Enable USB power for client devices, nicked from OGX MINI:
  // https://github.com/wiredopposite/OGX-Mini/blob/ea14d683adeea579228109d2f92a4465fb76974d/Firmware/RP2040/src/Board/board_api_private/board_api_usbh.cpp#L35


  // Required for Adafruit RP2040 with Type A Host
  // TODO: Make the port configurable
  // gpio_init(18);
  // gpio_set_dir(18, GPIO_OUT);
  // gpio_put(18, 1);

  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // device stack on native USB
  tud_init(0);

  while (true)
  {
    tud_task(); // tinyusb device task

    midi_client_task();
  }
}


// Adapted from: https://github.com/lichen-community-systems/flocking-midi/blob/3fa553875b6b478fdb840da9ca006ae9beb04b10/src/core.js#L447
int generate_status_byte (int msNibble, int lsNibble) {
    return (msNibble << 4) + lsNibble;
};

// The packet consists of 4 bytes (8-bit integers):
//
// 0. contains the "cable" we're using
// 1. MIDI status byte (4 bits for the message type and 4 bits for the channel)
// 2. (optional) Data Byte, varies by message type.
// 3. (optional) Data Byte, varies by message type, or EOX in the case of System Exclusive messages.
//
// I still find the core of flocking-midi useful in reminding myself of the bit-packing schemes:
// https://github.com/lichen-community-systems/flocking-midi/blob/main/src/core.js

void process_incoming_packet (uint8_t *incoming_packet) {
  uint8_t midi_data[3];
  memcpy(midi_data, incoming_packet + 1, 3);
  
  // Start with the message type
  int type = midi_data[0] >> 4;

  bool is_dirty;

  switch (type) {
    case MIDI_CIN_CONTROL_CHANGE:
      switch(midi_data[1]) {
        // Upward arrow
        case 91:
          if (y_offset > -5) {
            y_offset--;
            is_dirty = true;
          }
          break;
        // Downward arrow
        case 92:
          if (y_offset < 5) {
            y_offset++;
            is_dirty = true;
          }
          break;
        // Left Arrow
        case 93:
          if (x_offset > -5) {
            x_offset--;
            is_dirty = true;
          }
          break;
        // Right Arrow
        case 94:
          if (x_offset < 5) {
            x_offset++;
            is_dirty = true;
          }
          break;
        // Ignore everything else
        default:
          break;
      }
      break;
    default:
      break;
  }


  if (is_dirty) {
    midi_paint_launchpad();
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Client Callbacks

// Invoked when device is mounted
void tud_mount_cb(void) {}

// Invoked when device is unmounted
void tud_umount_cb(void) {}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allows us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {}

// Host Callbacks

// The empty placeholder callbacks would ordinarily throw warnings about unused variables, so we use the strategy outlined here:
// https://stackoverflow.com/questions/3599160/how-can-i-suppress-unused-parameter-warnings-in-c

// Invoked when device with MIDI interface is mounted.
void tuh_midi_mount_cb(uint8_t idx, __attribute__((unused)) const tuh_midi_mount_cb_t* mount_cb_data) {
  printf("MIDI Interface Index = %u, Address = %u, Number of RX cables = %u, Number of TX cables = %u\r\n",
          idx, mount_cb_data->daddr, mount_cb_data->rx_cable_count, mount_cb_data->tx_cable_count);

  client_device_idx = idx;
  midi_initialise_launchpad();
  midi_paint_launchpad();
}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(__attribute__((unused)) uint8_t idx) {
  client_device_idx = idx;
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  if (xferred_bytes == 0) {
    return;
  }

  uint8_t incoming_packet[4];
  while (tuh_midi_packet_read(idx, incoming_packet)) {
    // We should be able to set this some place saner.
    client_device_idx = idx;
    process_incoming_packet(incoming_packet);
  }
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes) {
  (void) idx;
  (void) xferred_bytes;
}

// End TinyUSB Callbacks

//--------------------------------------------------------------------+
// MIDI Tasks
//--------------------------------------------------------------------+
void midi_client_task(void)
{

  // Read any incoming messages from our primary USB port.
  while (tud_midi_available()) {
    uint8_t incoming_packet[4];
    tud_midi_packet_read(incoming_packet);
    process_incoming_packet(incoming_packet);
  }
}

void midi_host_task(void) {
  // TODO: Do something?
}

/**
 * Write a MIDI message to both our client side (tud) and host side (tuh).
 */
void combined_midi_stream_write (uint8_t const* buffer) {
  int total_buffer_size = sizeof(buffer);

  // Sysex
  if (buffer[0] == 0xF0 && total_buffer_size > 3) {
    // Send them two bytes at a time with "continuation" status (0xF7) for each subsequent packet.
    for (int offset = 1; offset < total_buffer_size; offset += 2 ) {
      uint8_t *sub_buffer;

      int bytes_remaining = total_buffer_size - offset;
      int bytes_to_copy = bytes_remaining < 2 ? bytes_remaining : 2;

      uint8_t leading_byte = offset == 1 ? 0xf0 : 0xf7;

      sub_buffer = (uint8_t *)malloc((bytes_to_copy + 1) * sizeof(uint8_t));
      sub_buffer[0] = leading_byte;

      memcpy(sub_buffer + 1, buffer + offset, bytes_to_copy);

      // static inline uint32_t tud_midi_stream_write (uint8_t cable_num, uint8_t const* buffer, uint32_t bufsize);
      tud_midi_stream_write(0, sub_buffer, sizeof(sub_buffer));

      // uint32_t tuh_midi_stream_write(uint8_t idx, uint8_t cable_num, uint8_t const *p_buffer, uint32_t bufsize);
      tuh_midi_stream_write(1, 0, sub_buffer, sizeof(sub_buffer));
      tuh_midi_write_flush(1);

      free(sub_buffer);
    }
  }
  // Everything else
  else {
    // static inline uint32_t tud_midi_stream_write (uint8_t cable_num, uint8_t const* buffer, uint32_t bufsize);
    tud_midi_stream_write(0, buffer, total_buffer_size);

    // uint32_t tuh_midi_stream_write(uint8_t idx, uint8_t cable_num, uint8_t const *p_buffer, uint32_t bufsize);
    tuh_midi_stream_write(1, 0, buffer, total_buffer_size);
  }
}

void midi_initialise_launchpad(void) {
  // if (!tuh_midi_mounted(client_device_idx)) { return; }
  // if (!client_device_idx) { return; }

  // Select "standalone" mode (it's the default, but for users who also use
  // Ableton, this will ensure things are set up properly).
  uint8_t standalone_mode_packet[9] = {
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x2C, 0x03, 0xf7
  };

  combined_midi_stream_write(standalone_mode_packet);

  // Select "programmer" layout
  uint8_t programmer_layout_packet[9] = {
    0xf0, 0, 0x20, 0x29, 0x02, 0x10, 0x16, 0x3, 0xf7
  };

  combined_midi_stream_write(programmer_layout_packet);
}

// "Paint" our current state to the Launchpad
void midi_paint_launchpad(void) {
  // if (!tuh_midi_mounted(client_device_idx)) { return; }
  // if (!client_device_idx) { return; }

  // TODO: Remove this once we figure out what's up with sysex
  for (int note = 51; note < 59; note++) {
    uint8_t send_note_on[3] = {
      MIDI_CIN_NOTE_ON << 4, note, 127
    };

    combined_midi_stream_write(send_note_on);
  }  



  // "Pulse" single LED
  // F0h 00h 20h 29h 02h 10h 28h <LED> <Colour> F7h
  // f0 00 20 29 02 10 28 63 57 f7

  // Light single LED with colour from 128 char palette
  // F0h 00h 20h 29h 02h 10h 0Bh <LED> <Colour> F7h
  uint8_t paint_side_light[12] = {
    0xf0, 0x00, 0x20, 0x29, 0x2, 0x10, 0x0B, 0x63, 0x03, 0xf7
  };

  // Light single LED using RGB, where RGB values are 0-127
  // F0h 00h 20h 29h 02h 10h 0Bh <LED> <Red> <Green> <Blue> F7h

  combined_midi_stream_write(paint_side_light);
  
  // tuh_midi_stream_write(client_device_idx, 1, paint_side_light, 12);

  // The "paint all" operation doesn't support RGB, so you have to pick a colour
  // from the built-in 128 colour palette, for example, 0 for black and 3 for
  // white.
  //
  // Paint All F0h 00h 20h 29h 02h 10h 0Eh <Colour> F7h
  uint8_t paint_all_sysex[9] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xE, 0, 0xf7
  };

  combined_midi_stream_write(paint_all_sysex);

  int first_column = 4 - x_offset;
  int second_column = 5 - x_offset;

  // Paint a Column, same deal about the colour
  // F0h 00h 20h 29h 02h 10h 0Ch <Column> (<Colour> * 10) F7h

  uint8_t paint_second_column[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xC, second_column, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };

  if (first_column >= 0) {
      uint8_t paint_first_column[19];
      memcpy(paint_first_column, paint_second_column, 19);
      paint_first_column[7] = first_column;
      combined_midi_stream_write(paint_first_column);
  }

  combined_midi_stream_write(paint_second_column);

  int first_row = 4 - y_offset;
  int second_row = 5 - y_offset;

  // Paint a Row, same deal about the colour
  // F0h 00h 20h 29h 02h 10h 0Dh <Row> (<Colour> * 10) F7h

  uint8_t paint_second_row[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xC, second_row, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };

  if (first_row >= 0) {
      uint8_t paint_first_row[19];
      memcpy(paint_first_row, paint_second_row, 19);
      paint_first_row[7] = first_row;
      combined_midi_stream_write(paint_first_row);
  }
  
  combined_midi_stream_write(paint_second_row);

  // Turn das blinkenlight off and on so we can tell it's trying to do something.
  toggle_led();

  // Paint the whole grid by repeating R,G,B values until it's complete.
  /*
    F0h 00h 20h 29h 02h 10h 0Fh <Grid Type> <Red> <Green> <Blue> F7h
    (240, 0, 32, 41, 2, 16, 15, <Grid Type>, <Red>, <Green>, <Blue>, 247)
    The <Red> <Green> <Blue> group may be repeated in the message up to 100 times.
    <Grid Type>
    - 0 for 10 by 10 grid, 1 for 8 by 8 grid (central square pads only)
  */
}

