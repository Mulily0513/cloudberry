/*
  Copyright (c) 2009-2017 Dave Gamble and agentcli_cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifndef agentcli_cJSON__h
#define agentcli_cJSON__h

#ifdef __cplusplus
extern "C"
{
#endif

/* project version */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 6
#define CJSON_VERSION_PATCH 0

#include <stddef.h>

/* agentcli_cJSON Types: */
#define agentcli_cJSON_Invalid (0)
#define agentcli_cJSON_False  (1 << 0)
#define agentcli_cJSON_True   (1 << 1)
#define agentcli_cJSON_NULL   (1 << 2)
#define agentcli_cJSON_Number (1 << 3)
#define agentcli_cJSON_String (1 << 4)
#define agentcli_cJSON_Array  (1 << 5)
#define agentcli_cJSON_Object (1 << 6)
#define agentcli_cJSON_Raw    (1 << 7) /* raw json */

#define agentcli_cJSON_IsReference 256
#define agentcli_cJSON_StringIsConst 512

/* The agentcli_cJSON structure: */
typedef struct agentcli_cJSON {
  /* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
  struct agentcli_cJSON *next;
  struct agentcli_cJSON *prev;
  /* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */
  struct agentcli_cJSON *child;

  /* The type of the item, as above. */
  int type;

  /* The item's string, if type==agentcli_cJSON_String  and type == agentcli_cJSON_Raw */
  char *valuestring;
  /* writing to valueint is DEPRECATED, use agentcli_cJSON_SetNumberValue instead */
  int valueint;
  /* The item's number, if type==agentcli_cJSON_Number */
  double valuedouble;

  /* The item's name string, if this item is the child of, or is in the list of subitems of an object. */
  char *string;
} agentcli_cJSON;

typedef struct agentcli_cJSON_Hooks {
  void *(*malloc_fn)(size_t sz);
  void (*free_fn)(void *ptr);
} agentcli_cJSON_Hooks;

typedef int agentcli_cJSON_bool;

#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif
#ifdef __WINDOWS__

/* When compiling for windows, we specify a specific calling convention to avoid issues where we are being called from a project with a different default calling convention.  For windows you have 2 define options:

CJSON_HIDE_SYMBOLS - Define this in the case where you don't want to ever dllexport symbols
CJSON_EXPORT_SYMBOLS - Define this on library build when you want to dllexport symbols (default)
CJSON_IMPORT_SYMBOLS - Define this if you want to dllimport symbol

For *nix builds that support visibility attribute, you can define similar behavior by

setting default visibility to hidden by adding
-fvisibility=hidden (for gcc)
or
-xldscope=hidden (for sun cc)
to CFLAGS

then using the CJSON_API_VISIBILITY flag to "export" the same symbols the way CJSON_EXPORT_SYMBOLS does

*/

/* export symbols by default, this is necessary for copy pasting the C and header file */
#if !defined(CJSON_HIDE_SYMBOLS) && !defined(CJSON_IMPORT_SYMBOLS) && !defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_EXPORT_SYMBOLS
#endif

#if defined(CJSON_HIDE_SYMBOLS)
#define CJSON_PUBLIC(type)   type __stdcall
#elif defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllexport) type __stdcall
#elif defined(CJSON_IMPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllimport) type __stdcall
#endif
#else /* !WIN32 */
#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(CJSON_API_VISIBILITY)
#define CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define CJSON_PUBLIC(type) type
#endif
#endif

/* Limits how deeply nested arrays/objects can be before agentcli_cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

/* returns the version of agentcli_cJSON as a string */
CJSON_PUBLIC(const char*)agentcli_cJSON_Version(void);

/* Supply malloc, realloc and free functions to agentcli_cJSON */
CJSON_PUBLIC(void) agentcli_cJSON_InitHooks(agentcli_cJSON_Hooks *hooks);

/* Memory Management: the caller is always responsible to free the results from all variants of agentcli_cJSON_Parse (with agentcli_cJSON_Delete) and agentcli_cJSON_Print (with stdlib free, agentcli_cJSON_Hooks.free_fn, or agentcli_cJSON_free as appropriate). The exception is agentcli_cJSON_PrintPreallocated, where the caller has full responsibility of the buffer. */
/* Supply a block of JSON, and this returns a agentcli_cJSON object you can interrogate. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_Parse(const char *value);
/* ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed. */
/* If you supply a ptr in return_parse_end and parsing fails, then return_parse_end will contain a pointer to the error so will match agentcli_cJSON_GetErrorPtr(). */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_ParseWithOpts(const char *value,
                                         const char **return_parse_end,
                                         agentcli_cJSON_bool require_null_terminated);

