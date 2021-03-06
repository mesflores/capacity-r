// transit_unit.c
//
// Functions for the transit_unit LP

//Includes
#include <math.h>
#include <stdio.h>

#include "ross.h"
#include "passenger.h"
#include "model.h"
#include "graph.h"
#include "transit_unit.h"
#include "route.h"
#include "utils.h"


/*
 * initial_approach - Send a message to the first station in the route
 */
int initial_approach(tu_state *s, tw_lp *lp, int init) {
    int self = lp->gid;
    float start_time;

    start_time = fmaxf((float)(s->start) - tw_now(lp), CONTROL_EPOCH);

    // Send an approach message to the first station on the schedule
#if DEBUG_FILE_OUTPUT
    fprintf(node_out_file, "[TU %d] TU sending TRAIN_ARRIVE to %d for %2.2f at %2.2f\n", self, s->route->origin, start_time, tw_now(lp));
    fflush(node_out_file);
#endif

    if (init == 0) {
        tw_event *e = tw_event_new(s->route->origin, (float)(s->start) - tw_now(lp), lp);
        message *msg = tw_event_data(e);
        msg->type = TRAIN_ARRIVE;
        msg->source = self;
        msg->prev_station = s->prev_station;
        tw_event_send(e);
    } else {

        //tw_output(lp, "\n[%.3f] TU %d: Starting new route at %d at %d\n", tw_now(lp), self, s->route->origin, s->start);
        tw_event *e = tw_event_new(s->route->origin, start_time, lp);
        message *msg = tw_event_data(e);
        msg->type = TRAIN_ARRIVE ;
        msg->source = self;
        msg->prev_station = s->prev_station;
        tw_event_send(e);
    }


    // Update your own state
    s->curr_state = TU_APPROACH;

    return 0;
}

/*
 * advance_route - check to see if there is another route in the list. If so,
 * go ahead and reset the state for the new route
 */
int advance_route(tu_state *s, tw_lp *lp) {
    int self = lp->gid;
    // If this is the first time through, grab it from the list
    if (s->route == NULL) {
        s->route = get_route(self);

    } else {
        // If the next one is null, dont change it
        if (s->route->next_route == NULL) {
            // Still set the route index to 0, we can undo it if we need
            s->route_index = 0;
            return 1;
        }
        // Bump the route to the next one in the list
        s->route = s->route->next_route;
    }
    // Reset the state for the new route
    s->curr_state = TU_IDLE;
    s->start = s->route->start_time - g_time_offset;
    // Clear out the passengers
    s->pass_list = NULL;
    s->pass_count = 0;

    // For the first stop, we have to give it a direction, to avoid
    // pathological queuing, so just give it the direction that matches where
    // it's going by making a fake previous
    if (s->route->start_dir > 0) {
        s->prev_station = s->route->origin - 1;
    } else {
        s->prev_station = s->route->origin + 1;
    }
    s->station = s->prev_station; // just set the the same
    s->route_index = 0;

    return 0;
}



//Init function
// - called once for each LP
// ! LP can only send messages to itself during init !
void transit_unit_init (tu_state *s, tw_lp *lp) {
    int self = lp->gid;

    // Do we want to init this even?
    if (self >= (g_num_stations + g_num_transit_units)) {
        // It's not a real train, do nothing
        //
        // Not sure if this is the best way to handle this, but basically,
        // since we need an equal number of LPs per PE, this just fills it out.
        return;
    }


    // Go ahead and init the route
    s->route = NULL;

    s->delayed = 0;
    s->completed = 0;

    advance_route(s, lp);

    return;
}

//Pre-run handler
void transit_unit_pre_run (tu_state *s, tw_lp *lp) {
    int self = lp->gid;

    // Need to do this again, or the pre run handler will eat it
    if (self >= (g_num_stations + g_num_transit_units)) {
        // Not sure if this is the best way to handle this, but basically,
        // since we need an equal number of LPs per PE, this just fills it out.
        return;
    }

    initial_approach(s, lp, 0);

    return;
}


//Forward event handler
void transit_unit_event (tu_state *s, tw_bf *bf, message *in_msg, tw_lp *lp) {
    int self = lp->gid;
    int dest;
    int curr_global;

    int delay;
    int current_index;
    long int next_station;
    int next_time;
    char curr_station;


    // initialize the bit field
    *(int *) bf = (int) 0;

    // handle the message
    switch (in_msg->type) {
        case ST_ACK : {
            // A station said we are good to go!
            //tw_output(lp,
            //          "\n[%.3f] TU %d: ACK received from %s!\n",
            //          tw_now(lp),
            //          self,
            //          sta_name_lookup(in_msg->source));
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] Received ack from: %ld\n",
                    self,
                    in_msg->source);
            fflush(node_out_file);
#endif

            if (s->curr_state != TU_APPROACH) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Spurious Ack from: %ld (wrong state!),"
                            " suspending!\n",
                        self,
                        in_msg->source);
                fflush(node_out_file);
