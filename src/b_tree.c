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
  int prev_comp;                /* Previous value of comp */
 
  int flush;                    /* Should I flush sector[0] to disk after b_tree_insert() */
} B_Tree;

void *t_node_setup(B_Tree* TREE, unsigned int lba, void* parent, int pindex){
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
    node->flush = 0;
    node->parent = parent;
    node->parent_index = pindex;
    //if(TREE->free_list != NULL) TREE->free_list->parent = node;
    TREE->free_list = node;
    node->keys = malloc((TREE->keys_per_block+1) * sizeof(unsigned char *));

    // set all the points
    for(int i = 0; i <= TREE->keys_per_block; i++){
        node->keys[i] = node->bytes + 2 + TREE->key_size * i;
    }
    
    // set the lbas
    node->lbas = malloc((TREE->lbas_per_block+1) * sizeof(int));
    //node->lbas = (void *) node->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4);
    memcpy(node->lbas,(void *) node->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4), TREE->lbas_per_block * 4);

    return node;
}

void flush(B_Tree *TREE);

void *b_tree_create(char *filename, long size, int key_size){
    // create tree
    B_Tree *TREE = malloc(sizeof(B_Tree));
    Tree_Node *t;

    TREE->disk = jdisk_create(filename,size);
    TREE->key_size = key_size;
    TREE->first_free_block = 2;
    TREE->root_lba = 1;
    TREE->flush = 1;

    TREE->size = jdisk_size(TREE->disk);
    TREE->num_lbas = TREE->size/JDISK_SECTOR_SIZE;
    TREE->keys_per_block = (JDISK_SECTOR_SIZE - 6) / (TREE->key_size + 4);
    TREE->lbas_per_block = TREE->keys_per_block + 1;
    TREE->tmp_e = NULL;
    TREE->tmp_e_index = -1;
    TREE->root = t_node_setup(TREE,TREE->root_lba,NULL,-1);
    //TREE->root = malloc(sizeof(struct tnode));
    t = TREE->root;
    //t->parent = NULL;
    //t->lba = asd
    //t->bytes;
    t->lbas[0] = 0;
    for(int i = 0; i < TREE->keys_per_block; i++) t->lbas[i] = 0;
    explicit_bzero(t->bytes,JDISK_SECTOR_SIZE+256);
    t->flush = 1;
    t->internal = 0;
    t->nkeys = 0;
    flush(TREE);
    return TREE;
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
    printf("KEYSPERBLOCK: %d\n",TREE->keys_per_block);
    TREE->lbas_per_block = TREE->keys_per_block + 1;
    TREE->tmp_e = NULL;
    TREE->tmp_e_index = -1;

    // go ahead and read the root node (maybe remove later)
    TREE->root = t_node_setup(TREE,TREE->root_lba,NULL,-1);

    return TREE;
}

void flush(B_Tree *TREE){
    Tree_Node *t = TREE->free_list;

    while(t != NULL){
        //printf("checking: 0x%x\n",t->lba);
        if(t->flush == 1){
            t->bytes[0] = t->internal;
            t->bytes[1] = t->nkeys;
            //printf("FLUSHING: 0x%x\n",t->lba);
            memcpy((void *) t->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4),t->lbas, TREE->lbas_per_block * 4);

            jdisk_write(TREE->disk,t->lba,t->bytes);
        }
        t = t->ptr;
    }

    if(TREE->flush == 1){
        jdisk_write(TREE->disk,0,TREE);
    }
}

