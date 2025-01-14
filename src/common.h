/*
  Native File Dialog

  Internal, common across platforms

  http://www.frogtoss.com/labs
 */

#include <stdbool.h>
#include <string.h>



#ifndef _NFD_COMMON_H
#define _NFD_COMMON_H

#define NFD_MAX_STRLEN 256
#define NFD_MAX_FILTERS 1000
#define _NFD_UNUSED(x) ((void)x) 

void *NFDi_Malloc( size_t bytes );
void  NFDi_Free( void *ptr );
void  NFDi_SetError( const char *msg );
void  NFDi_SafeStrncpy( char *dst, const char *src, size_t maxCopy );

#endif

typedef struct {
    char name[NFD_MAX_STRLEN];
    char pattern[NFD_MAX_STRLEN];
} NFDFilter;

typedef struct {
    NFDFilter* filters;
    size_t count;
} NFDFilterList;

static void ExpandFilterPattern(const char* input, char* output) {
    char* pOut = output;
    const char* pIn = input;
    bool isFirst = true;

    while (*pIn && (pOut - output) < NFD_MAX_STRLEN - 4) {
        if (!isFirst) {
            *pOut++ = ';';
        }
        if (*pIn != '*') {
            *pOut++ = '*';
            if (*pIn != '.') {
                *pOut++ = '.';
            }
        }
        while (*pIn && *pIn != ',' && (pOut - output) < NFD_MAX_STRLEN - 2) {
            *pOut++ = *pIn++;
        }
        if (*pIn == ',') {
            ++pIn;
        }
        isFirst = false;
    }
    *pOut = '\0';
}

static void UnescapeString(char* str) {
    char* read = str;
    char* write = str;
    while (*read) {
        if (*read == '\\' && *(read + 1)) {
            *write++ = *(read + 1);
            read += 2;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

// Helper function to parse named filter group [name|extensions]
static bool ParseNamedFilter(const char** p_filterList, char* nameBuf, char* specBuf) {
    if (!p_filterList || !*p_filterList || !nameBuf || !specBuf)
        return false;

    if (**p_filterList != '[')
        return false;

    ++(*p_filterList); // skip [

    // Read name part
    char* pName = nameBuf;
    bool escaped = false;
    size_t nameLen = 0;
    const size_t maxLen = NFD_MAX_STRLEN - 1;

    while (**p_filterList && nameLen < maxLen) {
        if (**p_filterList == ']' && !escaped)
            break;

        if (**p_filterList == '|' && !escaped)
            break;

        if (**p_filterList == '\\' && !escaped) {
            escaped = true;
        } else {
            *pName++ = **p_filterList;
            escaped = false;
            nameLen++;
        }
        ++(*p_filterList);
    }
    *pName = '\0';

    // Kontrola zda jsme nenarazili na konec řetězce
    if (!**p_filterList)
        return false;

    if (**p_filterList != '|')
        return false;

    ++(*p_filterList); // skip |

    // Read extensions part
    char* pSpec = specBuf;
    size_t specLen = 0;

    while (**p_filterList && specLen < maxLen) {
        if (**p_filterList == ']')
            break;

        *pSpec++ = **p_filterList;
        ++(*p_filterList);
        specLen++;
    }
    *pSpec = '\0';

    if (**p_filterList != ']')
        return false;

    ++(*p_filterList); // skip ]

    if (nameLen == 0 || specLen == 0)
        return false;

    UnescapeString(nameBuf);
    return true;
}

static bool ParseFilterList(const char* filterList, NFDFilterList* outList) {
    if (!filterList || strlen(filterList) == 0) {
        outList->filters = NULL;
        outList->count = 0;
        return true;
    }

    // Count filters
    size_t filterCount = 1;
    const char* p = filterList;
    bool inBrackets = false;
    while (*p) {
        if (*p == '[') inBrackets = true;
        else if (*p == ']') inBrackets = false;
        else if (*p == ';' && !inBrackets) ++filterCount;
        ++p;
    }

    if (filterCount > NFD_MAX_FILTERS) {
        NFDi_SetError("Too many filters.");
        return false;
    }

    NFDFilter* filters = (NFDFilter*)NFDi_Malloc(sizeof(NFDFilter) * filterCount);
    if (!filters) {
        NFDi_SetError("Memory allocation failed.");
        return false;
    }

    size_t currentFilter = 0;
    const char* p_filterList = filterList;
    char tempSpec[NFD_MAX_STRLEN];

    while (*p_filterList && currentFilter < filterCount) {
        // Skip whitespace
        while (*p_filterList == ' ' || *p_filterList == '\t')
            ++p_filterList;

        if (*p_filterList == '[') {
            // Named filter
            if (!ParseNamedFilter(&p_filterList,
                                filters[currentFilter].name,
                                tempSpec)) {
                // Skip to next filter on parse error
                while (*p_filterList && *p_filterList != ';')
                    ++p_filterList;
                continue;
            }
            ExpandFilterPattern(tempSpec, filters[currentFilter].pattern);
        } else {
            // Simple filter
            char* pSpec = tempSpec;
            size_t specLen = 0;

            while (*p_filterList && *p_filterList != ';' && *p_filterList != '[' &&
                   specLen < NFD_MAX_STRLEN - 1) {
                *pSpec++ = *p_filterList++;
                ++specLen;
            }
            *pSpec = '\0';

            if (specLen > 0) {
                strcpy(filters[currentFilter].name, tempSpec);
                ExpandFilterPattern(tempSpec, filters[currentFilter].pattern);
            }
        }

        if (*p_filterList == ';')
            ++p_filterList;

        ++currentFilter;
    }

    outList->filters = filters;
    outList->count = currentFilter;
    return true;
}