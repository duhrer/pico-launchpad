// Adapted from https://github.com/infovore/pico-example-midi

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

// Common state variables

// We only work with the most recently connected MIDI device, if we add hub support we'll have to flesh this out further.
uint8_t client_device_idx = 0;

// Module-specific state
int user_selected_row = 5;
int user_selected_column = 4;

// TODO: Make this configurable

// Adafruit RP2040 with Type A Host
//#define PIO_USB_DP_PIN      16

// RP2 wired to OGX conventions
// #define PIO_USB_DP_PIN  0

// Waveshare RP2350 
#define PIO_USB_DP_PIN      12

// "Breadboard" RP2 wired to avoid taking over the UART pins
// #define PIO_USB_DP_PIN 6

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

int main() {
  // TODO: Make this depend on the board type and make the port configurable
 
  // Enable USB power for client devices, nicked from OGX MINI:
  // https://github.com/wiredopposite/OGX-Mini/blob/ea14d683adeea579228109d2f92a4465fb76974d/Firmware/RP2040/src/Board/board_api_private/board_api_usbh.cpp#L35
  //
  // This is required for Adafruit RP2040 with Type A Host
  // gpio_init(18);
  // gpio_set_dir(18, GPIO_OUT);
  // gpio_put(18, 1);

  // According to the PIO author, the default 125MHz is not appropriate, instead
  // the sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  // Give the client side a brief chance to start up.
  sleep_ms(10);

  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // Start the device stack on the native USB port.
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

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (midi_data[2]) {
      switch(midi_data[1]) {
        // Upward arrow
        case 91:
          user_selected_row = (user_selected_row + 1) % 10;
          is_dirty = true;
          break;
        // Downward arrow
        case 92:
          user_selected_row = (user_selected_row + 9) % 10;
          is_dirty = true;
          break;
        // Left Arrow
        case 93:
          user_selected_column = (user_selected_column + 9) % 10;
          is_dirty = true;
          break;
        // Right Arrow
        case 94:
          user_selected_column = (user_selected_column + 1) % 10;
          is_dirty = true;
          break;
        // Ignore everything else
        default:
          break;
      }
    }
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
void tud_mount_cb(void) {
    midi_initialise_launchpad();
    midi_paint_launchpad();
}

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

  // TODO: Enable once we get host-side sysex working.
  // midi_initialise_launchpad();
  midi_paint_launchpad();
}

// Invoked when device with MIDI interface is un-mounted
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

void midi_initialise_launchpad(void) {
  // TODO: Rework when we can send sysex on the host side again.
  // if (!tuh_midi_mounted(client_device_idx)) { return; }
  // if (!client_device_idx) { return; }
  
  // Select "standalone" mode (it's the default, but for users who also use
  // Ableton, this will ensure things are set up properly).
  uint8_t standalone_mode_packet[9] = {
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x2C, 0x03, 0xf7
  };

  // Select "programmer" layout ("note" layout is the default)
  uint8_t programmer_layout_packet[9] = {
    0xf0, 0, 0x20, 0x29, 0x02, 0x10, 0x16, 0x3, 0xf7
  };

  // if (tuh_midi_mounted(client_device_idx)) {
  //   tuh_midi_stream_write(client_device_idx, 1, standalone_mode_packet, sizeof(standalone_mode_packet));
  //   tuh_midi_stream_write(client_device_idx, 1, programmer_layout_packet, sizeof programmer_layout_packet);
  // }

  tud_midi_stream_write(0, standalone_mode_packet, sizeof(standalone_mode_packet));
  tud_midi_stream_write(0, programmer_layout_packet, sizeof programmer_layout_packet);
}

