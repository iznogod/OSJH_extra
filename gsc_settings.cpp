/* Includes */
#include "gsc_settings.hpp"

/* Defines */

#define SETTINGS_ALLOCATE_BLOCK     8 // How many settings are allocated at once when we need to allocate more memory

/* Constant variables */
const static settings_type_map_t settingsTypeMapping[] = {
    {"string",  TYPE_STR},
    {"int",     TYPE_INT},
    {"float",   TYPE_FLOAT},
    {"bool",    TYPE_BOOL},
    {"strlist", TYPE_STRLIST},
};

/* Global variables */
static setting_t *settingsPerPlayer[MAX_CLIENTS]; // Dynamic per-player list of settings
static int totalSettingsAllocatedCount; // How many total settings we can fit before having to re-allocate memory
static int totalSettingsCount; // How many settings currently exist
static bool initialized = false; // Whether the "settings" module has been initialized (this is done when adding the first setting)

/* Local functions */
static setting_type_t getSettingTypeFromStr(const char *type)
{
    if(type == NULL)
        return TYPE_UNKNOWN;
    
    for(int i = 0; i < (int)(sizeof(settingsTypeMapping) / sizeof(settingsTypeMapping[0])); i++)
    {
        if(strlen(settingsTypeMapping[i].typeStr) == strlen(type))
            if(strcmp(settingsTypeMapping[i].typeStr, type) == 0)
                return settingsTypeMapping[i].type;
    }
    
    return TYPE_UNKNOWN;
}

static setting_t *getSettingPtr(int playerId, const char *settingName)
{
    if(!settingName)
    {
        // Temporary log line while testing
        printf("[%s::%s] - setting not found (%s)\n", __FILE__, __func__, settingName);
        return NULL;
    }
    
    if(strlen(settingName) >= SETTINGS_MAX_NAME_LEN)
    {
        // Temporary log line while testing
        printf("[%s::%s] - setting name too long (%s)\n", __FILE__, __func__, settingName);
        return NULL;
    }
    
    // Find the correct setting
    for(int i = 0; i < totalSettingsCount; i++)
    {
        assert(settingsPerPlayer[playerId][i] != NULL); // How could the setting not exist if totalSettingsCount says it does?
        
        // Is the name of the same length as the requested setting name?
        if(strlen(settingName) == strlen(settingsPerPlayer[playerId][i].name))
        {
            // Is the name the same as that of the requested setting?
            if(strcmp(settingName, settingsPerPlayer[playerId][i].name) == 0)
                return &settingsPerPlayer[playerId][i];
        }
    }
    
    return NULL;
}

static bool validateString(char *str, int min, int max)
{
    // Ensure that the string is within the range of allowed lengths
    if(((int)strlen(str) < min) || ((int)strlen(str) > max))
    {
        // Temporary log line while testing
        printf("[%s::%s] - validation failed for string value (%s)\n", __FILE__, __func__, str);
        return false;
    }
    
    return true;
}

static bool validateInt(int val, int min, int max)
{
    // Ensure that the int is within the range of allowed values
    if((val < min) || (val > max))
    {
        // Temporary log line while testing
        printf("[%s::%s] - validation failed for int value (%d)\n", __FILE__, __func__, val);
        return false;
    }
    
    return true;
}

static bool validateBool(int val)
{
    return (val == 0) || (val == 1);
}

static bool validateFloat(float val, float min, float max)
{
    // Ensure that the float is within the range of allowed values
    if((val < min) || (val > max)) // If this proves to be a problem we need to use FLT_EPSILON compare
    {
        // Temporary log line while testing
        printf("[%s::%s] - validation failed for float value (%f)\n", __FILE__, __func__, val);
        return false;
    }
    
    return true;
}

