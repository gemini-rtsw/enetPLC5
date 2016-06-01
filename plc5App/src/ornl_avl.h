#ifndef ___avl_h
#define ___avl_h 1

#ifdef __cplusplus
extern "C" {
#endif

/***
* avl.h - public include file for avl tree routines in avl.c
*/

#define AVL_FIELDS( super_struct_tag_name )\
  unsigned int __avl_depth;\
  int __avl_bf;\
  struct super_struct_tag_name *__avl_right;\
  struct super_struct_tag_name *__avl_left;\

typedef void *AVL_HANDLE;
typedef int (*AVL_INT_FUNC)( void *, void * );

#ifdef VMS
globalvalue AVL_BADHNDL;
globalvalue AVL_LBERR;
globalvalue AVL_NOMEM;
globalvalue AVL_NULLEFT;
globalvalue AVL_NULRIGHT;
globalvalue AVL_NULROOT;
globalvalue AVL_RBERR;
globalvalue AVL_STKOFL;
globalvalue AVL_SUCCESS;
globalvalue AVL_UNKBF;
#endif

#ifndef VMS
#define AVL_BADHNDL 0x30065002
#define AVL_BADHNDL_IDX 0
#define AVL_LBERR 0x30065004
#define AVL_LBERR_IDX 1
#define AVL_NOMEM 0x30065006
#define AVL_NOMEM_IDX 2
#define AVL_NULLEFT 0x30065008
#define AVL_NULLEFT_IDX 3
#define AVL_NULRIGHT 0x3006500A
#define AVL_NULRIGHT_IDX 4
#define AVL_NULROOT 0x3006500C
#define AVL_NULROOT_IDX 5
#define AVL_RBERR 0x3006500E
#define AVL_RBERR_IDX 6
#define AVL_STKOFL 0x30065010
#define AVL_STKOFL_IDX 7
#define AVL_SUCCESS 0x10065001
#define AVL_SUCCESS_IDX 8
#define AVL_UNKBF 0x30065012
#define AVL_UNKBF_IDX 9
#endif

int avl_init_tree(
  AVL_INT_FUNC comp_node_func,
  AVL_INT_FUNC comp_item_func,
  AVL_INT_FUNC copy_node_func,
  AVL_HANDLE *handle
);

int avl_destroy(
  AVL_HANDLE handle
);

int avl_dup_handle(
  AVL_HANDLE handle,
  AVL_HANDLE *dup_handle
);

int avl_insert_node(
  AVL_HANDLE handle,
  void *new_node,
  int *duplicate
);

int avl_delete_node(
  AVL_HANDLE handle,
  void **node		/* this parameter is a pointer to your record passed */
);			/* by reference */

int avl_get_match(
  AVL_HANDLE handle,
  void *key_value,
  void **node
);

int avl_get_first(
  AVL_HANDLE handle,
  void **node
);

int avl_get_next(
  AVL_HANDLE handle,
  void **node
);

int avl_get_last(
  AVL_HANDLE handle,
  void **node
);

int avl_get_prev(
  AVL_HANDLE handle,
  void **node
);

void avl_find_depth(
  AVL_HANDLE handle,
  int *depth,
  int *shortest_branch
);

#ifdef __cplusplus
}
#endif

#endif
