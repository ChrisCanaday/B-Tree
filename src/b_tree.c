//  Christopher Canaday
//  COSC 494 Lab 3
//  Implement a B-Tree on Disk
//  11/7/22

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
  Tree_Node *free_list;         /* List of all held nodes */
  
  Tree_Node *tmp_e;             /* When find() fails, this is a pointer to the external node */
  int tmp_e_index;              /* and the index where the key should have gone */

  void *root;                   /* Root of B_Tree */
 
  int flush;                    /* Should I flush sector[0] to disk after b_tree_insert() */
} B_Tree;

/*  t_node_setup
 *  Returns a handle to a new Tree_Node.
 *  Reads the information into the Tree_Node and stores
 *  it using the lba.
 * 
 *  @TREE is the B_Tree
 *  @lba is the logical block address to read from
 *  @parent is what the parent should be set to
 *  @pindex is the index in the parent
 */
void *t_node_setup(B_Tree* TREE, unsigned int lba, void* parent, int pindex){
    unsigned char buf[JDISK_SECTOR_SIZE];
    Tree_Node *node;

    node = TREE->free_list;

    // see if we have already read that block
    while(node != NULL){

        // if we have then return the correct node
        if(node->lba == lba){
            if(node->parent != parent) node->parent = parent;
            return node;
        }
        node = node->ptr;
    }

    node = malloc(sizeof(struct tnode));

    // read in the node
    jdisk_read(TREE->disk,lba,buf);
    memcpy(node->bytes,buf,JDISK_SECTOR_SIZE);

    // set defaults and add it to the free_list
    node->internal = node->bytes[0];
    node->nkeys = node->bytes[1];
    node->lba = lba;
    node->ptr = TREE->free_list;
    node->flush = 0;
    node->parent = parent;
    node->parent_index = pindex;
    TREE->free_list = node;

    // set up keys
    node->keys = malloc((TREE->keys_per_block+1) * sizeof(unsigned char *));

    for(int i = 0; i <= TREE->keys_per_block; i++){
        node->keys[i] = node->bytes + 2 + TREE->key_size * i;
    }
    
    // set the lbas
    node->lbas = malloc((TREE->lbas_per_block+1) * sizeof(int));
    memcpy(node->lbas,(void *) node->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4), TREE->lbas_per_block * 4);

    return node;
}

void flush(B_Tree *TREE);

/*  b_tree_create
 *  Returns a handle to a new B_Tree.
 *  Creates a new jdisk using the filename and size.
 *  Sets all the values in the B_Tree. 
 * 
 *  @filename is the name of the jdisk file
 *  @size is the size of that jdisk file
 *  @key_size is the size of each key
 */
void *b_tree_create(char *filename, long size, int key_size){
    B_Tree *TREE = malloc(sizeof(B_Tree));
    Tree_Node *t;
    
    // create the disk and set the B_Tree info
    TREE->disk = jdisk_create(filename,size);
    TREE->key_size = key_size;
    TREE->first_free_block = 2;
    TREE->root_lba = 1;
    TREE->flush = 1;

    // get the size and set all the info based off it
    TREE->size = jdisk_size(TREE->disk);
    TREE->num_lbas = TREE->size/JDISK_SECTOR_SIZE;
    TREE->keys_per_block = (JDISK_SECTOR_SIZE - 6) / (TREE->key_size + 4);
    TREE->lbas_per_block = TREE->keys_per_block + 1;
    TREE->tmp_e = NULL;
    TREE->tmp_e_index = -1;

    // setup the root node
    TREE->root = t_node_setup(TREE,TREE->root_lba,NULL,-1);
    t = TREE->root;
    t->lbas[0] = 0;
    for(int i = 0; i < TREE->keys_per_block; i++) t->lbas[i] = 0;

    // empty out the bytes section (random stuff makes lbas funky)
    explicit_bzero(t->bytes,JDISK_SECTOR_SIZE+256);

    // set some defaults of root node
    t->flush = 1;
    t->internal = 0;
    t->nkeys = 0;

    // flush the information to disk
    flush(TREE);
    return TREE;
}

/*  b_tree_attach
 *  Returns a handle to an existing B_Tree.
 *  Reads values from jdisk and sets up the B_Tree. 
 * 
 *  @filename is the name of the jdisk file
 */
void *b_tree_attach(char *filename){
    unsigned char buf[JDISK_SECTOR_SIZE];
    B_Tree *TREE = malloc(sizeof(B_Tree));

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
    TREE->tmp_e = NULL;
    TREE->tmp_e_index = -1;

    // go ahead and read the root node
    TREE->root = t_node_setup(TREE,TREE->root_lba,NULL,-1);

    return TREE;
}

/*  flush
 *  Flushes data to disk.
 *  Goes through entire B_Tree and writes anything that has changed.
 *  
 *  @TREE is the B_Tree
 */
