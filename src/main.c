//
// This is part of the source code of the carteur state machine generator.
// The code is under the BSD-3 license (see LICENSE).
//
// Hugo RENS <hugo.rens@univ-tlse3.fr>
//

// This triggers the construction and use of an inet graph to perform common
// or less common analysis, and, eventually, optimisations. UNSUPPORTED now...
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
/// Here are a few parameters. A state machine (or graph) has a name. It can
/// - declare the states (enum name_states) or not (default: yes)
/// - declare the events (enum name_events) or not (default: no)
///
typedef struct parameters_t {
    bool declare_states ;
    bool declare_events ;
    char* machine_name ;
    char* states_enum_name ;
    char* events_enum_name ;
} parameters_t ;

#define CARTEUR_DEFAULT_MACHINE_NAME "machine"
#define CARTEUR_DEFAULT_STATES_NAME "machine_states"
#define CARTEUR_DEFAULT_EVENTS_NAME "machine_events"

#define CARTEUR_DEFAULT_PARAMETERS \
    { .declare_states = true \
    , .declare_events = false \
    , .machine_name = CARTEUR_DEFAULT_MACHINE_NAME \
    , .states_enum_name = CARTEUR_DEFAULT_STATES_NAME \
    , .events_enum_name = CARTEUR_DEFAULT_EVENTS_NAME \
    }

void parameters_init(parameters_t* params) {
    params->declare_states = true ;
    params->declare_events = false ;
    // Copy names from string litterals, to prevent double-free.
    string_t machine_name_copy ;
    string_t states_enum_name_copy ;
    string_t events_enum_name_copy ;
    string_init_set(machine_name_copy, CARTEUR_DEFAULT_MACHINE_NAME) ;
    string_init_set(states_enum_name_copy, CARTEUR_DEFAULT_STATES_NAME) ;
    string_init_set(events_enum_name_copy, CARTEUR_DEFAULT_EVENTS_NAME) ;
    params->machine_name = string_clear_get_str(machine_name_copy) ;
    params->states_enum_name = string_clear_get_str(states_enum_name_copy) ;
    params->events_enum_name = string_clear_get_str(events_enum_name_copy) ;
}

#define CARTEUR_INI_TRUE "true"
#define CARTEUR_INI_FALSE "false"

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

void transition_init(transition_t t)
{
    t->from = 0ul ;
    t->to = 0ul ;
    t->event = 0ul ;
    t->callback = 0ul ;
}
void transition_set(transition_t transition, const transition_t model) {}
void transition_init_set(transition_t transition, const transition_t model)
{
    *transition = *model ;
}
void transition_clear(transition_t transition) {}
int transition_cmp(transition_t a, transition_t b)
{
    return a->event - b->event ;
}
int transition_cmp_qsort(transition_t const* a, transition_t const* b)
{
    return (*a)->event - (*b)->event ;
}
void transition_swap(transition_t a, transition_t b)
{
    transition_t temp ;
    *temp = *a ;
    *a = *b ;
    *b = *temp ;
}

#define M_OPL_transition_t() \
    ( INIT(transition_init) \
    , SET(transition_set) \
    , INIT_SET(transition_init_set) \
    , CLEAR(transition_clear) \
    , CMP(transition_cmp) \
    , SWAP(transition_swap) \
    )

#define M_DELETABLE_PTR_OPLIST \
    ( INIT(M_INIT_DEFAULT) \
    , INIT_SET(M_SET_DEFAULT) \
    , SET(M_SET_DEFAULT) \
    , CLEAR(free) \
    , EQUAL(M_EQUAL_DEFAULT) \
    , INIT_MOVE(M_MOVE_DEFAULT) \
    , MOVE(M_MOVE_DEFAULT) \
    , SWAP(M_SWAP_DEFAULT) \
    )

ARRAY_DEF(array_transition, transition_t)
ARRAY_DEF(array_string, string_t, STRING_OPLIST)
ARRAY_DEF(array_char_ptr, char*, M_DELETABLE_PTR_OPLIST)
DICT_DEF2(dict_string, string_t, STRING_OPLIST, size_t, M_DEFAULT_OPLIST)

// ................................................................... STAGE 1

