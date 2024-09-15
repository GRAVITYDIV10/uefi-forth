#!/usr/bin/env bash

while read line
do
	llvm-objdump -T --disassemble-symbols=$line ${1}
done
