#include <b_tree.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct tnode {
  unsigned char bytes[JDISK_SECTOR_SIZE+256]; /* This holds the sector for reading and writing->  
                                                 It has extra room because your internal representation  
                                                 will hold an extra key-> */
  unsigned char nkeys;                      /* Number of keys in the node */
  unsigned char flush;                      /* Should I flush this to disk at the end of b_tree_insert()? */
  unsigned char internal;                   /* Internal or external node */
  unsigned int lba;                         /* LBA when the node is flushed */
  unsigned char **keys;                     /* Pointers to the keys->  Size = MAXKEY+1 */
  unsigned int *lbas;                       /* Pointer to the array of LBA's->  Size = MAXKEY+2 */
  struct tnode *parent;                     /* Pointer to my parent -- useful for splitting */
  int parent_index;                         /* My index in my parent */
  struct tnode *ptr;                        /* Free list link */
} Tree_Node;

typedef struct {
  int key_size;                 /* These are the first 16/12 bytes in sector 0 */
  unsigned int root_lba;
  unsigned long first_free_block;

  void *disk;                   /* The jdisk */
  unsigned long size;           /* The jdisk's size */
  unsigned long num_lbas;       /* size/JDISK_SECTOR_SIZE */
  int keys_per_block;           /* MAXKEY */
  int lbas_per_block;           /* MAXKEY+1 */
  Tree_Node *free_list;         /* Free list of nodes */
  
  Tree_Node *tmp_e;             /* When find() fails, this is a pointer to the external node */
  int tmp_e_index;              /* and the index where the key should have gone */

  void *root;                   /* Root of B_Tree */
 
  int flush;                    /* Should I flush sector[0] to disk after b_tree_insert() */
} B_Tree;

void *t_node_read(void * b_tree, unsigned int lba){
    B_Tree *TREE = b_tree;
    unsigned char buf[JDISK_SECTOR_SIZE];
    Tree_Node *tnode = malloc(sizeof(struct tnode));

    jdisk_read(TREE->disk,lba,buf);
}

void *t_node_setup(Tree_Node* node,B_Tree* TREE, void* buf){
    memcpy(node->bytes,buf,JDISK_SECTOR_SIZE);

    node->internal = node->bytes[0];
    node->nkeys = node->bytes[1];
    node->keys = malloc((TREE->keys_per_block+1) * TREE->key_size);
    node->keys = (void *) node->bytes + 2; // stop compilar from yelling
    
    node->lbas = malloc((TREE->lbas_per_block+1) * 4);
    node->lbas = (void *) node->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4);

    return node;
    //node->lbas = JDISK_SECTOR_SIZE - node->bytes + 2 + 
}

void *b_tree_create(char *filename, long size, int key_size){
    // create tree
}
void *b_tree_attach(char *filename){
    unsigned char buf[JDISK_SECTOR_SIZE];
    B_Tree *TREE = malloc(sizeof(B_Tree));
    Tree_Node *node = malloc(sizeof(struct tnode));

    TREE->disk = jdisk_attach(filename);

    jdisk_read(TREE->disk,0,buf);

    memcpy(&TREE->key_size,buf,4);
    memcpy(&TREE->root_lba,buf+4,4);
    memcpy(&TREE->first_free_block,buf+8,8);

    TREE->size = jdisk_size(TREE->disk);
    TREE->num_lbas = TREE->size/JDISK_SECTOR_SIZE;

    TREE->keys_per_block = (JDISK_SECTOR_SIZE - 6) / (TREE->key_size + 4);
    TREE->lbas_per_block = TREE->keys_per_block + 1;

    explicit_bzero(buf,JDISK_SECTOR_SIZE);

    jdisk_read(TREE->disk,TREE->root_lba,buf);

    TREE->root = t_node_setup(node,TREE,buf);

    return TREE;
}

unsigned int b_tree_insert(void *b_tree, void *key, void *record){
    // insert to the tree
    return 0;
}
unsigned int b_tree_find(void *b_tree, void *key){
    // find a value in the tree
    return 0;
}
void *b_tree_disk(void *b_tree){
    return ((B_Tree*) b_tree)->disk;
}
int b_tree_key_size(void *b_tree){
    return ((B_Tree*) b_tree)->key_size;
}
void b_tree_print_tree(void *b_tree){
    printf("PRINT TREE\n");
    B_Tree *b = b_tree;
    Tree_Node *t = b->root;
    printf("%d\n",t->nkeys);
    /*for(int i = 0; i < t->nkeys; i++){
        printf("%s\n",t->keys[i]);
    }*/
    printf("%s\n",t->bytes);
    //t_node_read(b_tree,);
}