///
/// Information at stage 1. This contains storage for the states, events
/// and callback names, and a dynamic array of transitions.
///
typedef struct parsing_stage_data_t {
    parameters_t* parameters ;
    //igraph_t graph ;
    dict_string_t states ;
    dict_string_t events ;
    dict_string_t callbacks ;
    array_transition_t transitions ;
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
        string_t value_copy ;
        string_init_set_str(value_copy, value) ;
        const size_t n = dict_string_size(machine->states) ;
        dict_string_set_at(machine->states, value_copy, n) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
#endif
    }

    // Add transition.
    else if (strcmp(name, "transition") == 0) {
        parse_identifers(value, strlen(value), parser_transition_handler, &todo) ;
        array_transition_push_back(machine->transitions, transition) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
        //igraph_add_edge(graph, from, to).
#endif
    }

    // Parameters.
    else if (strcmp(name, "declare_states") == 0) {
        if (strcmp(value, CARTEUR_INI_TRUE) == 0)
            machine->parameters->declare_states = true ;
        else if (strcmp(value, CARTEUR_INI_FALSE) == 0)
            machine->parameters->declare_states = false ;
        //else
        //    error
    }

    else if (strcmp(name, "declare_events") == 0) {
        if (strcmp(value, CARTEUR_INI_TRUE) == 0)
            machine->parameters->declare_events = true ;
        else if (strcmp(value, CARTEUR_INI_FALSE) == 0)
            machine->parameters->declare_events = false ;
        // else
        //    error
    }

    else if (strcmp(name, "machine_name") == 0) {
        string_t copy ;
        string_init_set(copy, value) ;
        free(machine->parameters->machine_name) ;
        machine->parameters->machine_name = string_clear_get_str(copy) ;
    }

    else if (strcmp(name, "states_enum_name") == 0) {
        string_t copy ;
        string_init_set(copy, value) ;
        free(machine->parameters->states_enum_name) ;
        machine->parameters->states_enum_name = string_clear_get_str(copy) ;
    }

    else if (strcmp(name, "events_enum_name") == 0) {
        string_t copy ;
        string_init_set(copy, value) ;
        free(machine->parameters->events_enum_name) ;
        machine->parameters->events_enum_name = string_clear_get_str(copy) ;
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
    parameters_t* parameters ;
    array_char_ptr_t states ;
    array_char_ptr_t events ;
    array_char_ptr_t callbacks ;
    array_transition_t transitions ;
} generation_stage_data_t ;