static bool validateAndApplySetting(setting_t *ptr_setting, void *newVal, bool newSetting)
{
    switch(ptr_setting->type) {
        case TYPE_STR:
        {
            char *newValStr = *(char **)newVal;
            
            // Ensure that the string is within the range of allowed lengths
            if(!validateString(newValStr, ptr_setting->s_str.minLen, ptr_setting->s_str.maxLen))
                return false;
            
            if(newSetting)
            {
                ptr_setting->s_str.val = (char *)malloc(ptr_setting->s_str.maxLen + 1);
                ptr_setting->s_str.defaultVal = (char *)malloc(ptr_setting->s_str.maxLen + 1);
                
                memcpy(ptr_setting->s_str.defaultVal, newValStr, strlen(newValStr) + 1);
            }
            
            memcpy(ptr_setting->s_str.val, newValStr, strlen(newValStr) + 1);
        }
        break;
            
        case TYPE_INT:
        {
            int newValInt = *(int *)newVal;
            
            // Ensure that the int is within the range of allowed values
            if(!validateInt(newValInt, ptr_setting->s_int.minVal, ptr_setting->s_int.maxVal))
                return false;
            
            ptr_setting->s_int.val = newValInt;
            
            if(newSetting)
                ptr_setting->s_int.defaultVal = newValInt;
        }
        break;
            
        case TYPE_FLOAT:
        {
            float newValFloat = *(float *)newVal;
            
            // Ensure that the float is within the range of allowed values
            if(!validateFloat(newValFloat, ptr_setting->s_float.minVal, ptr_setting->s_float.maxVal))
                return false;
            
            ptr_setting->s_float.val = newValFloat;
            
            if(newSetting)
                ptr_setting->s_float.defaultVal = newValFloat;
        }
        break;
            
        case TYPE_BOOL:
        {
            int newValBool = *(int *)newVal;
            
            if(!validateBool(newValBool))
                return false;
            
            ptr_setting->s_bool.val = newValBool;
            
            if(newSetting)
                ptr_setting->s_bool.defaultVal = newValBool;
        }
        break;
            
        case TYPE_STRLIST: // createNewSetting(char *name, char *typeStr, char *str1, char *str2, ...)
        {
            // TODO: Add later
            // Temporary log line while testing
            printf("[%s::%s] - type strlist is not yet implemented (%s)\n", __FILE__, __func__, ptr_setting->name);
            return false;
        }
        break;
            
        default: // Forgot to add new type to this switch statement, loser ;)
            assert(false);
            return false;
    }
    
    return true;
}

/* API functions */