#endif
                tw_lp_suspend(lp, 0, 0);
                return;
            }

            // Was it the station we were expecting to hear from?
            current_index = s->route_index - 1;
            next_station = get_next(s->route, &(current_index));

            if (next_station != in_msg->source) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Spurious Ack from: %ld (expected %ld),"
                            "suspending!\n",
                        self,
                        in_msg->source,
                        next_station);
                fflush(node_out_file);
#endif
                tw_lp_suspend(lp, 0, 0);
                return;
            }

            // Go ahead and put yourself in alight mode
            s->curr_state = TU_ALIGHT;
            // Save the previous station
            //s->prev_station = s->station;
            SWAP_UL(&(s->prev_station), &(s->station));
            //s->station = in_msg->source;
            SWAP_UL(&(s->station), &(in_msg->source));

            //s->min_time = in_msg->next_arrival;
            SWAP(&(s->min_time), &(in_msg->next_arrival));


            // TODO: Send messages to station to empty passengers

            // For now, no passengers go right to boarding
            tw_event *e = tw_event_new(s->station, CONTROL_EPOCH, lp);
            message *msg = tw_event_data(e);
            msg->type = TRAIN_BOARD;
            msg->prev_station = s->prev_station;
            msg->source = self;
            //tw_output(lp,
            //          "[%.3f] TU %d: Sending alighting complete to %s!\n",
            //          tw_now(lp),
            //          self,
            //          sta_name_lookup(s->station));
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] Sending TRAIN_BOARD to %ld\n",
                    self,
                    s->station);
            fflush(node_out_file);
#endif
            tw_event_send(e);

            // Train is now accepting boarding passengers
            s->curr_state = TU_BOARD;

            break;
        }
        case P_COMPLETE : {

            // station is all done boarding
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] TU P_COMPLETE: Got complete from %lu\n",
                    self,
                    in_msg->source);
            fflush(node_out_file);
#endif

            /***** State Machine Sanity Checks *****/
            // Was this TU in the right state?
            if (s->curr_state != TU_BOARD) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Spurious P_COMPLETE from: %ld (bad state!)"
                            " suspending!\n",
                        self,
                        in_msg->source);
                fflush(node_out_file);
#endif
                tw_lp_suspend(lp, 0, 1);
                return;
            }

            // Was that station supposed to be talking to us?
            if (in_msg->source != s->station) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Spurious P_COMPLETE from: %ld (expected %ld)"
                            " suspending!\n",
                        self,
                        in_msg->source,
                        s->station);
                fflush(node_out_file);
#endif
                tw_lp_suspend(lp, 0, 1);
                return;
            }
            /***************************************/


#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] Current route index: %d(/%d)\n",
                    self,
                    s->route_index,
                    s->route->length-1);
            fflush(node_out_file);
#endif

            // Ask what the next station is, and when we are expected to get there
            next_station = get_next(s->route, &(s->route_index));
            next_time = get_next_time(s->route, &(s->route_index));

            // Did we have a next station?
            if (next_station != -1) {
                // How far away is the next station actually
                delay = get_delay_id(in_msg->source, next_station);
                // Do we need to leave now to get there, given the current
                // time and our scheduled arrival?
                if ((next_time - g_time_offset) > tw_now(lp) + delay) {
#if DEBUG_FILE_OUTPUT
                    fprintf(node_out_file,
                        "[TU %d] Pausing for delay (%2.2f seconds)!\n",
                        self,
                        ((next_time - g_time_offset) - tw_now(lp) - delay));
                    fflush(node_out_file);
#endif

                    // In this case we need to wait here for the difference
                    SWAP_SHORT(&(s->delayed), &(in_msg->delayed));
                    s->delayed = 1;
                    tw_event *e = tw_event_new(s->station,
                                               (next_time - g_time_offset) - tw_now(lp) - delay,
                                               lp);
                    message *msg = tw_event_data(e);
                    msg->type = TRAIN_BOARD;
                    msg->prev_station = s->prev_station;
                    msg->source = self;
                    tw_event_send(e);

                    // That's it, bail out of everything
                    break;
                }
                // Else, just proceed as normal, we need to leave now

                //  Stash whatever our status was before in the incoming message and reset
                // the delayed flag to 0, since at this point, we decided to leave
                SWAP_SHORT(&(s->delayed), &(in_msg->delayed));
                s->delayed = 0;
            } // Else, we are actually at the last stop, so nothing special to do for delay

            // Ok tell the next station that we are on our way
            s->curr_state = TU_APPROACH;

            // Bump the routeindex
            s->route_index += 1;

            if (next_station != -1) {
                // Time it takes to get to the next station
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] TU P_COMPLETE: Looking up delay from %lu to"
                            " %ld\n",
                        self,
                        in_msg->source,
                        next_station);
                fflush(node_out_file);
