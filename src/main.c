//
// This is part of the source code of the carteur state machine generator.
// The code is under the BSD-3 license (see LICENSE).
//
// Hugo RENS <hugo.rens@univ-tlse3.fr>
//

// This triggers the construction and use of an inet graph to perform common
// or less common analysis, and, eventually, optimisations. UNSUPPORTED
//#define CARTEUR_GRAPH_ANALYSIS

#include <m-lib/m-string.h>
#include <m-lib/m-array.h>
#include <m-lib/m-dict.h>

#include <ini.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#if defined(CARTEUR_GRAPH_ANALYSIS)
#error "Graph analysis is yet unsupported."
// See also this gomp bug introducing a memory leak in valgrind...
#include <igraph/igraph.h>
#endif

//
// The program operates in 3 stages. Stage 1 is obviously parsing ; the
// maintained structures involve a mapping from strings to integers, in
// order to build the graph easily. Stage 2 is dedicated to transforming
// (if enabled) the graph. During this stage, the mappings from strings to
// integers are also reversed, so that stage 3 (generation) can be able to
// query strings in from corresponding indices in the graph. This last stage
// is meant to generate code (this stage is done by languages backend).
//

///
/// One transition from some state to another state, triggered by an event
/// and calling a callback.
///
typedef struct transition_t {
    size_t from ;
    size_t to ;
    size_t event ;
    size_t callback ;
} transition_t[1] ;

// NOTE : I tend to avoid using this [1] "trick" myself when I can avoid it.
// However it makes life much easier when used along with M* lib.

void transition_init(transition_t t) { t->from = 0ul ; t->to = 0ul ; t->event = 0ul; t->callback = 0ul ; }
void transition_set(transition_t transition, const transition_t model) {}
void transition_init_set(transition_t transition, const transition_t model) { *transition = *model ; }
void transition_clear(transition_t transition) {}
int transition_cmp(transition_t a, transition_t b) { return a->event - b->event ; }
void transition_swap(transition_t a, transition_t b) { transition_t temp ; *temp = *a ; *a = *b ; *b = *temp ; }

#define M_OPL_transition_t() (INIT(transition_init), SET(transition_set), INIT_SET(transition_init_set), CLEAR(transition_clear), CMP(transition_cmp), SWAP(transition_swap))

ARRAY_DEF(arraylist_transition, transition_t)
ARRAY_DEF(arraylist_string, string_t, STRING_OPLIST)
DICT_DEF2(dict_string, string_t, STRING_OPLIST, size_t, M_DEFAULT_OPLIST)

// ................................................................... STAGE 1

///
/// Information at stage 1. This contains storage for the states, events
/// and callback names, and a dynamic array of transitions.
///
typedef struct parsing_stage_data_t {
    //igraph_t graph ;
    dict_string_t states ;
    dict_string_t events ;
    dict_string_t callbacks ;
    arraylist_transition_t transitions ;
} parsing_stage_data_t ;

///
/// [a-zA-Z0-9_] FIXME Use a real lexer instead...
///
static bool character_in_identifier(const char c)
{
    return ((c >= 'a') && (c <= 'z'))
        || ((c >= 'A') && (c <= 'Z'))
        || ((c >= '0') && (c <= '9'))
        || (c == '_') ;
}

///
/// Parse a sequence of identifiers of the form [a-zA-Z0-9_]+, and call the
/// provided callback for each token.
///
static void parse_identifers
    ( const char* str
    , const size_t len
    , void (*callback)(const char* token, void* userdata)
    , void* userdata
    )
{
    char* copy = malloc((len+1)*sizeof(char)) ;
    strncpy(copy, str, len+1) ;
    size_t i = 0 ;
    size_t start = i ;
    size_t end = start ;
    bool on_identifier = false ;
    for (; i <= len; ++ i) {
        if (character_in_identifier(copy[i])) {
            if (! on_identifier) {
                start = i ;
            }
            on_identifier = true ;
            ++ end ;
        } else {
            copy[i] = '\0' ;
            if (on_identifier) {
                on_identifier = false ;
                callback(&copy[start], userdata) ;
            }
            ++ start ;
            ++ end ;
        }
    }
    free(copy) ;
}

