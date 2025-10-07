#include <stdint.h>
#include "launchpad.h"
#include "tusb.h"

void initialise_client_launchpads(void) {
  initialise_mk1_client_launchpads();
  initialise_mk2_client_launchpads();
  initialise_mk3_client_launchpads();
}

void initialise_mk1_client_launchpads(void) {
  // Change the button layout Change the button layout Change the button layout
  // Host » Launchpad: Channel 1: controller 0 set to 1 or 2.
  //  B0h, 00h, 01-02h (176, 0, 1-2). 
  uint8_t x_y_mode_packet[3] = {
    0xB0, 0x00, 1
  };

  // This should use cable 0.
  tud_midi_stream_write(0, x_y_mode_packet, sizeof(x_y_mode_packet));
}

void initialise_mk2_client_launchpads(void) {
  // TODO: Rework when we can send sysex on the host side again.
  // if (!tuh_mounted(board_state->host_port_client_idx)) { return; }
  // if (!board_state->host_port_client_idx) { return; }
  
  // Select "standalone" mode (it's the default, but for users who also use
  // Ableton, this will ensure things are set up properly).
  uint8_t standalone_mode_packet[9] = {
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x2C, 0x03, 0xf7
  };

  // Select "programmer" layout ("note" layout is the default)
  uint8_t programmer_layout_packet[9] = {
    0xf0, 0, 0x20, 0x29, 0x02, 0x10, 0x16, 0x3, 0xf7
  };

  // This should use cable 1.
  tud_midi_stream_write(1, standalone_mode_packet, sizeof(standalone_mode_packet));
  tud_midi_stream_write(1, programmer_layout_packet, sizeof programmer_layout_packet);
}

void initialise_mk3_client_launchpads(void) {
  // Select the programmer's layout, we want layout 11h and page 0
  // F0h 00h 20h 29h 02h 0Eh 00h <layout> <page> 00h F7h
  uint8_t select_programmers_layout[] = {
    0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0x11, 0x00, 0x00, 0xF7
  };

  // They don't have a "clear all" method, just a sysex to send a value for
  // every pad, so we skip that.

  // This should use cable 2.
  tud_midi_stream_write(2, select_programmers_layout, sizeof select_programmers_layout);
}

void paint_client_launchpads(struct board_state *board_state) {
  paint_mk1_client_launchpads(board_state);
  paint_mk2_client_launchpads(board_state);
  paint_mk3_client_launchpads(board_state);
}

void paint_mk1_client_launchpads(struct board_state *board_state) {
  // There is a wacky mode for note on messages on channel 3 where the note is
  // one colour for one pad and the velocity is the colour for the next pad. You
  // blaze through them in sequnce from the top-left corner, which is not how
  // we're working on other devices, and is why we reverse the rows.

  // Send an initial (out of range) note to force any existing bulk update mode to end.
  uint8_t initial_note_on_message[3] = {
    MIDI_CIN_NOTE_ON << 4, 127, 0
  };

  tud_midi_stream_write(0, initial_note_on_message, sizeof(initial_note_on_message));

  for (int row = 8; row > 0; row--) {
    // Shift by one column so that the square pads align on all units.
    for (int col = 1; col < 9; col+=2) {
      uint8_t note = ((row == board_state->active_row) || (col == board_state->active_column)) ? 0x3C : 0x0C;
      uint8_t velocity = ((row == board_state->active_row) || ((col+1) == board_state->active_column)) ? 0x3C : 0x0C;

      uint8_t note_on_message[3] = { 0x92, note, velocity };

      // The virtual port for the MK1 should be cable 0.
      tud_midi_stream_write(0, note_on_message, sizeof(note_on_message));
    }
  }

  // Right-most column, equivalent to column 9 on other devices.  Inverted relative to the pads.
  for (int row = 8; row > 0; row-=2) {
    uint8_t note = ((board_state->active_column == 9) || (row == board_state->active_row)) ? 0x3C : 0x0C;
    uint8_t velocity = ((board_state->active_column == 9) || ((row-1) == board_state->active_row)) ? 0x3C : 0x0C;
    uint8_t note_on_message[3] = { 0x92, note, velocity };

    // The virtual port for the MK1 should be cable 0.
    tud_midi_stream_write(0, note_on_message, sizeof(note_on_message));
  }

  // Top-most row, equivalent to row 9 on larger devices.
  for (int col = 1; col < 9; col+=2) {
    uint8_t note = ((board_state->active_row == 9) || (col == board_state->active_column)) ? 0x3C : 0x0C;
    uint8_t velocity = ((board_state->active_row == 9) || ((col+1) == board_state->active_column)) ? 0x3C : 0x0C;
    uint8_t note_on_message[3] = { 0x92, note, velocity };

    // The virtual port for the MK1 should be cable 0.
    tud_midi_stream_write(0, note_on_message, sizeof(note_on_message));
  }
}

