// Adapted from https://github.com/infovore/pico-example-midi

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

// TODO: Make these options configurable with conditional blocks

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

#include "midi_device_multistream.h"

#include "launchpad.h"

static struct board_state board_state = {
  4, 5, true, {0, UNkNOWN}
};

// End state variables

void midi_client_task(void);

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

    if (board_state.is_dirty) {
      paint_client_launchpads(&board_state);

      if (tuh_midi_mounted(board_state.host.client_idx)) {
        paint_host_launchpad(&board_state);
      }

      board_state.is_dirty = false;
    }
  }
}


//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Client Callbacks

// Invoked when device is mounted
void tud_mount_cb(void) {
    initialise_client_launchpads();
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

// Nicked from: https://github.com/hathach/tinyusb/blob/6ce46da042eb9ab243e4127ddcac57519c0a226f/examples/host/device_info/src/main.c#L118

// Declare for buffer for usb transfer, may need to be in USB/DMA section and
// multiple of dcache line size if dcache is enabled (for some ports).
CFG_TUH_MEM_SECTION struct {
  TUH_EPBUF_TYPE_DEF(tusb_desc_device_t, device);
  TUH_EPBUF_DEF(serial, 64*sizeof(uint16_t));
  TUH_EPBUF_DEF(buf, 128*sizeof(uint16_t));
} desc;

// Invoked when device with MIDI interface is mounted.
void tuh_midi_mount_cb(uint8_t idx, __attribute__((unused)) const tuh_midi_mount_cb_t* mount_cb_data) {
  board_state.host.client_idx = idx;

  // printf("MIDI Interface Index = %u, Address = %u, Number of RX cables = %u, Number of TX cables = %u\r\n",
  // idx, mount_cb_data->daddr, mount_cb_data->rx_cable_count, mount_cb_data->tx_cable_count);

  // TODO: Set up something to contain the device descriptor.

  // Adapted from: https://github.com/hathach/tinyusb/blob/master/examples/host/device_info/src/main.c

  // tusb_xfer_result_t tuh_descriptor_get_device_sync(uint8_t daddr, void* buffer, uint16_t len) {
  tuh_descriptor_get_device_sync(mount_cb_data->daddr, &desc.device, 18);

  // printf("Device %u: ID %04x:%04x SN ", daddr, desc.device.idVendor, desc.device.idProduct);
  board_state.host.launchpad_version = get_launchpad_version(desc.device.idVendor, desc.device.idProduct);
}

// Invoked when device with MIDI interface is un-mounted
void tuh_midi_umount_cb(__attribute__((unused)) uint8_t idx) {
  board_state.host.client_idx = 0;
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  if (xferred_bytes == 0) {
    return;
  }

  uint8_t incoming_packet[4];
  while (tuh_midi_packet_read(idx, incoming_packet)) {
    process_incoming_host_packet(incoming_packet, &board_state);
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
  while (tud_midi_available()) {
    uint8_t incoming_packet[4];
    tud_midi_packet_read(incoming_packet);

    // Temporarily "loopback" internally.
    tud_midi_packet_write(incoming_packet);

    process_incoming_client_packet(incoming_packet, &board_state);
  }
}
