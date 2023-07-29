#!/bin/bash
set -eu
SELF=$(dirname $0)
LSTDIR=${SELF}/scripts/reflect
{
echo '#ifndef _SLURMDB_DEFS_H'
echo '#define _SLURMDB_DEFS_H'
echo 'typedef enum {'
RESULT=$(gdb sacct -x "${LSTDIR}/slurm.lst" < /dev/null | grep "=")
echo $RESULT | tr ' ' ','
echo '} tres_types_t;'
echo 'const char *reflect_tres_name(int num);'
RESULT_MULTILINE=$(echo $RESULT | tr ' ' '\n')
RESULT_SORTED=$(awk -F= '{printf "%s\n", $2}' <<< $RESULT_MULTILINE | sort -n)
echo "#define TRES_MIN $(head -n 1 <<< $RESULT_SORTED)"
echo "#define TRES_MAX $(tail -n 1 <<< $RESULT_SORTED)"
echo '#define TRES_SIZE (TRES_MAX-TRES_MIN+1)'
echo '#define TRES_IDX(ENUM_ENTRY) ((size_t)(ENUM_ENTRY)-TRES_MIN)'
echo '#define TRES_ENUM(IDX) (IDX+TRES_MIN)'
echo '#endif'
} > ${SELF}/include/tresdef.h
{
echo 'const char *reflect_tres_name(int num) {'
echo 'switch(num) {'
awk -F= '{printf "case %s: return \"%s\";\n", $2, $1}' <<< $RESULT_MULTILINE
echo 'default: return "NOTFOUND";'
echo '}'
echo '}'
} > ${SELF}/include/tresdef.cpp