///
/// Turn a mapping (string -> integer) into an array of strings, such that
/// the mapping is reversed.
///
void fill_array_from_dictionnary
    ( array_char_ptr_t dest
    , const dict_string_t src
    )
{
    const size_t n = dict_string_size(src) ;
    array_char_ptr_init(dest) ;
    array_char_ptr_resize(dest, n) ;
    dict_string_it_t it ;
    dict_string_it(it, src) ;
    for (size_t i = 0; i < n; ++ i) {
        const dict_string_itref_t* ref = dict_string_cref(it) ;
        string_t copy ;
        string_init_set(copy, ref->key) ;
        array_char_ptr_set_at(dest, ref->value, string_clear_get_str(copy)) ;
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
    array_transition_init_move(generation->transitions, parsing->transitions) ;
    array_transition_special_sort(generation->transitions, transition_cmp_qsort) ;
    // This seems to trigger a fault when used on a 2-sized array. See l.814 of m-array.h FIXME...
    //array_transition_special_stable_sort(generation->transitions) ;
#if defined(CARTEUR_GRAPH_ANALYSIS)
    // TODO
#endif
}

// ................................................................... STAGE 3

///
/// ...
///
void generate_C_header(FILE* file, generation_stage_data_t* stage_data) {
    const char* machine_name = stage_data->parameters->machine_name ;
    const char* states_enum_name = stage_data->parameters->states_enum_name ;
    fprintf(file, "// File generated by carteur.\n\n") ;
    fprintf(file, "#pragma once\n\n") ;

    // Emit an enum of all states.
    if (stage_data->parameters->declare_states) {
        fprintf(file, "typedef enum {\n") ;
        const size_t n_states = array_char_ptr_size(stage_data->states) ;
        for (size_t s = 0; s < n_states; ++ s) {
            fprintf(file, "\t%s,\n", *array_char_ptr_get(stage_data->states, s));
        }
        fprintf(file, "} %s ;\n\n", stage_data->parameters->states_enum_name) ;
    }

    // Emit an enum of all events.
    if (stage_data->parameters->declare_events) {
        fprintf(file, "typedef enum {\n") ;
        const size_t n_events = array_char_ptr_size(stage_data->events) ;
        for (size_t s = 0; s < n_events; ++ s) {
            fprintf(file, "\t%s,\n", *array_char_ptr_get(stage_data->events, s));
        }
        fprintf(file, "} %s ;\n\n", stage_data->parameters->events_enum_name) ;
    }

    // Emit one signature per event.
    const size_t n_events = array_char_ptr_size(stage_data->events) ;
    for (size_t ev = 0; ev < n_events; ++ ev) {
        const char* ev_name = *array_char_ptr_get(stage_data->events, ev) ;
        fprintf(file, "void %s_handle_%s(%s* state, void* user) ;\n"
                , machine_name, ev_name, states_enum_name) ;
    }
    fprintf(file, "\n") ;
}

///
/// ...
///
void generate_C_source(FILE* file, generation_stage_data_t* stage_data) {
    const char* machine_name = stage_data->parameters->machine_name ;
    const char* states_enum_name = stage_data->parameters->states_enum_name ;
    fprintf(file, "#include \"generated_%s.h\"\n\n", machine_name) ;

    array_transition_it_t it ;
    array_transition_it(it, stage_data->transitions) ;
    bool transitions_available = ! array_transition_end_p(it) ;

    // Consume all transitions. The outer loop handles event changes, and thus
    // generates the head and bottom of each handler function. The inner loop
    // consumes all transitions related to the same event (hence the order
    // requirement) to produce the different cases.
    while (transitions_available) {
        const transition_t* ref = array_transition_cref(it) ;
        char* event = *array_char_ptr_get(stage_data->events, (*ref)->event) ;
        const size_t current_event = (*ref)->event ;
        const char* callback = NULL ;
        const char* from     = NULL ;
        const char* to       = NULL ;

        // Emit top of function.
        fprintf(file, "void %s_handle_%s(%s* state, void* user) {\n",
                machine_name, event, states_enum_name) ;
        fprintf(file, "\tswitch (*state) {\n") ;

        // Emit cases.
        bool same_event = true ;
        while (same_event) {
            // Get names for the current transition t.
            callback = *array_char_ptr_get(stage_data->callbacks, (*ref)->callback) ;
            from = *array_char_ptr_get(stage_data->states, (*ref)->from) ;
            to   = *array_char_ptr_get(stage_data->states, (*ref)->to) ;
            fprintf(file, "\tcase %s:\n", from) ;
            fprintf(file, "\t\t%s(user)\n", callback) ;
            fprintf(file, "\t\t*state = %s\n", to) ;
            fprintf(file, "\t\tbreak\n") ;

            // Decide if the event will be the same or not.
            array_transition_next(it) ;
            if (array_transition_end_p(it)) {
                transitions_available = false ;
                break ;
            }
            ref = array_transition_cref(it) ;
            same_event = (current_event == (*ref)->event) ;
        }

        // Emit bottom of function.
        // TODO Give the (default) option to raise an error.
        fprintf(file, "\tdefault:\n\t\t// IMPOSSIBLE\n") ;
        fprintf(file, "\t}\n}\n\n") ;
    }
}

///
/// ...
///
void generate_C(generation_stage_data_t* stage_data) {
    FILE* h_file = fopen("generated_" CARTEUR_DEFAULT_MACHINE_NAME ".h", "w") ;
    FILE* c_file = fopen("generated_" CARTEUR_DEFAULT_MACHINE_NAME ".c", "w") ;
    generate_C_header(h_file, stage_data) ;
    generate_C_source(c_file, stage_data) ;
    fclose(h_file) ;
    fclose(c_file) ;
}

// ...................................................................... MAIN

int main()
{
    //int err ;
    parameters_t params ;
    parameters_init(&params) ;
    parsing_stage_data_t parsing_stage_data ;
    parsing_stage_data.parameters = &params ;

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
    array_transition_init(parsing_stage_data.transitions) ;
    
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
        array_transition_size(parsing_stage_data.transitions) ;
    for (size_t i = 0; i < n_transitions; ++ i) {
        transition_t* ref = array_transition_get(parsing_stage_data.transitions, i) ;
        printf("%zu %zu %zu %zu\n", (*ref)->from, (*ref)->to, (*ref)->event, (*ref)->callback) ;
    }
    */

    // Stage 2 - Optimisation.
    generation_stage_data_t generation_data ;
    generation_data.parameters = &params ;
    transform_graph(&parsing_stage_data, &generation_data) ;

    /*
    printf("\nTransitions (sorted) :\n") ;
    for (size_t i = 0; i < n_transitions; ++ i) {
        transition_t* ref = array_transition_get(generation_data.transitions, i) ;
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
    array_transition_clear(generation_data.transitions) ;
    array_char_ptr_clear(generation_data.states) ;
    array_char_ptr_clear(generation_data.events) ;
    array_char_ptr_clear(generation_data.callbacks) ;

    free(params.machine_name) ;
    free(params.states_enum_name) ;
    free(params.events_enum_name) ;

#if defined(CARTEUR_GRAPH_ANALYSIS)
    igraph_destroy(&parsing_stage_data.graph) ;
#endif

    return 0 ;
}

