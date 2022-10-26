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

void *t_node_setup(B_Tree* TREE, unsigned int lba){
    unsigned char buf[JDISK_SECTOR_SIZE];
    Tree_Node *node;

    node = TREE->free_list;

    // see if we have already read that block
    while(node != NULL){
        if(node->lba == lba) return node;
        node = node->ptr;
    }

    node = malloc(sizeof(struct tnode));

    jdisk_read(TREE->disk,lba,buf);
    memcpy(node->bytes,buf,JDISK_SECTOR_SIZE);

    // set defaults and add it to the free_list
    node->internal = node->bytes[0];
    node->nkeys = node->bytes[1];
    node->lba = lba;
    node->ptr = TREE->free_list;
    if(TREE->free_list != NULL) TREE->free_list->parent = node;
    TREE->free_list = node;
    node->keys = malloc((TREE->keys_per_block+1) * sizeof(unsigned char *));

    // set all the points
    for(int i = 0; i < TREE->keys_per_block; i++){
        node->keys[i] = node->bytes + 2 + TREE->key_size * i;
    }
    
    // set the lbas
    node->lbas = malloc((TREE->lbas_per_block+1) * sizeof(int));
    node->lbas = (void *) node->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4);

    return node;
}

void *b_tree_create(char *filename, long size, int key_size){
    // create tree
}

void *b_tree_attach(char *filename){
    unsigned char buf[JDISK_SECTOR_SIZE];
    B_Tree *TREE = malloc(sizeof(B_Tree));
    //Tree_Node *node = malloc(sizeof(struct tnode));

    TREE->disk = jdisk_attach(filename);

    jdisk_read(TREE->disk,0,buf);

    // read in BTREE info
    memcpy(&TREE->key_size,buf,4);
    memcpy(&TREE->root_lba,buf+4,4);
    memcpy(&TREE->first_free_block,buf+8,8);

    // set up some values
    TREE->size = jdisk_size(TREE->disk);
    TREE->num_lbas = TREE->size/JDISK_SECTOR_SIZE;

    TREE->keys_per_block = (JDISK_SECTOR_SIZE - 6) / (TREE->key_size + 4);
    TREE->lbas_per_block = TREE->keys_per_block + 1;

    // go ahead and read the root node (maybe remove later)
    TREE->root = t_node_setup(TREE,TREE->root_lba);

    return TREE;
}

unsigned int b_tree_insert(void *b_tree, void *key, void *record){
    // insert to the tree
    return 0;
}

// returns the last lba in the node (used to get data lba of upper node)
unsigned int get_last_lba(Tree_Node *t){
    return t->lbas[t->nkeys];
}

unsigned int recursive_find(B_Tree *TREE,Tree_Node *t, void *key){
    int i;
    unsigned int comp;
    
    // check all keys in current node
    for(i = 0; i < t->nkeys; i++){
        comp = memcmp(t->keys[i],key,TREE->key_size);
        if(comp == 0 && t->internal == 0){
            return t->lbas[i];
        }else if(comp == 0 && t->internal == 1){
            return get_last_lba(t_node_setup(TREE,t->lbas[i]));
        }
    }

    // recursively check all children
    if(t->internal == 1){
        for(i = 0; i < t->nkeys+1; i++){
            comp = recursive_find(TREE,t_node_setup(TREE,t->lbas[i]),key);
            if(comp != 0) return comp;
        }
    }

    return 0;
}

unsigned int b_tree_find(void *b_tree, void *key){
    // find a value in the tree
    B_Tree *b = b_tree;
    return recursive_find(b,b->root,key);
}

void *b_tree_disk(void *b_tree){
    return ((B_Tree*) b_tree)->disk;
}

int b_tree_key_size(void *b_tree){
    return ((B_Tree*) b_tree)->key_size;
}

void print_node(Tree_Node *t, B_Tree *TREE){
    int i,j;
    
    printf("LBA 0x%08x. Internal: %d\n",t->lba,t->internal);
    for(i = 0; i < t->nkeys+1; i++){
        if(i < t->nkeys){
            printf("Entry %d: Key: %-20s LBA: 0x%08x\n",i,t->keys[i],t->lbas[i]);
        }else{
            printf("Entry %d:                           LBA: 0x%08x\n",i,t->lbas[i]);
        }
    }
    printf("\n");
    for(i = 0; i < t->nkeys+1; i++){
        if(t->internal == 1) print_node(t_node_setup(TREE,t->lbas[i]),TREE);
    }
}

void b_tree_print_tree(void *b_tree){
    B_Tree *b = b_tree;
    Tree_Node *t = b->root;
    Tree_Node *i;

    print_node(t,b);

    i = b->free_list;
    while(i != NULL){
        printf("LBA 0x%08x.\n",i->lba);
        i = i->ptr;
    }
}