void Gsc_CreateNewSetting(void)
{
    // Initialize the module if necessary
    if(!initialized)
    {
        for(int i = 0; i < MAX_CLIENTS; i++)
            settingsPerPlayer[i] = (setting_t *)calloc(SETTINGS_ALLOCATE_BLOCK, sizeof(setting_t));
        
        totalSettingsAllocatedCount = SETTINGS_ALLOCATE_BLOCK; // We have allocated space for this many settings
        totalSettingsCount = 0; // We haven't stored any settings yet
        initialized = true;
    }
    
    // These are common for all settings
    char *name, *typeStr;
    stackGetParamString(0, &name);
    stackGetParamString(1, &typeStr);
    
    // Name has a static length as this will be faster when using short names
    if(strlen(name) >= SETTINGS_MAX_NAME_LEN)
    {
        // Temporary log line while testing
        printf("[%s::%s] - settings name too long (%s)\n", __FILE__, __func__, name);
        stackPushUndefined();
        return;
    }
    
    // Ensure the setting is of a valid type before we apply it
    setting_type_t settingType = getSettingTypeFromStr(typeStr);
    if(settingType == TYPE_UNKNOWN)
    {
        // Temporary log line while testing
        printf("[%s::%s] - unknown settings type (%s)\n", __FILE__, __func__, typeStr);
        stackPushUndefined();
        return;
    }
    
    // Check if the setting already exists
    for(int i = 0; i < totalSettingsCount; i++)
    {
        // Settings can be adjusted per-player, but all players have each setting (be it with default or custom value)
        // That's why we can just use index 0 to check if the setting exists
        if(settingsPerPlayer[0]->name != NULL)
        {
            if(strlen(name) == strlen(settingsPerPlayer[0]->name))
            {
                if(strcmp(name, settingsPerPlayer[0]->name) == 0)
                {
                    // Temporary log line while testing
                    printf("[%s::%s] - setting already exists (%s)\n", __FILE__, __func__, name);
                    stackPushUndefined();
                    return;
                }
            }
        }
    }
    
    // Check if we have enough memory allocated to store another setting
    if(totalSettingsAllocatedCount <= totalSettingsCount)
    {
        // We don't have enough memory to store another setting without re-allocating first
        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            settingsPerPlayer[i] = (setting_t *)realloc(settingsPerPlayer[i], (totalSettingsAllocatedCount + SETTINGS_ALLOCATE_BLOCK) * sizeof(setting_t));
            assert(settingsPerPlayer[i]);
        }
        
        // We allocated extra memory
        totalSettingsAllocatedCount += SETTINGS_ALLOCATE_BLOCK;
    }
    
    // Create the base of the new setting, but don't apply it in the actual global variable until we're sure
    setting_t newSetting = {0};
    memcpy(newSetting.name, name, strlen(name) + 1);
    newSetting.type = settingType;
    
    // Perform type-specific handling
    switch(settingType) {
        case TYPE_STR: // createNewSetting(char *name, char *typeStr, int minLen, int maxLen, char *defaultVal)
        {
            // String types have a minimum and maximum length (EXCLUDING terminator)
            int minLen, maxLen;
            stackGetParamInt(2, &minLen);
            stackGetParamInt(3, &maxLen);
            newSetting.s_str.minLen = minLen;
            newSetting.s_str.maxLen = maxLen;
            
            char *defaultVal;
            stackGetParamString(4, &defaultVal);
            
            if(!validateAndApplySetting(&newSetting, (void *)&defaultVal, true))
            {
                stackPushUndefined();
                return;
            }
            
            stackPushString(defaultVal);
        }
        break;
            
        case TYPE_INT: // createNewSetting(char *name, char *typeStr, int minVal, int maxVal, int defaultVal)
        {
            // Int types have a minimum and maximum value
            int minVal, maxVal;
            stackGetParamInt(2, &minVal);
            stackGetParamInt(3, &maxVal);
            
            newSetting.s_int.minVal = minVal;
            newSetting.s_int.maxVal = maxVal;
            
            int defaultVal;
            stackGetParamInt(4, &defaultVal);
            
            if(!validateAndApplySetting(&newSetting, (void *)&defaultVal, true))
            {
                stackPushUndefined();
                return;
            }
            
            stackPushInt(defaultVal);
        }
        break;
            
        case TYPE_FLOAT: // createNewSetting(char *name, char *typeStr, float minVal, float maxVal, float defaultVal)
        {
            // Float types have a minimum and maximum value
            float minVal, maxVal;
            stackGetParamFloat(2, &minVal);
            stackGetParamFloat(3, &maxVal);
            
            newSetting.s_float.minVal = minVal;
            newSetting.s_float.maxVal = maxVal;
            
            float defaultVal;
            stackGetParamFloat(4, &defaultVal);
            
            if(!validateAndApplySetting(&newSetting, (void *)&defaultVal, true))
            {
                stackPushUndefined();
                return;
            }
            
            stackPushFloat(defaultVal);
        }
        break;
            
        case TYPE_BOOL: // createNewSetting(char *name, char *typeStr, bool defaultVal)
        {
            // Bool types only have a default value, no requirements other than it must be true or false
            int defaultVal;
            stackGetParamInt(2, &defaultVal);
            
            if(!validateAndApplySetting(&newSetting, (void *)&defaultVal, true))
            {
                stackPushUndefined();
                return;
            }
            
            stackPushInt(defaultVal);
        }
        break;
            
        case TYPE_STRLIST: // createNewSetting(char *name, char *typeStr, char *str1, char *str2, ...)
        {
            // TODO: Add later
            // Temporary log line while testing
            printf("[%s::%s] - type strlist is not yet implemented (%s)\n", __FILE__, __func__, name);
            stackPushUndefined();
            return;
        }
        break;
            
        default: // Forgot to add new type to this switch statement, loser ;)
            assert(false);
            return;
    }
    
    // Add the setting for each player
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        // This access with totalSettingsCount is OK because we verified we allocated enough memory at start of function
        memcpy(&settingsPerPlayer[i][totalSettingsCount], &newSetting, sizeof(settingsPerPlayer[i][totalSettingsCount]));
        
        // For strings we've only allocated memory once, now we need to allocate it for each player
        if(newSetting.type == TYPE_STR)
        {
            settingsPerPlayer[i][totalSettingsCount].s_str.val = (char *)malloc(newSetting.s_str.maxLen + 1);
            settingsPerPlayer[i][totalSettingsCount].s_str.defaultVal = (char *)malloc(newSetting.s_str.maxLen + 1);
            
            memcpy(settingsPerPlayer[i][totalSettingsCount].s_str.val, newSetting.s_str.val, newSetting.s_str.maxLen + 1);
            memcpy(settingsPerPlayer[i][totalSettingsCount].s_str.defaultVal, newSetting.s_str.defaultVal, newSetting.s_str.maxLen + 1);
        }
    }
    
    // Now we can free the initial memory we may have reserved
    if(newSetting.type == TYPE_STR)
    {
        free(newSetting.s_str.val);
        free(newSetting.s_str.defaultVal);
    }
    
    // We added an extra setting
    totalSettingsCount++;
    stackPushString(name);
}

