#ifndef _LAUNCHPAD_H_
#define _LAUNCHPAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Definitions, initially we'll split this up a bit.
enum LaunchpadVersion {
  UNkNOWN,
  MK1,
  MK2,
  MK3
};

struct host_state {
    uint8_t client_idx;
    enum LaunchpadVersion launchpad_version;
};

struct board_state {
    int active_row;
    int active_column;
    bool is_dirty;

    struct host_state host;
};

void initialise_client_launchpads(void);

void initialise_mk1_client_launchpads(void);
void initialise_mk2_client_launchpads(void);
void initialise_mk3_client_launchpads(void);

void paint_client_launchpads(struct board_state*);

void paint_mk1_client_launchpads(struct board_state*);
void paint_mk2_client_launchpads(struct board_state*);
void paint_mk3_client_launchpads(struct board_state*);

void paint_host_launchpad(struct board_state*);

void paint_mk1_host_launchpad(struct board_state*);
void paint_mk2_host_launchpad(struct board_state*);
void paint_mk3_host_launchpad(struct board_state*);

void process_incoming_host_packet(uint8_t*, struct board_state*);

void process_incoming_client_packet(uint8_t *, struct board_state*);

void process_incoming_mk1_packet (uint8_t*, struct board_state*);
void process_incoming_mk2_packet (uint8_t*, struct board_state*);
void process_incoming_mk3_packet (uint8_t*, struct board_state*);

enum LaunchpadVersion get_launchpad_version (uint16_t, uint16_t);

#ifdef __cplusplus
}
#endif

#endif /* _LAUNCHPAD_H_ */