enum uncomplete_transition_field {
    CARTEUR_TRANSITION_FIELD_FROM = 0,
    CARTEUR_TRANSITION_FIELD_TO = 1,
    CARTEUR_TRANSITION_FIELD_EVENT = 2,
    CARTEUR_TRANSITION_FIELD_CALLBACK = 3,
} ;

typedef struct parser_transition_handler_data_t {
    struct transition_t* transition ;
    enum uncomplete_transition_field current_field ;
    parsing_stage_data_t* machine ;
} parser_transition_handler_data_t ;

///
/// Fill an uncomplete transition with the given token.
///
void parser_transition_handler
    ( const char* token
    , void* userdata
    )
{
    size_t temp ;
    size_t* pos ;
    parser_transition_handler_data_t* t =
        (parser_transition_handler_data_t*) userdata ;
    string_t name ; // string_t version of token.
    string_init_set_str(name, token) ;
    switch (t->current_field) {
        case CARTEUR_TRANSITION_FIELD_FROM:
            pos = dict_string_get(t->machine->states, name) ;
            if (pos == NULL) {
                // FIXME Improve error handling.
                fprintf(stderr, "Reference to unknown state '%s'.\n", token) ;
                exit(4) ;
            }
            //printf("From: %s (state #%zu)\n", token, *pos) ;
            t->current_field = CARTEUR_TRANSITION_FIELD_TO ;
            t->transition->from = *pos ;
            break ;
        case CARTEUR_TRANSITION_FIELD_TO:
            pos = dict_string_get(t->machine->states, name) ;
            if (pos == NULL) {
                // FIXME Improve error handling.
                fprintf(stderr, "Reference to unknown state '%s'.\n", token) ;
                exit(4) ;
            }
            //printf("To: %s (state #%zu)\n", token, *pos) ;
            t->current_field = CARTEUR_TRANSITION_FIELD_EVENT ;
            t->transition->to = *pos ;
            break ;
        case CARTEUR_TRANSITION_FIELD_EVENT:
            pos = dict_string_get(t->machine->events, name) ;
            if (pos == NULL) {
                size_t n = dict_string_size(t->machine->events) ;
                dict_string_set_at(t->machine->events, name, n) ;
                temp = n ;
                pos = &temp ;
            }
            //printf("Event: %s\n", token) ;
            t->current_field = CARTEUR_TRANSITION_FIELD_CALLBACK ;
            t->transition->event = *pos ;
            break ;
        case CARTEUR_TRANSITION_FIELD_CALLBACK:
            pos = dict_string_get(t->machine->callbacks, name) ;
            if (pos == NULL) {
                size_t n = dict_string_size(t->machine->callbacks) ;
                dict_string_set_at(t->machine->callbacks, name, n) ;
                temp = n ;
                pos = &temp ;
            }
            //printf("Callback: %s\n", token) ;
            t->current_field = CARTEUR_TRANSITION_FIELD_FROM ;
            t->transition->callback = *pos ;
            break ;
    }
}

///
/// General handler for the INI parser. This will distinguish names 'state'
/// and 'transition'. On name 'state' it will add a state to the dictionnary.
/// On name 'transition', it will check that the states exist, and, if they
/// do, find or register the event and callback.
///
static int parser_handler
    ( void* user
    , const char* section
    , const char* name
    , const char* value
    )
{
    transition_t transition ;
    parsing_stage_data_t* machine = (parsing_stage_data_t*) user ;
    parser_transition_handler_data_t todo =
        { .transition = &transition[0]
        , .current_field = CARTEUR_TRANSITION_FIELD_FROM
        , .machine = machine
        } ;

    // Add state.
    if (strcmp(name, "state") == 0) {
        string_t name ;
        string_init_set_str(name, value) ;
        const size_t n = dict_string_size(machine->states) ;
        dict_string_set_at(machine->states, name, n) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
#endif
    }

    // Add transition.
    else if (strcmp(name, "transition") == 0) {
        parse_identifers(value, strlen(value), parser_transition_handler, &todo) ;
        arraylist_transition_push_back(machine->transitions, transition) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
        //igraph_add_edge(graph, from, to).
#endif
    }

    // Unknown name, error in the file.
    else {
        return 0 ;  /* unknown section/name, error */
    }

    return 1 ;
}

