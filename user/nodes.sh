#!/bin/bash
name="nodo"
major=$1
n=$(($2-1))

rm -f $name*

for i in $(seq 0 $n);
do
   	mknod $name$i c $major $i
done