/* Render a agentcli_cJSON entity to text for transfer/storage. */
CJSON_PUBLIC(char *)agentcli_cJSON_Print(const agentcli_cJSON *item);
/* Render a agentcli_cJSON entity to text for transfer/storage without any formatting. */
CJSON_PUBLIC(char *)agentcli_cJSON_PrintUnformatted(const agentcli_cJSON *item);
/* Render a agentcli_cJSON entity to text using a buffered strategy. prebuffer is a guess at the final size. guessing well reduces reallocation. fmt=0 gives unformatted, =1 gives formatted */
CJSON_PUBLIC(char *)agentcli_cJSON_PrintBuffered(const agentcli_cJSON *item, int prebuffer, agentcli_cJSON_bool fmt);
/* Render a agentcli_cJSON entity to text using a buffer already allocated in memory with given length. Returns 1 on success and 0 on failure. */
/* NOTE: agentcli_cJSON is not always 100% accurate in estimating how much memory it will use, so to be safe allocate 5 bytes more than you actually need */
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_PrintPreallocated(agentcli_cJSON *item, char *buffer, const int length, const agentcli_cJSON_bool format);
/* Delete a agentcli_cJSON entity and all subentities. */
CJSON_PUBLIC(void) agentcli_cJSON_Delete(agentcli_cJSON *c);

/* Returns the number of items in an array (or object). */
CJSON_PUBLIC(int) agentcli_cJSON_GetArraySize(const agentcli_cJSON *array);
/* Retrieve item number "item" from array "array". Returns NULL if unsuccessful. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_GetArrayItem(const agentcli_cJSON *array, int index);
/* Get item "string" from object. Case insensitive. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_GetObjectItem(const agentcli_cJSON *const object, const char *const string);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_GetObjectItemCaseSensitive(const agentcli_cJSON *const object, const char *const string);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_HasObjectItem(const agentcli_cJSON *object, const char *string);
/* For analysing failed parses. This returns a pointer to the parse error. You'll probably need to look a few chars back to make sense of it. Defined when agentcli_cJSON_Parse() returns 0. 0 when agentcli_cJSON_Parse() succeeds. */
CJSON_PUBLIC(const char *)agentcli_cJSON_GetErrorPtr(void);

