=======
Carteur
=======

The Unstoppable State Machine
=============================

Generate C code from a state machine description. This is motivated by my
profound hatred of programming user interfaces, which mostly involves writing
boilerplate code and recheck everything each time one wants to change
something. Currently, from a file `test.ini` ::

    state = a
    state = b
    state = c
    transition = a,b,event1,callback1
    transition = a ,  c,event2    ,callback2

doing the commands (from the project root directory) ::

    make
    ./truc test.ini

would generate a file `truc.c` containing ::

    #include "truc.h"

    void handle_event1(void)
    {
	    switch (...) {
	    case a:
		    break ;
	    case b:
		    break ;
	    case c:
		    break ;
	    default:
		    // Impossible.
	    }
    }
    
    void handle_event2(void)
    {
	    switch (...) {
	    case a:
		    break ;
	    case b:
		    break ;
	    case c:
		    break ;
	    default:
		    // Impossible.
	    }
    }


It is, of course, a WIP project. While it would at first seem to be dedicated
to UI programming, any state machine code could potentially be generated in
the same fashion.

