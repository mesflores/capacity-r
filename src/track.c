// track.c
//
// Functions to manage tracks and queues at stations

#include "ross.h"
#include "passenger.h"
#include "model.h"
#include "station.h"
#include "track.h"


// Map to a left or right track
track_t* track_map(int curr_station, int prev_station, station_state* s, message *in_msg) {
    // Left
    if (prev_station < curr_station) {
        return &(s->left); 
        // Right
    } else if (prev_station > curr_station) {
        return &(s->right);
        // This is basically an assert...
    } else {
#if DEBUG_FILE_OUTPUT
        fprintf(node_out_file, "[ST %d] Proccessing a %d\n", curr_station, in_msg-> type);
        fprintf(node_out_file, "[ST %d] Invalid matching stations: curr: %d prev: %d\n", curr_station, curr_station, prev_station);
        fflush(node_out_file);
#endif
        exit(-1);
    }
}
track_t* track_map_rev(int curr_station, int prev_station, station_state* s, message *in_msg) {
    // Left
    if (prev_station < curr_station) {
        return &(s->left); 
        // Right
    } else if (prev_station > curr_station) {
        return &(s->right);
        // This is basically an assert...
    } else {
#if DEBUG_FILE_OUTPUT
        fprintf(node_out_file, "Invalid matching stations: curr: %d prev: %d, state %d\n", curr_station, prev_station, in_msg->type);
        fflush(node_out_file);
#endif
        exit(-1);
    }
}

// Queue management 
int add_train(tw_lpid new_train, track_t* track) {
    // Add a new train -- find the first nonzero spot
    // if queued_tu_present is 0, add to 0
    // if 1, it means 1 train already here, add to 1
    int index = track->queued_tu_present;
    int i=0;

    // Sanity check that it hasn't filled up
    if (index >= QUEUE_LEN - 1) {
#if DEBUG_FILE_OUTPUT
        fprintf(node_out_file, "Station queue exceeded!\n");
        fprintf(node_out_file, "Dumping queue: ");
        for (i=0; i < QUEUE_LEN; i++) {
            fprintf(node_out_file, "%lu ", track->queued_tu[i]);
        }
        fprintf(node_out_file, "\n");
        fflush(node_out_file);
#endif
        tw_error(TW_LOC, "Station queue exceeded!");
    }

    // Add it in
    track->queued_tu[index] = new_train;
    track->queued_tu_present += 1;

    /// Return where we put it
    return index;
}
int pop_head(track_t* track) {
    // Grab the guy at the head
    tw_lpid curr_train = track->queued_tu[0];

    // Scoot everybody down
    for (int i=0; i < (QUEUE_LEN - 1); i++) {
        track->queued_tu[i] = track->queued_tu[i+1];
    }
    
    // Decrease the queued count
    track->queued_tu_present -= 1;

    // Return that guy we got from the front
    return curr_train;
}
int add_train_head(tw_lpid new_train, track_t* track) {
    // Here we need to add somebody back to the head

    // First, scoot everyone else down
    for (int i=1; i < (QUEUE_LEN - 1); i++) {
        track->queued_tu[i] = track->queued_tu[i+1];
    }

    // Put the new one in the right spot
    track->queued_tu[0] = new_train;
    // Bump up the count
    track->queued_tu_present += 1;

    return 0;
}
int pop_tail(track_t* track) {
    // Just chop the guy off the back

    int curr_index = track->queued_tu_present - 1;
    tw_lpid curr_tail = track->queued_tu[curr_index]; 

    // Clear out that tail
    track->queued_tu[curr_index] = 0;
    // decrement the number present
    track->queued_tu_present -= 1;

    return curr_tail;
}