/* These functions check the type of an item */
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsInvalid(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsFalse(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsTrue(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsBool(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsNull(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsNumber(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsString(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsArray(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsObject(const agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_IsRaw(const agentcli_cJSON *const item);

/* These calls create a agentcli_cJSON item of the appropriate type. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateNull(void);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateTrue(void);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateFalse(void);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateBool(agentcli_cJSON_bool boolean);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateNumber(double num);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateString(const char *string);
/* raw json */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateArray(void);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateObject(void);

/* These utilities create an Array of count items. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_CreateStringArray(const char **strings, int count);

/* Append item to the specified array/object. */
CJSON_PUBLIC(void) agentcli_cJSON_AddItemToArray(agentcli_cJSON *array, agentcli_cJSON *item);
CJSON_PUBLIC(void) agentcli_cJSON_AddItemToObject(agentcli_cJSON *object, const char *string, agentcli_cJSON *item);
/* Use this when string is definitely const (i.e. a literal, or as good as), and will definitely survive the agentcli_cJSON object.
 * OSS_WARNING: When this function was used, make sure to always check that (item->type & agentcli_cJSON_StringIsConst) is zero before
 * writing to `item->string` */
CJSON_PUBLIC(void) agentcli_cJSON_AddItemToObjectCS(agentcli_cJSON *object, const char *string, agentcli_cJSON *item);
/* Append reference to item to the specified array/object. Use this when you want to add an existing agentcli_cJSON to a new agentcli_cJSON, but don't want to corrupt your existing agentcli_cJSON. */
CJSON_PUBLIC(void) agentcli_cJSON_AddItemReferenceToArray(agentcli_cJSON *array, agentcli_cJSON *item);
CJSON_PUBLIC(void) agentcli_cJSON_AddItemReferenceToObject(agentcli_cJSON *object, const char *string, agentcli_cJSON *item);

/* Remove/Detatch items from Arrays/Objects. */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_DetachItemViaPointer(agentcli_cJSON *parent, agentcli_cJSON *const item);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_DetachItemFromArray(agentcli_cJSON *array, int which);
CJSON_PUBLIC(void) agentcli_cJSON_DeleteItemFromArray(agentcli_cJSON *array, int which);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_DetachItemFromObject(agentcli_cJSON *object, const char *string);
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_DetachItemFromObjectCaseSensitive(agentcli_cJSON *object, const char *string);
CJSON_PUBLIC(void) agentcli_cJSON_DeleteItemFromObject(agentcli_cJSON *object, const char *string);
CJSON_PUBLIC(void) agentcli_cJSON_DeleteItemFromObjectCaseSensitive(agentcli_cJSON *object, const char *string);

/* Update array items. */
CJSON_PUBLIC(void) agentcli_cJSON_InsertItemInArray(agentcli_cJSON *array,
                                           int which,
                                           agentcli_cJSON *newitem); /* Shifts pre-existing items to the right. */
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_ReplaceItemViaPointer(agentcli_cJSON *const parent, agentcli_cJSON *const item, agentcli_cJSON *replacement);
CJSON_PUBLIC(void) agentcli_cJSON_ReplaceItemInArray(agentcli_cJSON *array, int which, agentcli_cJSON *newitem);
CJSON_PUBLIC(void) agentcli_cJSON_ReplaceItemInObject(agentcli_cJSON *object, const char *string, agentcli_cJSON *newitem);
CJSON_PUBLIC(void) agentcli_cJSON_ReplaceItemInObjectCaseSensitive(agentcli_cJSON *object, const char *string, agentcli_cJSON *newitem);

/* Duplicate a agentcli_cJSON item */
CJSON_PUBLIC(agentcli_cJSON *)agentcli_cJSON_Duplicate(const agentcli_cJSON *item, agentcli_cJSON_bool recurse);
/* Duplicate will create a new, identical agentcli_cJSON item to the one you pass, in new memory that will
need to be released. With recurse!=0, it will duplicate any children connected to the item.
The item->next and ->prev pointers are always zero on return from Duplicate. */
/* Recursively compare two agentcli_cJSON items for equality. If either a or b is NULL or invalid, they will be considered unequal.
 * case_sensitive determines if object keys are treated case sensitive (1) or case insensitive (0) */
CJSON_PUBLIC(agentcli_cJSON_bool) agentcli_cJSON_Compare(const agentcli_cJSON *const a, const agentcli_cJSON *const b, const agentcli_cJSON_bool case_sensitive);

CJSON_PUBLIC(void) agentcli_cJSON_Minify(char *json);

/* Macros for creating things quickly. */
#define agentcli_cJSON_AddNullToObject(object, name) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateNull())
#define agentcli_cJSON_AddTrueToObject(object, name) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateTrue())
#define agentcli_cJSON_AddFalseToObject(object, name) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateFalse())
#define agentcli_cJSON_AddBoolToObject(object, name, b) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateBool(b))
#define agentcli_cJSON_AddNumberToObject(object, name, n) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateNumber(n))
#define agentcli_cJSON_AddStringToObject(object, name, s) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateString(s))
#define agentcli_cJSON_AddRawToObject(object, name, s) agentcli_cJSON_AddItemToObject(object, name, agentcli_cJSON_CreateRaw(s))

/* When assigning an integer value, it needs to be propagated to valuedouble too. */
#define agentcli_cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
/* helper for the agentcli_cJSON_SetNumberValue macro */
CJSON_PUBLIC(double) agentcli_cJSON_SetNumberHelper(agentcli_cJSON *object, double number);
#define agentcli_cJSON_SetNumberValue(object, number) ((object != NULL) ? agentcli_cJSON_SetNumberHelper(object, (double)number) : (number))

/* Macro for iterating over an array or object */
#define agentcli_cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* malloc/free objects using the malloc/free functions that have been set with agentcli_cJSON_InitHooks */
CJSON_PUBLIC(void *)agentcli_cJSON_malloc(size_t size);
CJSON_PUBLIC(void) agentcli_cJSON_free(void *object);

#ifdef __cplusplus
}
#endif

#endif