void paint_mk2_client_launchpads(struct board_state *board_state) {
  // Sysex messages used to paint the Launchpad in this pass....

  // The "paint all" operation doesn't support RGB, so you have to pick a colour
  // from the built-in 128 colour palette, for example, 0 for black and 3 for
  // white, 24 for green.
  //

  // Paint All F0h 00h 20h 29h 02h 10h 0Eh                                                                                                                                  <Colour> F7h
  uint8_t paint_all_sysex[9] = {
      0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xE, 0, 0xf7
  };


  // Here's what we'll eventually use for everything in one pass....

  /*
    F0h 00h 20h 29h 02h 10h 0Fh <Grid Type> <Red> <Green> <Blue> F7h
    (240, 0, 32, 41, 2, 16, 15, <Grid Type>, <Red>, <Green>, <Blue>, 247)
    The <Red> <Green> <Blue> group may be repeated in the message up to 100 times.
    <Grid Type> - 0 for 10 by 10 grid, 1 for 8 by 8 grid (central square pads only)
  */

//   uint8_t paint_all_sysex[309] = {
//     0xf0, 0, 0x20, 0x29, 0x2, 0x10, // sysex header plus device/mfr.
//     0xF, // type of message
//     0, // Grid type
//     // 10 rows, 10 cells, 3 colours per cell
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     0xf7 // end sysex message
//   };

  // Paint a Column, same deal about the colour palette.
  // F0h 00h 20h 29h 02h 10h 0Ch <Column> (<Colour> * 10) F7h
  uint8_t paint_column[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xC, board_state->active_column, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };

  // Paint a Row, same deal about the colour palette.
  // F0h 00h 20h 29h 02h 10h 0Dh <Row> (<Colour> * 10) F7h
  uint8_t paint_row[19] = {
    0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xD, board_state->active_row, 
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0xf7
  };
 
  // We currently use the "pulse" method.
  uint8_t paint_side_light[12] = {
    0xf0, 0x00, 0x20, 0x29, 0x2, 0x10, 0x28, 0x63, 3, 0xf7
  };

  // The virtual port for the MK2 should be cable 1.
  tud_midi_stream_write(1, paint_all_sysex, sizeof(paint_all_sysex));
  tud_midi_stream_write(1, paint_side_light, sizeof(paint_side_light));
  tud_midi_stream_write(1, paint_column, sizeof(paint_column));
  tud_midi_stream_write(1, paint_row, sizeof(paint_row));
}