#endif

                delay = get_delay_id(in_msg->source, next_station);
                // Actually its possible somebody ahead of us was delayed, check the min time
                if (delay < (s->min_time - tw_now(lp))) {
                    delay = s->min_time - tw_now(lp) + 1; // TODO: Controllable
                    //tw_output(lp,
                    //          "[%.3f] TU %d: Incurred cascading delay!\n",
                    //          tw_now(lp),
                    //          self);
                }
            }

            // Ok all done go ahead and depart
            tw_event *e = tw_event_new(in_msg->source, CONTROL_EPOCH, lp);
            message *msg = tw_event_data(e);
            msg->type = TRAIN_DEPART;
            msg->source = self;
            msg->prev_station = s->prev_station;
            // If we arent going anywhere just 0 this
            if (next_station == -1) {
                msg->next_arrival = 0;
            }
            else {
                msg->next_arrival = tw_now(lp) + delay;
            }
            //tw_output(lp,
            //          "\n[%.3f] TU %d: Sending depart to %s!\n",
            //          tw_now(lp),
            //          self,
            //          sta_name_lookup(in_msg->source));
            tw_event_send(e);

            // Actually we were at the end of the route
            if (next_station == -1) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file, "[TU %d] Finished route!\n", self);
                fflush(node_out_file);
#endif

                // We do need to tell the last station that we left
                tw_output(lp,
                          //"\n[%.3f] TU %d:Route ending at STA %s!\n",
                          "\n[%.3f] %d %s FINISH %.3f\n", // <TU>, <Station>, FINISH, <schedule_error>
                          tw_now(lp),
                          self,
                          sta_name_lookup(in_msg->source),
                          tw_now(lp) - (s->route->end_time - g_time_offset)
                          );
                if ((tw_now(lp) - (s->route->end_time - g_time_offset)) < 0.0) {
                    tw_output(lp, "FINISH UNDERRUN: %d\n", s->route->end_time);
                }
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] route index %d\n", self, s->route_index);
                fprintf(node_out_file,
                        "[TU %d] station %lu\n", self, s->station);
                fprintf(node_out_file,
                        "[TU %d] prev_station %lu\n", self, s->prev_station);
                fprintf(node_out_file,
                        "[TU %d] delay status %d\n", self, s->delayed);
                fflush(node_out_file);
#endif

                if (advance_route(s, lp) == 0) {
                    tw_output(lp,
                              "\n[%.3f] TU %d: Starting fresh route!\n",
                              tw_now(lp),
                              self);
                    initial_approach(s, lp, 1);
                } else {
                    // End of the run for this LP, let us know
                    s->completed = 1; // Mark it as done
#if DEBUG_FILE_OUTPUT
                    fprintf(node_out_file, "[TU %d] Completed final run!\n", self);
                    fflush(node_out_file);
#endif
                    tw_output(lp,
                              "\n[%.3f] TU %d: Completed final run!\n",
                              tw_now(lp),
                              self);
                }


                break;
            }

            // Not at that station anymore
            // We actually want to use this, not going to zero it out yet
            //s->station = -1;

            // Send it an approach message
            tw_event *approach = tw_event_new(next_station, delay, lp);
            message *next_msg = tw_event_data(approach);
            next_msg->type = TRAIN_ARRIVE;
            next_msg->source = self;
            next_msg->prev_station = s->station;
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] TU sending TRAIN_ARRIVE to %lu with prev %lu\n",
                    self,
                    next_station,
                    s->station);
            fflush(node_out_file);
#endif
            //tw_output(lp,
            //          "[%.3f] TU %d: Sending approach to %s!\n",
            //          tw_now(lp),
            //          self,
            //          sta_name_lookup(next_station));
            tw_event_send(approach);
            break;
        }
        case P_BOARD : {
            // station is all done boarding
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] TU P_BOARD: Got board from %lu\n",
                    self,
                    in_msg->source);
            fflush(node_out_file);
