// Adapted from https://github.com/infovore/pico-example-midi

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

// Set the "data plus" pin for PIO USB to 16, which is what the Adafruit feather rp2040 with USB Host uses.
#define PIO_USB_DP_PIN      16

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

void core1_main() {
  sleep_ms(10);

  pio_usb_configuration_t pio_cfg = PIO_USB_CONFIG;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  tuh_init(1);

  while (true) {
    tuh_task();
  }
}

int main() {
  // Enable USB power for client devices, nicked from OGX MINI:
  // https://github.com/wiredopposite/OGX-Mini/blob/ea14d683adeea579228109d2f92a4465fb76974d/Firmware/RP2040/src/Board/board_api_private/board_api_usbh.cpp#L35

  gpio_init(18);
  gpio_set_dir(18, GPIO_OUT);
  gpio_put(18, 1);

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
// 1. MIDI status byte (4 bits for the message type and 4 bits for the channel channel)
// 2. (optional) Data Byte, varies by message type.
// 3. (optional) Data Byte, varies by message type, or EOX in the case of System Exclusive messages.
//
// I still find the core of flocking-midi useful in reminding myself of the bit-packing schemes:
// https://github.com/lichen-community-systems/flocking-midi/blob/main/src/core.js

void transform_and_send_incoming_packet (uint8_t *incoming_packet) {
    uint8_t midi_data[3];
    memcpy(midi_data, incoming_packet + 1, 3);
    
    bool send_data = true;

    // Start with the message type
    int type = midi_data[0] >> 4;

    // int channel = midi_data[0] & 0xf;

    // Force all messages to MIDI channel 0.
    midi_data[0] = generate_status_byte(type, 0);

    switch (type) {
      case MIDI_CIN_NOTE_OFF:
      case MIDI_CIN_NOTE_ON:
      case MIDI_CIN_POLY_KEYPRESS:
      case MIDI_CIN_CHANNEL_PRESSURE:
        // Pass through note and aftertouch messages without further modification.
        break;
      case MIDI_CIN_CONTROL_CHANGE:
        // Invert the mod wheel
        if (midi_data[1] == 1) {
          midi_data[2] = 127 - midi_data[2];
        }
        break;
      case MIDI_CIN_PITCH_BEND_CHANGE:
        // Invert the pitch bend value
        int raw_value = midi_data[2] << 7 | midi_data[1];
        int inverted_value = 16383 - raw_value;
        midi_data[1] = inverted_value & 0x7f;
        midi_data[2] = (inverted_value >> 7) & 0x7f;

        break;
      // Strip all other message types such as sysex, clock, etc.
      default:
        send_data = false;
    }


    if (send_data) {
      tud_midi_stream_write(0, midi_data, 3);
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
// remote_wakeup_en : if host allow us  to perform remote wakeup
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
void tuh_midi_mount_cb(__attribute__((unused)) uint8_t idx, __attribute__((unused)) const tuh_midi_mount_cb_t* mount_cb_data) {}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(__attribute__((unused)) uint8_t idx) {}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  if (xferred_bytes == 0) {
    return;
  }

  uint8_t incoming_packet[4];
  while (tuh_midi_packet_read(idx, incoming_packet)) {
    transform_and_send_incoming_packet(incoming_packet);
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
    transform_and_send_incoming_packet(incoming_packet);
  }
}

void midi_host_task(void) {
  // TODO: Do something?
}