void split(B_Tree *TREE,Tree_Node *t){
    Tree_Node *parent, *sibling;
    int middle, pindex, i, comp;
    
    if(t == NULL) return;
    if(t->nkeys < TREE->keys_per_block){
        split(TREE,t->parent);
        return;
    }

    //printf("top\n");
    if(t->parent == NULL){
        parent = t_node_setup(TREE,TREE->first_free_block,NULL,-1);
        for(int i = 0; i < TREE->keys_per_block; i++) parent->lbas[i] = 0;
        explicit_bzero(parent->bytes,JDISK_SECTOR_SIZE+256);
        t->parent = parent;
        TREE->root = parent;
        TREE->first_free_block++;
        parent->nkeys = 0;
        parent->lbas[0] = t->lba;
        t->parent_index = 0;
        parent->internal = 1;
        TREE->root_lba = parent->lba;
    }
    //printf("under if\n");
    parent = t->parent;
    parent->flush = 1;

    middle = (TREE->keys_per_block/2);
    pindex = 0;
    for(i = 0; i < parent->nkeys; i++){
        comp = memcmp(t->keys[middle],parent->keys[i],TREE->key_size);
        if(comp < 0){
            pindex = i;
        }
    }
    t->parent_index = pindex;
    //printf("for1\n");
    for(i = parent->nkeys+1; i > t->parent_index; i--){
        memcpy(parent->keys[i],parent->keys[i-1],TREE->key_size);
    }
    //printf("memcpy 1\n");
    //printf("pindex = %d\n",t->parent_index);
    //printf("middle = %d\n",middle);
    memcpy(parent->keys[t->parent_index],t->keys[middle],TREE->key_size);

    //printf("for 2\n");
    for(i = parent->nkeys; i > t->parent_index; i--){
        parent->lbas[i+1] = parent->lbas[i];
    }
    //printf("rand\n");
    parent->nkeys++;
    parent->lbas[t->parent_index] = t->lba;

    //printf("rand2\n");
    sibling = t_node_setup(TREE,TREE->first_free_block,parent,t->parent_index+1);
    TREE->first_free_block++;
    sibling->nkeys = 0;
    sibling->internal = t->internal;
    sibling->flush = 1;

    //printf("rand3\n");
    parent->lbas[t->parent_index+1] = sibling->lba;

    middle++;
    //printf("for3\n");
    for(i = middle; i < t->nkeys; i++){
        memcpy(sibling->keys[i-middle],t->keys[i],TREE->key_size);
        sibling->lbas[i-middle] = t->lbas[i];
        sibling->nkeys++;
    }
    //printf("misc\n");
    sibling->lbas[sibling->nkeys] = t->lbas[t->nkeys]; 

    t->nkeys = middle-1;

    split(TREE,parent);
}

unsigned int b_tree_insert(void *b_tree, void *key, void *record){
    // insert to the tree
    unsigned int lba;
    int i, index;
    B_Tree *TREE = b_tree;
    Tree_Node *t;
    
    if(TREE->first_free_block >= TREE->num_lbas) return 0;

    //printf("FIND\n");
    lba = b_tree_find(b_tree,key);
    if(lba != 0){
        jdisk_write(TREE->disk,lba,record);
        return lba;
    }
    TREE->flush = 0;

    //printf("IF\n");

    //if(TREE->root_lba == TREE->num_lbas) return 0;

    t = TREE->tmp_e;
    index = TREE->tmp_e_index;

    if(t == NULL){
        t = TREE->root;
        index = 0;
    }

    //printf("0x%x\n",t);
    //printf("t->keys[index]: %s\n",t->keys[index]);
    //printf(TREE->size)

    //printf("before for\n");
    for(i = t->nkeys; i > index; i--){
        //printf("%d\n",i);
        //t->keys[i] = t->keys[i-1];
        memcpy(t->keys[i],t->keys[i-1],TREE->key_size);
    }

    //printf("after 1st for\n");
    memcpy(t->keys[index],key,TREE->key_size);
    t->nkeys += 1;
    //printf("t->nkeys == %d",t->nkeys);
    t->flush = 1;

    //printf("before write\n");
    lba = TREE->first_free_block;
    jdisk_write(TREE->disk,lba,record);
    TREE->first_free_block++;
    TREE->flush = 1;
    //printf("after write\n");

    for(i = t->nkeys; i > index; i--){
        t->lbas[i] = t->lbas[i-1];
        //memcpy(t->lbas[i],t->lbas[i-1],4);
    }

    //printf("after 2nd for\n");
    t->lbas[index] = lba;
    
    //memcpy((void *) t->bytes + (JDISK_SECTOR_SIZE - TREE->lbas_per_block * 4),t->lbas, TREE->lbas_per_block * 4);
    //memcpy(t->lbas[index],&lba,4);

    if(t->nkeys > TREE->keys_per_block){
        split(TREE,t);
    }

    /*printf("WRITESTACK\n");
    jdisk_write(TREE->disk,TREE->first_free_block,record);
    printf("memcpy\n");
    //printf("t->keys[t->nkeys-1]: %s\n",t->keys[t->nkeys-1]);
    printf("key: %s\n",key);
    memcpy(t->keys[t->nkeys],key,TREE->key_size);
    printf("POST\n");
    t->lbas[t->nkeys] = TREE->first_free_block;
    TREE->first_free_block += 1;
    TREE->flush = 1;
    t->flush = 1;
    t->nkeys += 1;*/

    //printf("FLUSH\n");
    flush(TREE);
    return lba;
}

