#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "../simtypes.h"
#include "../simmem.h"
#include "freelist.h"

// define USE_VALGRIND_MEMCHECK to make
// valgrind aware of the freelist memory pool
#ifdef USE_VALGRIND_MEMCHECK
#include <valgrind/memcheck.h>
#endif


struct nodelist_node_t
{
	nodelist_node_t* next;
};


// list of all allocated memory
static nodelist_node_t *chunk_list = NULL;

/* this module keeps account of the free nodes of list and recycles them.
 * nodes of the same size will be kept in the same list
 * to be more efficient, all nodes with sizes smaller than 16 will be used at size 16 (one cacheline)
 */

// if additional fixed sizes are required, add them here
// (the few request for larger ones are satisfied with xmalloc otherwise)


// for 64 bit, set this to 128
#define MAX_LIST_INDEX (128)

// list for nodes size 8...64
#define NUM_LIST ((MAX_LIST_INDEX/4)+1)

static nodelist_node_t *all_lists[NUM_LIST] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL
};


// to have this working, we need chunks at least the size of a pointer
const size_t min_size = sizeof(void *);



void *freelist_t::gimme_node(size_t size)
{
	nodelist_node_t ** list = NULL;
	if(size==0) {
		return NULL;
	}

//#ifdef _64BIT
//	// all sizes should be divisible by 8
//	size = ((size+3)>>2)<<2;
//	if(size == 4)
//	{
//		size = 8;
//	}
//#else
//	// all sizes should be divisible by 4
//	size = ((size+3)>>2)<<2;
//#endif

	// all sizes should be divisible by 4 and at least as large as a pointer
	size = max( min_size, size );
	size = (size+3)>>2;
	size <<= 2;

	// hold return value
	nodelist_node_t *tmp;
	if(size>MAX_LIST_INDEX) {
		return xmalloc(size);
	}
	else {
		list = &(all_lists[size/4]);
	}

	// need new memory?
	if(*list==NULL) {
		int num_elements = 32764/(int)size;
		char* p = (char*)xmalloc(num_elements * size + sizeof(p));

#ifdef USE_VALGRIND_MEMCHECK
		// tell valgrind that we still cannot access the pool p
		VALGRIND_MAKE_MEM_NOACCESS(p, num_elements * size + sizeof(p));
#endif // valgrind

		// put the memory into the chunklist for free it
		nodelist_node_t *chunk = (nodelist_node_t *)p;

#ifdef USE_VALGRIND_MEMCHECK
		// tell valgrind that we reserved space for one nodelist_node_t
		VALGRIND_CREATE_MEMPOOL(chunk, 0, false);
		VALGRIND_MEMPOOL_ALLOC(chunk, chunk, sizeof(*chunk));
		VALGRIND_MAKE_MEM_UNDEFINED(chunk, sizeof(*chunk));
#endif // valgrind

		chunk->next = chunk_list;
		chunk_list = chunk;
		p += sizeof(p);
		// then enter nodes into nodelist
		for( int i=0;  i<num_elements;  i++ ) {
			nodelist_node_t *tmp = (nodelist_node_t *)(p+i*size);

#ifdef USE_VALGRIND_MEMCHECK
			// tell valgrind that we reserved space for one nodelist_node_t
			VALGRIND_CREATE_MEMPOOL(tmp, 0, false);
			VALGRIND_MEMPOOL_ALLOC(tmp, tmp, sizeof(*tmp));
			VALGRIND_MAKE_MEM_UNDEFINED(tmp, sizeof(*tmp));
#endif // valgrind
			tmp->next = *list;
			*list = tmp;
		}
	}
	// return first node
	tmp = *list;
	*list = tmp->next;

#ifdef USE_VALGRIND_MEMCHECK
	// tell valgrind that we now have access to a chunk of size bytes
	VALGRIND_MEMPOOL_CHANGE(tmp, tmp, tmp, size);
	VALGRIND_MAKE_MEM_UNDEFINED(tmp, size);
#endif // valgrind

	return (void *)tmp;
}


void freelist_t::putback_node( size_t size, void *p )
{
	nodelist_node_t ** list = NULL;
	if(size==0  ||  p==NULL) {
		return;
	}

//#ifdef _64BIT
//	// all sizes should be divisible by 8
//	size = ((size+3)>>2);
//	if(size == 1)
//	{
//		size = 2;
//	}
//#else
//	// all sizes should be divisible by 4
//	size = ((size+3)>>2);
//#endif
	
	// all sizes should be dividable by 4
	size = max( min_size, size );
	size = ((size+3)>>2);
	size <<= 2;

	if(size>MAX_LIST_INDEX) {
		free(p);
		return;
	}
	else {
		list = &(all_lists[size/4]);
	}

#ifdef USE_VALGRIND_MEMCHECK
	// tell valgrind that we keep access to a nodelist_node_t within the memory chunk
	VALGRIND_MEMPOOL_CHANGE(p, p, p, sizeof(nodelist_node_t));
	VALGRIND_MAKE_MEM_NOACCESS(p, size);
	VALGRIND_MAKE_MEM_UNDEFINED(p, sizeof(nodelist_node_t));
#endif // valgrind

	// putback to first node
	nodelist_node_t *tmp = (nodelist_node_t *)p;
	tmp->next = *list;
	*list = tmp;
}


// clears all list memories
void freelist_t::free_all_nodes()
{
	printf("freelist_t::free_all_nodes(): frees all list memory\n" );
	while(chunk_list) {
		nodelist_node_t *p = chunk_list;
		printf("freelist_t::free_all_nodes(): free node %p (next %p)\n",p,chunk_list->next);
		chunk_list = chunk_list->next;

		// now release memory
#ifdef USE_VALGRIND_MEMCHECK
		VALGRIND_DESTROY_MEMPOOL( p );
#endif // valgrind
		guarded_free( p );
	}
	printf("freelist_t::free_all_nodes(): zeroing\n");
	for( int i=0;  i<NUM_LIST;  i++  ) {
		all_lists[i] = NULL;
	}
	printf("freelist_t::free_all_nodes(): ok\n");
}
