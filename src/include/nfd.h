/*
  Native File Dialog

  User API

  http://www.frogtoss.com/labs
 */


#ifndef _NFD_H
#define _NFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* dll support */
#ifdef _WIN32
#  ifdef MODULE_API_EXPORTS
#    define NATIVE_FILE_DIALOG_MODULE_API __declspec(dllexport)
#  else
#    define NATIVE_FILE_DIALOG_MODULE_API __declspec(dllimport)
#  endif
#else
#  define NATIVE_FILE_DIALOG_MODULE_API
#endif

/* denotes UTF-8 char */
typedef char nfdchar_t;

/* opaque data structure -- see NFD_PathSet_* */
typedef struct {
    nfdchar_t* buf;
    size_t*    indices; /* byte offsets into buf */
    size_t     count;   /* number of indices into buf */
} nfdpathset_t;

typedef enum {
    NFD_ERROR, /* programmatic error */
    NFD_OKAY,  /* user pressed okay, or successful return */
    NFD_CANCEL /* user pressed cancel */
} nfdresult_t;


/* nfd_<targetplatform>.c */

/* single file open dialog */    
NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_OpenDialog(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath);

/* multiple file open dialog */    
NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_OpenDialogMultiple(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdpathset_t *outPaths);

/* save dialog */
NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_SaveDialog(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath);


/* select folder dialog */
NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_PickFolder(
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath);

/* nfd_common.c */

/* get last error -- set when nfdresult_t returns NFD_ERROR */
NATIVE_FILE_DIALOG_MODULE_API const char *NFD_GetError(void);
/* get the number of entries stored in pathSet */
NATIVE_FILE_DIALOG_MODULE_API size_t NFD_PathSet_GetCount(const nfdpathset_t *pathSet);
/* Get the UTF-8 path at offset index */
NATIVE_FILE_DIALOG_MODULE_API nfdchar_t *NFD_PathSet_GetPath(const nfdpathset_t *pathSet, size_t index);
/* Free the pathSet */    
NATIVE_FILE_DIALOG_MODULE_API void NFD_PathSet_Free(nfdpathset_t *pathSet);

NATIVE_FILE_DIALOG_MODULE_API void NFD_Dummy();

NATIVE_FILE_DIALOG_MODULE_API void *NFD_Malloc(size_t bytes);

NATIVE_FILE_DIALOG_MODULE_API void NFD_Free(void* ptr);


#ifdef __cplusplus
}
#endif

#endif