/**
 * Call this on map start. This removes all settings for all players.
 */
void Gsc_DeleteAllSettings(void)
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(settingsPerPlayer[i] != NULL)
        {
            for(int j = 0; j < totalSettingsCount; j++)
            {
                switch(settingsPerPlayer[i][j].type) {
                    case TYPE_STR:
                    {
                        free(settingsPerPlayer[i][j].s_str.val);
                        settingsPerPlayer[i][j].s_str.val = NULL;
                        
                        free(settingsPerPlayer[i][j].s_str.defaultVal);
                        settingsPerPlayer[i][j].s_str.defaultVal = NULL;
                    }
                    break;
                        
                    case TYPE_STRLIST:
                    {
                        // TODO: Add later
                        assert(false);
                        return;
                    }
                    break;
                    
                    default: // Rest of the cases have nothing to free
                        break;
                }
            }
        }
        
        free(settingsPerPlayer[i]);
        settingsPerPlayer[i] = NULL;
    }
    
    totalSettingsAllocatedCount = 0;
    totalSettingsCount = 0;
    
    initialized = false;
}

/**
 * Set an existing setting to a specific value for a player
 */
void Gsc_SetSetting(int id)
{
    char *name; // Setting name
    stackGetParamString(0, &name);
    
    setting_t *ptr_setting = getSettingPtr(id, name);
    
    if(!ptr_setting)
    {
        stackPushUndefined();
        return;
    }
    
    // We may need to convert string setting to int, float or bool when provided by a user
    if(stackGetParamType(1) == VAR_STRING)
    {
        char *newValStr;
        stackGetParamString(1, &newValStr);
                
        switch(ptr_setting->type) {
            case TYPE_STR:
            {
                if(!validateAndApplySetting(ptr_setting, (void *)&newValStr, false))
                {
                    stackPushUndefined();
                    return;
                }
                
                stackPushString(newValStr);
            }
            break; // This is OK, a string is a string
                
            case TYPE_INT: // We were given a string, but we expect an integer
            case TYPE_BOOL: // We treat these the same here
            {
                char *endPtr;
                int newVal = strtol(newValStr, &endPtr, 10);
                
                // Check if the function error'd
                if(endPtr == newValStr)
                {
                    // Temporary log line while testing
                    printf("[%s::%s] - (%s) new value could not be converted (%s)\n", __FILE__, __func__, name, newValStr);
                    stackPushUndefined();
                    return;
                }
                
                if(!validateAndApplySetting(ptr_setting, (void *)&newVal, false))
                {
                    stackPushUndefined();
                    return;
                }
                
                stackPushInt(newVal);
            }
            break;
            
            case TYPE_FLOAT: // We were given a string, but we expect a float
            {
                char *endPtr;
                double newValD = strtod(newValStr, &endPtr);
                
                // Check if the function error'd
                if(endPtr == newValStr)
                {
                    // Temporary log line while testing
                    printf("[%s::%s] - (%s) new value could not be converted (%s)\n", __FILE__, __func__, name, newValStr);
                    stackPushUndefined();
                    return;
                }
                
                float newVal = (float)newValD;
                
                if(!validateAndApplySetting(ptr_setting, (void *)&newVal, false))
                {
                    stackPushUndefined();
                    return;
                }
                
                stackPushInt(newVal);
            }
            break;
            
            default:
            {
                assert(false);
            }
            break;
        }
        
        // We handled all cases of string
        return;
    }
    
    // We handled the entries if they were provided as string
    assert(ptr_setting->type != TYPE_STR);
    
    switch(ptr_setting->type) {
        // We already handled TYPE_STR above
        
        case TYPE_INT:
        {
            // Int types have a minimum and maximum value
            int newVal;
            stackGetParamInt(1, &newVal);
            
            if(!validateAndApplySetting(ptr_setting, (void *)&newVal, false))
            {
                stackPushUndefined();
                return;
            }
                
            stackPushInt(newVal);
        }
        break;
            
        case TYPE_FLOAT:
        {
            // Float types have a minimum and maximum value
            float newVal;
            stackGetParamFloat(1, &newVal);
            
            if(!validateAndApplySetting(ptr_setting, (void *)&newVal, false))
            {
                stackPushUndefined();
                return;
            }
                
            stackPushFloat(newVal);
        }
        break;
            
        case TYPE_BOOL:
        {
            // Bool types only have a default value, no requirements other than it must be true or false
            int newVal;
            stackGetParamInt(1, &newVal);
            
            if(!validateAndApplySetting(ptr_setting, (void *)&newVal, false))
            {
                stackPushUndefined();
                return;
            }
            
            stackPushInt(newVal);
        }
        break;
            
        case TYPE_STRLIST:
        {
            // TODO: Add later
            assert(false);
            return;
        }
        break;
            
        default:
            assert(false);
            return;
    }
}

