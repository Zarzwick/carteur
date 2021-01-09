# This is not general yet... I'll fix that in a moment. The problem is mostly
# the M* lib which is a headers lib that I like, but that doesn't appear to be
# distributed (hence the $(HOME)/.local/include which is where I install it).

all:
	clang -Wall -I/usr/include/igraph -I$(HOME)/.local/include -linih -std=c11 -g -ggdb src/main.c -o truc

# Linking with igraph links with gomp which triggers a memory leak, though...
#clang -Wall -I/usr/include/igraph -ligraph -linih -std=c11 -g -ggdb src/main.c -o truc