void flush(B_Tree *TREE){
    Tree_Node *t = TREE->free_list;

    // go through all the nodes
    while(t != NULL){
        if(t->flush == 1){

            // move the data to the bytes segment
            t->bytes[0] = t->internal;
            t->bytes[1] = t->nkeys;
            memcpy((void *) t->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4),t->lbas, TREE->lbas_per_block * 4);

            // write the bytes to disk
            jdisk_write(TREE->disk,t->lba,t->bytes);
        }
        t = t->ptr;
    }

    // write the B_Tree info if needed
    if(TREE->flush == 1){
        jdisk_write(TREE->disk,0,TREE);
    }
}

/*  split
 *  Splits a node into two.
 *  Will create a new node when necessary.
 *  Recurses up the tree and checks to see if 
 *  any other nodes need to be split.
 *
 *  @TREE is the B_Tree
 *  @t is the node to split
 */
void split(B_Tree *TREE,Tree_Node *t){
    Tree_Node *parent, *sibling;
    int middle, pindex, i, comp;
    
    // base case
    if(t == NULL) return;

    // don't need to split so go up
    if(t->nkeys <= TREE->keys_per_block){
        split(TREE,t->parent);
        return;
    }

    // we have no parent so have to create one
    if(t->parent == NULL){
        parent = t_node_setup(TREE,TREE->first_free_block,NULL,-1);

        // set all lbas to 0
        for(int i = 0; i < TREE->keys_per_block; i++) parent->lbas[i] = 0;

        // zero out the bytes (random stuff is not fun)
        explicit_bzero(parent->bytes,JDISK_SECTOR_SIZE+256);

        // set all the relationship stuff up + set the parent up
        t->parent = parent;
        TREE->root = parent;
        TREE->first_free_block++;
        parent->nkeys = 0;
        parent->lbas[0] = t->lba;
        t->parent_index = 0;
        parent->internal = 1;
        TREE->root_lba = parent->lba;
    }

    // set local parent to t's parent and make sure to flush it to disk later
    parent = t->parent;
    parent->flush = 1;

    // find the middle of the node
    middle = (TREE->keys_per_block/2);

    // find where to put the middle key in the parent
    pindex = 0;
    for(i = 0; i < parent->nkeys; i++){
        comp = memcmp(t->keys[middle],parent->keys[i],TREE->key_size);
        if(comp < 0 && pindex == 0){
            pindex = i;
            break;
        }else if(comp > 0 && i == parent->nkeys-1 && pindex == 0){
            pindex = i+1;
        }
    }
    t->parent_index = pindex;
    
    // move all the parent's keys over
    for(i = parent->nkeys; i > t->parent_index; i--){
        memcpy(parent->keys[i],parent->keys[i-1],TREE->key_size);
    }
    
    // put the middle key into parent
    memcpy(parent->keys[t->parent_index],t->keys[middle],TREE->key_size);

    // move all of parents lbas, set the middle on, and increment the nkeys
    for(i = parent->nkeys; i > t->parent_index; i--){
        parent->lbas[i+1] = parent->lbas[i];
    }
    parent->nkeys++;
    parent->lbas[t->parent_index] = t->lba;

    // setup the sibling and set its values
    sibling = t_node_setup(TREE,TREE->first_free_block,parent,t->parent_index+1);
    TREE->first_free_block++;
    sibling->nkeys = 0;
    sibling->internal = t->internal;
    sibling->flush = 1;
    parent->lbas[t->parent_index+1] = sibling->lba;

    // move all the stuff to the right of middle to the sibling
    middle++;
    for(i = middle; i < t->nkeys; i++){
        memcpy(sibling->keys[i-middle],t->keys[i],TREE->key_size);
        sibling->lbas[i-middle] = t->lbas[i];
        sibling->nkeys++;
    }
    sibling->lbas[sibling->nkeys] = t->lbas[t->nkeys]; 

    // set the number of keys for the node
    t->nkeys = middle-1;

    // recurse up to see if anything else needs to be split
    split(TREE,parent);
}

/*  reset_flush
 *  Sets all nodes flush field to 0.
 *  
 *  @b is the B_Tree
 */
void reset_flush(B_Tree *b){
    Tree_Node *t = b->free_list;

    // set all flushes to 0
    while(t != NULL){
        t->flush = 0;
        t= t->ptr;
    }
    b->flush = 0;
}

/*  b_tree_insert
 *  Inserts a key and record into a B_Tree.
 *
 *  @b_tree is the B_Tree
 *  @key is the insertion key
 *  @record is the data to insert
 */