/**
 * Get an existing setting for a specific player
 */
void Gsc_GetSetting(int id)
{
    char *name;
    stackGetParamString(0, &name);
    
    setting_t *ptr_setting = getSettingPtr(id, name);
    
    if(!ptr_setting)
    {
        stackPushUndefined();
        return;
    }
    
    switch(ptr_setting->type) {
        case TYPE_STR:
            stackPushString(ptr_setting->s_str.val);
            break;
            
        case TYPE_INT:
            stackPushInt(ptr_setting->s_int.val);
            break;
            
        case TYPE_FLOAT:
            stackPushFloat(ptr_setting->s_float.val);
            break;
            
        case TYPE_BOOL:
            stackPushInt(ptr_setting->s_bool.val);
            break;
            
        case TYPE_STRLIST:
        {
            // TODO: Add later
            assert(false);
            return;
        }
        break;
            
        default:
            assert(false);
            return;
    }
}

/**
 * Clear the settings for a given clientNum for when a new player connects in that slot
 */
void Gsc_ClearSettings(int id)
{
    for(int i = 0; i < totalSettingsCount; i++)
    {
        setting_t *ptr_setting = &settingsPerPlayer[id][i];
        if(!ptr_setting)
            continue;
        
        switch(ptr_setting->type) {
            case TYPE_STR:
                memcpy(ptr_setting->s_str.val, ptr_setting->s_str.defaultVal, ptr_setting->s_str.maxLen + 1);
                break;
                
            case TYPE_INT:
                ptr_setting->s_int.val = ptr_setting->s_int.defaultVal;
                break;
                
            case TYPE_FLOAT:
                ptr_setting->s_float.val = ptr_setting->s_float.defaultVal;
                break;
                
            case TYPE_BOOL:
                ptr_setting->s_bool.val = ptr_setting->s_bool.defaultVal;
                break;
                
            case TYPE_STRLIST:
            {
                // TODO: Add later
                assert(false);
                return;
            }
            break;
                
            default:
                assert(false);
                return;
        }
    }
}