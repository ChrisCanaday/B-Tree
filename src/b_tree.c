#include <b_tree.h>

void *b_tree_create(char *filename, long size, int key_size){
    // create tree
}
void *b_tree_attach(char *filename){
    // attach to a tree
}

unsigned int b_tree_insert(void *b_tree, void *key, void *record){
    // insert to the tree
}
unsigned int b_tree_find(void *b_tree, void *key){
    // find a value in the tree
}
void *b_tree_disk(void *b_tree){
    // no idea
}
int b_tree_key_size(void *b_tree){
    // idk
}
void b_tree_print_tree(void *b_tree){
    // print the tree
}