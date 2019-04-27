#ifndef _GSC_SETTINGS_HPP_
#define _GSC_SETTINGS_HPP_

/* Includes */
#include "shared.hpp"

/* Defines */
#define SETTINGS_MAX_NAME_LEN   32 // Includes terminator

/* Types */
typedef enum { // Struct containing the definition for each setting type
    TYPE_STR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STRLIST,
    TYPE_UNKNOWN,
} setting_type_t;

/** Structs for each setting type */
typedef struct {
    char *val;
    int minLen;
    int maxLen;
    char *defaultVal;
} setting_string_t;

typedef struct {
    int val;
    int minVal;
    int maxVal;
    int defaultVal;
} setting_int_t;

typedef struct {
    float val;
    float minVal;
    float maxVal;
    float defaultVal;
} setting_float_t;

typedef struct {
    bool val;
    bool defaultVal;
} setting_bool_t;

typedef struct {
    char *val;
    char **list;
    int listLen;
} setting_strlist_t;
/** End structs for each setting type */

typedef struct { // Actual setting struct which will be maintained as a per-player settings list
    char name[SETTINGS_MAX_NAME_LEN]; // Should be enough for a name
    setting_type_t type;
    
    union {
        setting_string_t s_str;
        setting_int_t s_int;
        setting_float_t s_float;
        setting_bool_t s_bool;
        setting_strlist_t s_strlist;
    };
} setting_t;

typedef struct {
    const char *typeStr;
    const setting_type_t type;
} settings_type_map_t;

/* Prototypes */
void Gsc_CreateNewSetting(void);
void Gsc_DeleteAllSettings(void);
void Gsc_SetSetting(int id);
void Gsc_GetSetting(int id);
void Gsc_ClearSettings(int id);

#endif
