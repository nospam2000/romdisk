#!/bin/bash

if [ -z "${FLAVOR}" ] ; then
#export FLAVOR=_dbg
export FLAVOR=_rel
fi

make clean; make