// ................................................................... STAGE 2

///
/// Contains dull stores for states, events and callbacks names. This time,
/// the mapping is from integer to string.
///
/// This is for stage 3, but the goal of stage 2 is precisely to change the
/// representation from stage 1 to the one in stage 3.
///
typedef struct generation_stage_data_t {
    arraylist_string_t states ;
    arraylist_string_t events ;
    arraylist_string_t callbacks ;
    arraylist_transition_t transitions ;
} generation_stage_data_t ;

///
/// Turn a mapping (string -> integer) into an array of strings, such that
/// the mapping is reversed.
///
void fill_array_from_dictionnary
    ( arraylist_string_t dest
    , const dict_string_t src
    )
{
    const size_t n = dict_string_size(src) ;
    arraylist_string_init(dest) ;
    arraylist_string_resize(dest, n) ;
    dict_string_it_t it ;
    dict_string_it(it, src) ;
    for (size_t i = 0; i < n; ++ i) {
        const dict_string_itref_t* ref = dict_string_cref(it) ;
        arraylist_string_set_at(dest, ref->value, ref->key) ;
        dict_string_next(it) ;
    }
}

///
/// General function that embodies the whole second stage. It turns maps into
/// arrays for stage 3, and possibly perform graph analysis.
///
void transform_graph
    ( parsing_stage_data_t* parsing
    , generation_stage_data_t* generation
    )
{
    fill_array_from_dictionnary(generation->states, parsing->states) ;
    fill_array_from_dictionnary(generation->events, parsing->events) ;
    fill_array_from_dictionnary(generation->callbacks, parsing->callbacks) ;
    arraylist_transition_init_move(generation->transitions, parsing->transitions) ;
    // This seems to trigger a fault when used on a 2-sized array. See l.814 of m-array.h FIXME...
    arraylist_transition_special_stable_sort(generation->transitions) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
    // TODO
#endif
}

// ................................................................... STAGE 3

void generate_C_header(FILE* file, generation_stage_data_t* stage_data) {
    fprintf(file, "// File generated by carteur.\n\n") ;
    fprintf(file, "#pragma once\n\n") ;
    // Emit one signature per event.
    const size_t n_events = arraylist_string_size(stage_data->events) ;
    for (size_t ev = 0; ev < n_events; ++ ev) {
        string_t* ev_name = arraylist_string_get(stage_data->events, ev) ;
        fprintf(file, "void handle_%s(void) ;\n", string_get_cstr(*ev_name)) ;
    }
    fprintf(file, "\n") ;
}

void generate_C_source(FILE* file, generation_stage_data_t* stage_data) {
    fprintf(file, "#include \"truc.h\"\n\n") ;
    arraylist_transition_it_t it ;
    arraylist_transition_it(it, stage_data->transitions) ;
    bool transitions_available = ! arraylist_transition_end_p(it) ;
    // Consume all transitions. The outer loop handles event changes, and thus
    // generates the head and bottom of each handler function. The inner loop
    // consumes all transitions related to the same event (hence the order
    // requirement) to produce the different cases.
    while (transitions_available) {
        const transition_t* ref = arraylist_transition_cref(it) ;
        const size_t current_event = (*ref)->event ;
        string_t* event = arraylist_string_get(stage_data->events, (*ref)->event) ;
        string_t* callback = NULL ;
        string_t* from     = NULL ;
        string_t* to       = NULL ;
        // Emit signature.
        fprintf(file, "void handle_%s(void) {\n", string_get_cstr(*event)) ;
        // Emit cases.
        bool same_event = true ;
        while (same_event) {
            // Get names for the current transition t.
            callback = arraylist_string_get(stage_data->callbacks, (*ref)->callback) ;
            from = arraylist_string_get(stage_data->states, (*ref)->from) ;
            to   = arraylist_string_get(stage_data->states, (*ref)->to) ;
            fprintf(file, "\tcase %s:\n", string_get_cstr(*from)) ;
            fprintf(file, "\t\t// call callback %s\n", string_get_cstr(*callback)) ;
            fprintf(file, "\t\t// new state = %s\n", string_get_cstr(*to)) ;
            fprintf(file, "\t\tbreak\n") ;
            // Decide if the event will be the same or not.
            arraylist_transition_next(it) ;
            if (arraylist_transition_end_p(it)) {
                transitions_available = false ;
                break ;
            }
            ref = arraylist_transition_cref(it) ;
            same_event = (current_event == (*ref)->event) ;
        }
        fprintf(file, "}\n\n") ;
    }
}