void paint_mk3_client_launchpads(struct board_state *board_state) {

  // TODO: We should eventually use this method instead.
  /*

    Host => Launchpad Pro [MK3]:
    Hex Version: F0h 00h 20h 29h 02h 0Eh 03h <Colour Spec> [ <Colour Spec> [_] ] F7h
    Decimal Version: 240 0 32 41 2 14 3 <Colour Spec> [ <Colour Spec> [_] ] 247


    The <Colour Spec> is structured as follows:
    - Lighting type (1 byte)
    - LED index (1 byte)
    - Lighting data (1 – 3 bytes)

    Lighting types:

        Hex: 00h / Decimal: 0 --- Static colour from palette, Lighting data is 1 byte specifying
        palette entry.
        Hex: 01h / Decimal: 1 --- Flashing colour, Lighting data is 2 bytes specifying Colour B and
        Colour A.
        Hex: 02h / Decimal: 2 --- Pulsing colour, Lighting data is 1 byte specifying palette entry.
        Hex: 03h / Decimal: 3 --- RGB colour, Lighting data is 3 bytes for Red, Green and Blue (127:
    Max, 0: Min).

        [The 1 byte colours are the same palette they use from the MK2, i.e. white is 0x03]
        [The 3 byte colours are similar to the MK2 scheme, 0-127 for each of R, G, and B.]

  */

//   // We don't know what things looked like before, so we have to paint all 100
//   // cells we use. 
//   for (int row = 5; row < 6; row++) {
//     // We're using the indexed colour scheme, so we need 7 bytes for the header, 3
//     // bytes for each pad, and 1 for the footer.
//     uint8_t paint_row[38] = {
//     0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x03, // Mfr. header and command identifier.    
//     };
//     paint_row[17] = 0xF7; // Footer

//     int buffer_index = 7;


//     int row_offset = row == 0 ? 100 : (row * 10);
//     for (int col = 0; col < 10; col++) {
//         int note = row_offset + col;

//         // Lighting type
//         paint_row[buffer_index] = 0x00;
//         buffer_index++;

//         // LED index
//         paint_row[buffer_index] = note;
//         buffer_index++;

//         // Lighting data
//         int color = (row == board_state->active_row || col == board_state->active_column) ? 0x03 : 0x00;
//         paint_row[buffer_index] = color;
//         buffer_index++;
//     }

//     // The virtual port for the MK3 should be cable 2.
//     tud_midi_stream_write(2, paint_row, sizeof(paint_row));
//   }

    // Write note messages for the host side until we figure out sysex there.
    for (int note = 1; note < 99; note++) {
        int note_col = note % 10;
        int note_row = (note - note_col)/10;

        uint8_t velocity = (note_col == board_state->active_column || note_row == board_state->active_row) ? 3 : 0;

        uint8_t note_on_message[3] = {
        MIDI_CIN_NOTE_ON << 4, note, velocity
        };

        tud_midi_stream_write(2, note_on_message, sizeof(note_on_message));
    }

}

void paint_host_launchpad(struct board_state *board_state) {
    if (board_state->host.launchpad_version == MK1) {
        paint_mk1_host_launchpad(board_state);
    }
    else if (board_state->host.launchpad_version == MK2) {
        paint_mk2_host_launchpad(board_state);
    }
    else if (board_state->host.launchpad_version == MK3) {
        paint_mk3_host_launchpad(board_state);
    }
}

// TODO: When we figure out sending sysex to the host's client device, we can simplify this.
void paint_mk1_host_launchpad(struct board_state *board_state) {
    // We use a different strategy here because the MK1 units skip notes between rows.
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
        int note = (row * 16) + col;

        uint8_t velocity = (col == board_state->active_column || row == board_state->active_row) ? 3 : 0;

        uint8_t note_on_message[3] = {
            MIDI_CIN_NOTE_ON << 4, note, velocity
        };

        tuh_midi_stream_write(board_state->host.client_idx, 0, note_on_message, sizeof(note_on_message));
        }
    }
}

// TODO: When we figure out sending sysex to the host's client device, we can simplify this.
void paint_mk2_host_launchpad(struct board_state *board_state) {
    // Write note messages for the host side until we figure out sysex there.
    for (int note = 1; note < 99; note++) {
        int note_col = note % 10;
        int note_row = (note - note_col)/10;

        uint8_t velocity = (note_col == board_state->active_column || note_row == board_state->active_row) ? 3 : 0;

        uint8_t note_on_message[3] = {
        MIDI_CIN_NOTE_ON << 4, note, velocity
        };

        // tuh_midi_stream_write(board_state->host.client_idx, 1, note_on_message, sizeof(note_on_message));
        tuh_midi_stream_write(0, 1, note_on_message, sizeof(note_on_message));
    }
}