#endif
            /****** XXX This should be disabled, as passengers are not generated atm *****/
            // Passenger!
            passenger_t* new_pass = tw_calloc(TW_LOC,
                                              "create_passenger",
                                              sizeof(passenger_t),
                                              1);
            // Copy it in
            memcpy(new_pass, &(in_msg->curr_pass), sizeof(passenger_t));
            // Stick it in the list
            new_pass->next = s->pass_list;
            s->pass_list = new_pass;
            s->pass_count += 1;
            //tw_output(lp,
            //          "[%.3f] TU %d: Passenger boarded train %d!\n",
            //          tw_now(lp),
            //          self,
            //          self);

            // Have it continue boarding.
            // TODO: Probably we could do this optimistically instead of explicit acks...
            tw_event *e = tw_event_new(in_msg->source, CONTROL_EPOCH, lp);
            message *msg = tw_event_data(e);
            msg->type = TRAIN_BOARD;
            msg->source = self;
            msg->prev_station = s->prev_station;
            //tw_output(lp,
            //          "[%.3f] TU %d: Continue boarding to %s!\n",
            //          tw_now(lp),
            //          self,
            //          sta_name_lookup(in_msg->source));
            tw_event_send(e);
            break;
        }
        default :
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d]: Unhandeled forward message type %d\n",
                    self,
                    in_msg->type);
#endif
            break;
    }


    return;
}


//Reverse Event Handler
void transit_unit_event_reverse (tu_state *s, tw_bf *bf, message *in_msg, tw_lp *lp) {
    int self = lp->gid;
    switch (in_msg->type) {
        case ST_ACK : {
#if DEBUG_FILE_OUTPUT
            // Print from the station, since we swapped it in here
            fprintf(node_out_file,
                    "[TU %d] Undoing an ack from %lu\n",
                    self,
                    s->station);
            fflush(node_out_file);
#endif
            //reset to approach
            s->curr_state = TU_APPROACH;

            // Reset the previous to whatever it was
            SWAP_UL(&(s->station), &(in_msg->source));
            // Reset the curr to the previous
            SWAP_UL(&(s->prev_station), &(s->station));

            //Reset the min time
            SWAP(&(s->min_time), &(in_msg->next_arrival));

            break;
        }
        case P_COMPLETE : {
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] Undoing P_COMPLETE from %lu\n",
                    self,
                    in_msg->source);
            fflush(node_out_file);
#endif

            // Did we delay here?
            if (s->delayed == 1) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Nothing to do here, just unset the delay flag!\n",
                        self);
                fflush(node_out_file);
#endif
                // In this case, nothing to do but to unset the delay flag
                SWAP_SHORT(&(s->delayed), &(in_msg->delayed));
                break;
            }

            // First, we need to check if our route index is 0, if so, we
            // must roll back to the previous
            if (s->route_index == 0) {
#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] Undoing roll over to next route!\n",
                        self);
                fflush(node_out_file);
#endif
                // If this was the last route, our route is already set
                if (s->completed) {
                    s->completed = 0;
                }
                else {
                    // Scoot back to the previous route
                    s->route = s->route->prev_route;
                }
                s->route_index = s->route->length;
                s->station = s->route->route[s->route->length - 1];
                s->prev_station = s->route->route[s->route->length - 2];
                // Passenger features stay blank since the train empties at last stop
                s->start = s->route->start_time - g_time_offset;

#if DEBUG_FILE_OUTPUT
                fprintf(node_out_file,
                        "[TU %d] route index %d\n", self, s->route_index);
                fprintf(node_out_file,
                        "[TU %d] station %lu\n", self, s->station);
                fprintf(node_out_file,
                        "[TU %d] prev_station %lu\n", self, s->prev_station);
                fprintf(node_out_file,
                        "[TU %d] start %d\n", self, s->start);
                fprintf(node_out_file,
                        "[TU %d] After rollover undo delayed is: %d\n", self, s->delayed);
                fflush(node_out_file);
#endif
            } else {
                //Reset the delay, if we hadn't been at the end
                SWAP_SHORT(&(s->delayed), &(in_msg->delayed));
            }

            s->route_index -= 1;
            s->curr_state = TU_BOARD;

            break;
        }
        case P_BOARD : {
            // TODO: Currently disabled!
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "[TU %d] Undoing P_BOARD from %lu\n",
                    self,
                    in_msg->source);
            fflush(node_out_file);
#endif
            break;
        }
        default :
#if DEBUG_FILE_OUTPUT
            fprintf(node_out_file,
                    "TU %d: Unhandeled reverse message type %d\n",
                    self,
                    in_msg->type);
            fflush(node_out_file);
#endif
            break;
    }
    return;
}

//report any final statistics for this LP
void transit_unit_final (tu_state *s, tw_lp *lp){
    return;
}

