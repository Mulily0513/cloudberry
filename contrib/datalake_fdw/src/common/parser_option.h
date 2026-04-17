#ifndef PARSER_OPTION_H
#define PARSER_OPTION_H

#include "postgres.h"
#include "nodes/pg_list.h"


char* getStringOption(List *options, const char *optionName);
bool getBoolOption(List *options, const char *optionName, bool defaultValue);
int getIntOption(List *options, const char *optionName, int defaultValue);

#endif