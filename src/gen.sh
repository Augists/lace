#!/bin/bash
bash lace.sh 10 64 > lace.h
bash lace.sh 10 32 > lace32.h
bash lace.sh 14 128 > lace128.h
sed "s:lace\.h:lace32\.h:g" lace.c > lace32.c
sed "s:lace\.h:lace128\.h:g" lace.c > lace128.c
