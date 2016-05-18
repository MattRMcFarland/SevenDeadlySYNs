// common/constants.h
//
// defines constants for communication between tracker and peers

#ifndef _CONSTANTS_H
#define _CONSTANTS_H

// new clients connect here on tracker
#define TRACKER_LISTENING_PORT 5969 

// clients listen here for connections from other clients
#define CLIENT_LISTENING_PORT 7681

// defined DART_SYNC directory that will be syncronized across peers
#define DARTSYNC_DIR "~/dart_sync/"

#endif // _CONSTANTS_H