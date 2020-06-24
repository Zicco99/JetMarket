#!/bin/bash
printf "\n"
while [ -e /proc/$1 ] #jetmarket.PID , loop until process is closed
do
    sleep 1s
done
if [ -f "logfile.log" ]; then
    while read line; do echo $line; done < logfile.log
else
    printf "logfile.log not found\n"
fi