// returns the last lba in the node (used to get data lba of upper node)
unsigned int get_last_lba(Tree_Node *t,B_Tree *TREE){
    if(t->internal == 1){
        return get_last_lba(t_node_setup(TREE,t->lbas[t->nkeys],t,t->nkeys),TREE);
    }
    //TREE->tmp_e = t;
    //TREE->tmp_e_index = t->nkeys;
    return t->lbas[t->nkeys];
}

unsigned int recursive_find(B_Tree *TREE,Tree_Node *t, void *key){
    int i, comp, comp2, index;
    
    // check all keys in current node
    /*for(i = 0; i < t->nkeys; i++){
        comp = memcmp(key,t->keys[i],TREE->key_size);
        if(comp == 0 && t->internal == 0){
            TREE->tmp_e = t;
            TREE->tmp_e_index = i;
            return t->lbas[i];
        }else if(comp == 0 && t->internal == 1){
            return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
        }else if(comp < 0 && comp < TREE->prev_comp){
            TREE->tmp_e = t;
            TREE->prev_comp = comp;
            printf("COMP == %d\n", comp);
            printf("t->keys[i]: %s\n",t->keys[i]);
            TREE->tmp_e_index = i;
        }
    }

    if(TREE->prev_comp == 0x7FFFFFFF){
        TREE->tmp_e = t;
        TREE->tmp_e_index = t->nkeys;
    }*/

    /*if(t->internal == 1){
        for(i = 0; i < t->nkeys; i++){
            comp = memcmp(t->keys[i],key,TREE->key_size);
            if(comp == 0){
                return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
            }else if(comp < 0){
                return recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i),key);
            }else if(comp > 0)
        }
    }*/

    /*for(i = 0; i < t->nkeys; i++){
        comp = memcmp(key,t->keys[i],TREE->key_size);
        if(t->internal == 1 && comp == 0){
            return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
        }else if(comp == 0){
            return t->lbas[i];
        }
    }*/
    /*for(i = 0; i < t->nkeys; i++){
        comp = memcmp(key,t->keys[i],TREE->key_size);
        comp2 = memcmp(key,t->keys[i+1],TREE->key_size);
        if(i == t->nkeys-1){
            if(t->internal == 1) return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
            if(t->internal == 0) return
        }
        if(comp > 0 && comp2 < 0){
            return recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i+1),key);
        }
    }*/



    //index = -1;
    for(i = 0; i < t->nkeys; i++){
        comp = memcmp(key,t->keys[i],TREE->key_size);
        if(t->internal == 1){
            if(comp == 0){
                return get_last_lba(t_node_setup(TREE,t->lbas[i],t,i),TREE);
            }else if(comp < 0){
                //index = i;
                //comp = recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i),key);
                return recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i),key);;
                //if(comp != 0) return comp;
            }else if(comp > 0 && i == t->nkeys-1){
                //index = i+1;
                //comp = recursive_find(TREE,t_node_setup(TREE,t->lbas[i+1],t,i+1),key);
                return recursive_find(TREE,t_node_setup(TREE,t->lbas[i+1],t,i+1),key);
                //if(comp != 0) return comp;
            }
        }else{
            //printf("comp: %d\n",comp);
            //printf("%s\n",t->keys[i]);
            if(comp == 0){
                return t->lbas[i];
            }else if(comp < 0){
                //printf("comp: %d\n",comp);
                //printf("%s\n",t->keys[i]);
                TREE->tmp_e = t;
                //TREE->prev_comp = comp;
                TREE->tmp_e_index = i;
                return 0;
            }else if(comp > 0 && i == t->nkeys-1){
                TREE->tmp_e = t;
                TREE->tmp_e_index = i+1;
                return 0;
            }
        }
    }

    //if(index != -1) return recursive_find(TREE,t_node_setup(TREE,t->lbas[index],t,index),key);

    // recursively check all children
    /*if(t->internal == 1){
        for(i = 0; i < t->nkeys+1; i++){
            comp = recursive_find(TREE,t_node_setup(TREE,t->lbas[i],t,i),key);
            if(comp != 0) return comp;
        }
    }*/

    return 0;
}

unsigned int b_tree_find(void *b_tree, void *key){
    // find a value in the tree
    B_Tree *b = b_tree;
    b->prev_comp = 0x7FFFFFFF;
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
        if(t->internal == 1) print_node(t_node_setup(TREE,t->lbas[i],t,i),TREE);
    }
}

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