#pragma once

typedef enum{
    READY = 0,
    BLOCKED,
    RUNNING,
    FINISHED
} State;

const char* state2string(State s) {
    switch(s) 
    {
        case READY:    return "READY";
        case RUNNING:  return "RUNNING";
        case BLOCKED:  return "BLOCKED";
        case FINISHED: return "FINISHED";
        default:       return "UNKNOWN";
    }
}