unsigned int b_tree_insert(void *b_tree, void *key, void *record){
    // insert to the tree
    unsigned int lba;
    int i, index;
    B_Tree *TREE = b_tree;
    Tree_Node *t;
    
    // not enough room
    if(TREE->first_free_block >= TREE->num_lbas) return 0;

    reset_flush(TREE);

    // find where the thing should go
    lba = b_tree_find(b_tree,key);

    // if its already there then just replace the value
    if(lba != 0){
        jdisk_write(TREE->disk,lba,record);
        return lba;
    }

    // get where it should go
    t = TREE->tmp_e;
    index = TREE->tmp_e_index;

    if(t == NULL){
        t = TREE->root;
        index = 0;
    }

    // move all the keys over and set the correct one
    for(i = t->nkeys; i > index; i--) memcpy(t->keys[i],t->keys[i-1],TREE->key_size);
    memcpy(t->keys[index],key,TREE->key_size);
    t->nkeys += 1;
    t->flush = 1;

    // read in the data
    lba = TREE->first_free_block;
    jdisk_write(TREE->disk,lba,record);
    TREE->first_free_block++;
    TREE->flush = 1;

    // set all the lbas
    for(i = t->nkeys; i > index; i--) t->lbas[i] = t->lbas[i-1];
    t->lbas[index] = lba;
    
    // split if necessary
    if(t->nkeys > TREE->keys_per_block){
        split(TREE,t);
    }

    // flush everything to disk that needs it
    flush(TREE);
    return lba;
}

// returns the last lba in the node (used to get data lba of upper node)
/*  get_last_lba
 *  Returns the last lba in a node.
 *  Recurses to find the node if you give it an internal node.
 *
 *  @t is the node to find the lba of
 *  @TREE is the B_Tree
 */
unsigned int get_last_lba(Tree_Node *t,B_Tree *TREE){
    if(t->internal == 1){
        return get_last_lba(t_node_setup(TREE,t->lbas[t->nkeys],t,t->nkeys),TREE);
    }
    return t->lbas[t->nkeys];
}

/*  recusive_find
 *  Returns the lba associated with a key.
 *
 *  @TREE is the B_Tree
 *  @t is the current Tree_Node
 *  @key is the key
 */
unsigned int recursive_find(B_Tree *TREE,Tree_Node *t, void *key){
    int i, comp;

    // loop through all the keys in the node
    for(i = 0; i < t->nkeys; i++){

        // compare the key in this position with insertion key
        comp = memcmp(key,t->keys[i],TREE->key_size);

        if(t->internal == 1){
            if(comp == 0){
                // we found the key so get its lba
                return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
            }else if(comp < 0){
                // the key is to the left
                return recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i),key);;
            }else if(comp > 0 && i == t->nkeys-1){
                // the key is to the right (only at the end)
                return recursive_find(TREE,t_node_setup(TREE,t->lbas[i+1],t,i+1),key);
            }
        }else{
            if(comp == 0){
                // this is the key so return its lba
                return t->lbas[i];
            }else if(comp < 0){
                // it should be where this key is
                TREE->tmp_e = t;
                TREE->tmp_e_index = i;
                return 0;
            }else if(comp > 0 && i == t->nkeys-1){
                // it should be after this key (always on the end)
                TREE->tmp_e = t;
                TREE->tmp_e_index = i+1;
                return 0;
            }
        }
    }

    return 0;
}

/*  b_tree_find
 *  Returns the lba associated with key.
 *  Calls recursive_find.
 * 
 *  @b_tree is the B_Tree
 *  @key is the key
 */
unsigned int b_tree_find(void *b_tree, void *key){
    B_Tree *b = b_tree;
    return recursive_find(b,b->root,key);
}

/*  b_tree_disk
 *  Returns a handle to the jdisk inside a B_Tree.
 *
 *  @b_tree is the B_Tree
 */
void *b_tree_disk(void *b_tree){
    return ((B_Tree*) b_tree)->disk;
}

/*  b_tree_key_size
 *  Returns the key size of a given B_Tree.
 *
 *  @b_tree is the B_Tree
 */
int b_tree_key_size(void *b_tree){
    return ((B_Tree*) b_tree)->key_size;
}

/*  print_node
 *  Prints a given node.
 *  Recurses to print all of its children.
 *  
 *  @t is the Tree_Node
 *  @TREE is the B_Tree
 */
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
        if(t->internal == 1) print_node(t_node_setup(TREE,t->lbas[i],t,i),TREE);
    }
}

/*  b_tree_print_tree
 *  Prints a given B_Tree.
 *  
 *  @b_tree is the B_Tree
 */
void b_tree_print_tree(void *b_tree){
    B_Tree *b = b_tree;
    Tree_Node *t = b->root;
    Tree_Node *i;

    if(b->tmp_e != NULL) printf("0x%x  LBA: 0x%08x\n",b->tmp_e,b->tmp_e->lba);
    if(b->tmp_e != NULL) printf("%d\n",b->tmp_e_index);
    if(b->tmp_e != NULL) printf("KEY: %s\n",b->tmp_e->keys[b->tmp_e_index]);
    print_node(t,b);

    i = b->free_list;
    while(i != NULL){
        printf("LBA 0x%08x.\n",i->lba);
        i = i->ptr;
    }
}