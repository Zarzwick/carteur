=======
Carteur
=======

The Unstoppable State Machine
=============================

Generate C code from a state machine description. This is motivated by my
profound hatred of programming user interfaces, which mostly involves writing
boilerplate code and recheck everything each time one wants to change
something. Currently, from a file `test.ini` ::

    # Options for the generator.
    machine_name = machine
    declare_states = true
    declare_events = true
    states_enum_name = state_t
    events_enum_name = event_t
    
    # Declare some states (vertices).
    state = A
    state = B
    state = C
    state = D
    
    # Special properties of the graph (TODO).
    #start = A
    #end = D
    
    # Declare some transitions (edges).
    transition = A, B, tell_A_to_go_B, go_from_A_to_B
    transition = A, C, tell_A_to_go_C, go_from_A_to_C
    transition = B, C, tell_B_to_go_C, go_from_B_to_C
    transition = B, D, tell_to_go_D, go_from_B_to_D
    transition = C, D, tell_to_go_D, go_from_C_to_D

and issuing the commands (from the project root directory) ::

    make
    ./carteur test.ini

would generate a file `generated_machine.c` containing ::

    #include "generated_machine.h"
    
    void machine_handle_tell_A_to_go_B(state_t* state, void* user) {
        switch (*state) {
        case A:
            go_from_A_to_B(user)
            *state = B
            break
        default:
            // IMPOSSIBLE
        }
    }
    
    void machine_handle_tell_A_to_go_C(state_t* state, void* user) {
        switch (*state) {
        case A:
            go_from_A_to_C(user)
            *state = C
            break
        default:
            // IMPOSSIBLE
        }
    }
    
    void machine_handle_tell_B_to_go_C(state_t* state, void* user) {
        switch (*state) {
        case B:
            go_from_B_to_C(user)
            *state = C
            break
        default:
            // IMPOSSIBLE
        }
    }
        
    void machine_handle_tell_to_go_D(state_t* state, void* user) {
        switch (*state) {
        case B:
            go_from_B_to_D(user)
            *state = D
            break
        case C:
            go_from_C_to_D(user)
            *state = D
            break
        default:
            // IMPOSSIBLE
        }
    }

It is, of course, a WIP project. While it would at first seem to be dedicated
to UI programming, any state machine code could potentially be generated in
the same fashion.