// TODO: When we figure out sending sysex to the host's client device, we can
// simplify this by using their sysex strategy (see the client implementation).
void paint_mk3_host_launchpad(struct board_state *board_state) {
    // Write note messages for the host side until we figure out sysex there.
    for (int note = 1; note < 99; note++) {
        int note_col = note % 10;
        int note_row = (note - note_col)/10;

        uint8_t velocity = (note_col == board_state->active_column || note_row == board_state->active_row) ? 3 : 0;

        uint8_t note_on_message[3] = {
        MIDI_CIN_NOTE_ON << 4, note, velocity
        };

        // The MK3 wants data on the first cable, i.e. "MIDI" and not "DIN" or "DAW"
        tuh_midi_stream_write(board_state->host.client_idx, 0, note_on_message, sizeof(note_on_message));
    }
}

void process_incoming_host_packet(uint8_t *incoming_packet, struct board_state *board_state) {
    if (board_state->host.launchpad_version == MK1) {
        process_incoming_mk1_packet(incoming_packet, board_state);
    }
    else if (board_state->host.launchpad_version == MK2) {
        process_incoming_mk2_packet(incoming_packet, board_state);
    }
    else if (board_state->host.launchpad_version == MK3) {
        process_incoming_mk3_packet(incoming_packet, board_state);
    }
}

void process_incoming_client_packet(uint8_t *incoming_packet, struct board_state *board_state) {
    uint8_t cable = (incoming_packet[0] >> 4) & 0xf;

    // MK1
    if (cable == 0) {
      process_incoming_mk1_packet(incoming_packet, board_state);
    }
    // MK2
    else if (cable == 1) {
      process_incoming_mk2_packet(incoming_packet, board_state);
    }
    // MK3
    else if (cable == 2) {
      process_incoming_mk3_packet(incoming_packet, board_state);
    }
}


// Respond to MK1 (Launchpad S) controls
void process_incoming_mk1_packet (uint8_t *incoming_packet, struct board_state *board_state) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);
  
  // Start with the message type
  int type = data[0] >> 4;

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 104:
          board_state->active_row = (board_state->active_row + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Downward arrow
        case 105:
          board_state->active_row = (board_state->active_row + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Left Arrow
        case 106:
          board_state->active_column = (board_state->active_column + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Right Arrow
        case 107:
          board_state->active_column = (board_state->active_column + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

// Respond to MK2 controls
void process_incoming_mk2_packet (uint8_t *incoming_packet, struct board_state *board_state) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);
  
  // Start with the message type
  int type = data[0] >> 4;

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 91:
          board_state->active_row = (board_state->active_row + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Downward arrow
        case 92:
          board_state->active_row = (board_state->active_row + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Left Arrow
        case 93:
          board_state->active_column = (board_state->active_column + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Right Arrow
        case 94:
          board_state->active_column = (board_state->active_column + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

// Respond to MK3 controls
void process_incoming_mk3_packet (uint8_t *incoming_packet, struct board_state *board_state) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);
  
  // Start with the message type
  int type = data[0] >> 4;

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 80:
          board_state->active_row = (board_state->active_row + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Downward arrow
        case 70:
          board_state->active_row = (board_state->active_row + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Left Arrow
        case 91:
          board_state->active_column = (board_state->active_column + 9) % 10;
          board_state->is_dirty = true;
          break;
        // Right Arrow
        case 92:
          board_state->active_column = (board_state->active_column + 1) % 10;
          board_state->is_dirty = true;
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

enum LaunchpadVersion get_launchpad_version (uint16_t idVendor, uint16_t idProduct) {
  enum LaunchpadVersion launchpad_version;
  launchpad_version = UNkNOWN;

  if (idVendor == 0x1235) {
    if (idProduct == 0x000E) {
      launchpad_version = MK1;
    }
    else if (idProduct >= 0x0051 && idProduct <= 0x0060) {
      launchpad_version = MK2;
    }
    else if (idProduct >= 0x0123 && idProduct <= 0x0132) {
      launchpad_version = MK3;
    }
  }

  return launchpad_version;
}