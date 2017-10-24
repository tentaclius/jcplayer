#!/bin/sh

SRCDIR=src

export JJ_MODULE=$(echo "$1" | sed -e 's/.cpp$//')
export JJ_MAKE_TARGET="${JJ_MODULE}.so"
export JJ_MODULE_SOURCE="${JJ_MODULE}.cpp"
export JJ_MAIN_SOURCE="${JJ_MODULE}_m.cpp"

if ! test -f "$JJ_MODULE_SOURCE"
then
   echo "The module $JJ_MODULE_SOURCE does not exist"
   exit 1
fi

cat $SRCDIR/script.cpp | sed -e "s/[ \t]*JJ_MODULE_SOURCE/#include \"$JJ_MODULE_SOURCE\"/" > ${JJ_MAIN_SOURCE}

make -f Makefile ${JJ_MAKE_TARGET}

rm -f "$JJ_MAIN_SOURCE"
