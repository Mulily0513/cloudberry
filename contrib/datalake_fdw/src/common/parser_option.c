#include "parser_option.h"
#include "postgres.h"
#include "fmgr.h"
#include "foreign/foreign.h"
#include "utils/builtins.h"
#include "utils/formatting.h"
#include "commands/defrem.h"

char* getStringOption(List *options, const char *optionName)
{
    ListCell *lc;

    foreach(lc, options)
    {
        DefElem *def = (DefElem *) lfirst(lc);
        if (pg_strcasecmp(def->defname, optionName) == 0)
        {
            return defGetString(def);
        }
    }

    return NULL;
}

bool getBoolOption(List *options, const char *optionName, bool defaultValue)
{
    char *value = getStringOption(options, optionName);

    if (!value)
        return defaultValue;

    if (pg_strcasecmp(value, "true") == 0)
        return true;
    else if (pg_strcasecmp(value, "false") == 0)
        return false;
    else
        return defaultValue;
}

int getIntOption(List *options, const char *optionName, int defaultValue)
{
    char *value = getStringOption(options, optionName);

    if (!value)
        return defaultValue;

    return atoi(value);
}