void generate_C(generation_stage_data_t* stage_data) {
    FILE* h_file = fopen("truc.h", "w") ;
    FILE* c_file = fopen("truc.c", "w") ;
    generate_C_header(h_file, stage_data) ;
    generate_C_source(c_file, stage_data) ;
    fclose(h_file) ;
    fclose(c_file) ;
}

// ...................................................................... MAIN

int main()
{
    //int err ;
    parsing_stage_data_t parsing_stage_data ;

#if defined(CARTEUR_GRAPH_ANALYSIS)
    err = igraph_empty(&machine.graph, 0, true) ;
    if (err != IGRAPH_SUCCESS) {
        fprintf(stderr, "Failed to initialise graph.\n") ;
        return 1 ;
    }
#endif

    dict_string_init(parsing_stage_data.states) ;
    dict_string_init(parsing_stage_data.events) ;
    dict_string_init(parsing_stage_data.callbacks) ;
    arraylist_transition_init(parsing_stage_data.transitions) ;
    
    // Stage 1 - Parsing.
    ini_parse("test.ini", parser_handler, &parsing_stage_data) ;

    /*
    dict_string_it_t it ;
    printf("States :\n") ;
    dict_string_it(it, parsing_stage_data.states) ;
    while (! dict_string_end_p(it)) {
        const dict_string_itref_t* ref = dict_string_cref(it) ;
        printf("(%zu) %s\n", ref->value, string_get_cstr(ref->key)) ;
        dict_string_next(it) ;
    }
    printf("\nEvents :\n") ;
    dict_string_it(it, parsing_stage_data.events) ;
    while (! dict_string_end_p(it)) {
        const dict_string_itref_t* ref = dict_string_cref(it) ;
        printf("(%zu) %s\n", ref->value, string_get_cstr(ref->key)) ;
        dict_string_next(it) ;
    }
    printf("\nCallbacks :\n") ;
    dict_string_it(it, parsing_stage_data.callbacks) ;
    while (! dict_string_end_p(it)) {
        const dict_string_itref_t* ref = dict_string_cref(it) ;
        printf("(%zu) %s\n", ref->value, string_get_cstr(ref->key)) ;
        dict_string_next(it) ;
    }
    printf("\nTransitions :\n") ;
    const size_t n_transitions =
        arraylist_transition_size(parsing_stage_data.transitions) ;
    for (size_t i = 0; i < n_transitions; ++ i) {
        transition_t* ref = arraylist_transition_get(parsing_stage_data.transitions, i) ;
        printf("%zu %zu %zu %zu\n", (*ref)->from, (*ref)->to, (*ref)->event, (*ref)->callback) ;
    }
    */

    // Stage 2 - Optimisation.
    generation_stage_data_t generation_data ;
    transform_graph(&parsing_stage_data, &generation_data) ;

    /*
    printf("\nTransitions (sorted) :\n") ;
    for (size_t i = 0; i < n_transitions; ++ i) {
        transition_t* ref = arraylist_transition_get(generation_data.transitions, i) ;
        printf("%zu %zu %zu %zu\n", (*ref)->from, (*ref)->to, (*ref)->event, (*ref)->callback) ;
    }
    */

    // Clear stage 1 dictionnaries as they aren't required anymore.
    dict_string_clear(parsing_stage_data.callbacks) ;
    dict_string_clear(parsing_stage_data.events) ;
    dict_string_clear(parsing_stage_data.states) ;

    // Stage 3 - Generation.
    generate_C(&generation_data) ;
    
    // After - Cleaning.
    arraylist_transition_clear(generation_data.transitions) ;
    arraylist_string_clear(generation_data.callbacks) ;
    arraylist_string_clear(generation_data.events) ;
    arraylist_string_clear(generation_data.states) ;

#if defined(CARTEUR_GRAPH_ANALYSIS)
    igraph_destroy(&parsing_stage_data.graph) ;
#endif

    return 0 ;
}

