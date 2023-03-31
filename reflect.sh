#!/bin/bash
set -eu
{
echo '#ifndef _SLURMDB_DEFS_H'
echo '#define _SLURMDB_DEFS_H'
echo 'typedef enum {'
RESULT=$(gdb sacct -x reflect.lst < /dev/null | grep "=")
echo $RESULT | tr ' ' ','
echo '} tres_types_t;'
echo 'const char *reflect_tres_name(int num) {'
echo 'switch(num) {'
echo $RESULT | tr ' ' '\n' | awk -F= '{printf "case %s: return \"%s\";\n", $2, $1}'
echo 'default: return "NOTFOUND";'
echo '}'
echo '}'
echo '#endif'
} > $(dirname $0)/include/tresdef.h