// "Paint" our current state to the Launchpad
void midi_paint_launchpad(void) {
  // Sysex messages used to paint the Launchpad in this pass....

  // The "paint all" operation doesn't support RGB, so you have to pick a colour
  // from the built-in 128 colour palette, for example, 0 for black and 3 for
  // white, 24 for green.
  //
  // Paint All F0h 00h 20h 29h 02h 10h 0Eh <Colour> F7h
  uint8_t paint_all_sysex[9] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xE, 0, 0xf7
  };

  // Paint a Column, same deal about the colour palette.
  // F0h 00h 20h 29h 02h 10h 0Ch <Column> (<Colour> * 10) F7h
  uint8_t paint_column[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xC, user_selected_column, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };

  // Paint a Row, same deal about the colour palette.
  // F0h 00h 20h 29h 02h 10h 0Dh <Row> (<Colour> * 10) F7h
  uint8_t paint_row[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xD, user_selected_row, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };
 

  // For reference, there are three ways to light a single LED like the side
  // LED:
  
  // Light single LED using RGB, where RGB values are 0-127 F0h 00h 20h 29h
  // 02h 10h 0Bh <LED> <Red> <Green> <Blue> F7h

  // Light single LED with colour from the standard palette.
  // F0h 00h 20h 29h 02h 10h 0Ah <LED> <Colour> F7h

  // "Pulse" single LED using a colour from the standard palette.
  // F0h 00h 20h 29h 02h 10h 28h <LED> <Colour> F7h
  // f0 00 20 29 02 10 28 63 57 f7

  // We currently use the "pulse" method.
  uint8_t paint_side_light[12] = {
    0xf0, 0x00, 0x20, 0x29, 0x2, 0x10, 0x28, 0x63, 3, 0xf7
  };

  // Update our host port "client device"
  if (tuh_midi_mounted(client_device_idx)) {
    // Write note messages for the host side until we figure out sysex there.
    for (int note = 1; note < 99; note++) {
      int note_col = note % 10;
      int note_row = (note - note_col)/10;

      uint8_t velocity = (note_col == user_selected_column || note_row == user_selected_row) ? 3 : 0;

      uint8_t note_on_message[3] = {
        MIDI_CIN_NOTE_ON << 4, note, velocity
      };

      tuh_midi_stream_write(client_device_idx, 1, note_on_message, sizeof(note_on_message));
    }

    // This is the opposite of a canary in the coal mine.  If this comes back to
    // life and lights up the "side LED", we've managed to get sysex working on
    // the host side.
    //
    // Notes thus far:
    // 1. The same sysex works on the client side
    // 2. tuh_midi_stream_write claims that bytes have been written.

    // A packet consists of 4 bytes (8-bit integers) for a "Normal" 3 byte message like a "note on":
    //
    // 0. contains the "cable" we're using
    // 1. MIDI status byte (4 bits for the message type and 4 bits for the channel channel)
    // 2. (optional) Data Byte, varies by message type.
    // 3. (optional) Data Byte, varies by message type, or EOX in the case of System Exclusive messages.
    //
    // In our case, we're setting the first byte and copying three bytes from a larger payload.

    // Tried breaking it up into multiple 4 byte packets, which I obviously don't understand
    //tuh_midi_stream_write(client_device_idx, 1, paint_side_light, sizeof(paint_side_light));
    // bool tuh_midi_packet_write (uint8_t idx, uint8_t const packet[4])
    // tuh_midi_packet_write(client_device_idx, (uint8_t[4]){
    //   1, 0xf0, 0x00, 0x20
    // });
    // tuh_midi_packet_write(client_device_idx, (uint8_t[4]){
    //   1, 0x29, 0x2, 0x10
    // });
    // tuh_midi_packet_write(client_device_idx, (uint8_t[4]){
    //   1, 0x28, 0x63, 3
    // });

    // tuh_midi_packet_write(client_device_idx, (uint8_t[4]){
    //   1, 0xf7
    // });

    // uint32_t tuh_midi_packet_write_n(uint8_t idx, const uint8_t* buffer, uint32_t bufsize)
    uint8_t paint_side_light_packet[13] = {
        // 1,
      0xf0, 0x00, 0x20, 0x29, 0x2, 0x10, 0x28, 0x63, 3, 0xf7
    };
    tuh_midi_packet_write_n(client_device_idx, paint_side_light_packet, 12);   

    // For host transmissions, we have to "flush" the output manually once per cycle.
    tuh_midi_write_flush(client_device_idx);
  }

  // We can use sysex messages to paint the client.
  tud_midi_stream_write(0, paint_side_light, sizeof(paint_side_light));
  tud_midi_stream_write(0, paint_all_sysex, sizeof(paint_all_sysex));
  tud_midi_stream_write(0, paint_column, sizeof(paint_column));
  tud_midi_stream_write(0, paint_row, sizeof(paint_column));
}

