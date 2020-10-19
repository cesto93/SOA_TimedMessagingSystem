#!/bin/bash
name="nodo"
major=$1
n=$(($2-1))

for i in $(seq 0 $n);
do
	rm -f $name$i
   	mknod $name$i c $major $i
done
