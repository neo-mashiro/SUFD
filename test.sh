#!/bin/bash
for i in {1..32}
do
   nc -d localhost 9000 &
   sleep 1
done
