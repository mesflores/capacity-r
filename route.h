 // route.h
 // the route objects

#ifndef _route_h
#define _route_h


typedef struct route_t {
    int* route; // The set of stations themselves
    int* delay;
    int length; // The length of the route

    // Some more meta information
    int origin;
    int terminal;
} route_t;

route_t* init_route(int* steps, int* delays, int len);
int get_next(route_t* route_obj, int current);



#endif
