#!/bin/bash

#qsub -I -l nodes=2:ppn=1 -d `pwd`
#aprun -N1 -n2 hostname - to retrieve hostnames
#first arg to script is the node on which the server run;
#second arg is the node where the client needs to connect to
#i.e. the server node itself
#aprun -N1 -n2 ./launchtransfer.sh nid00008 nid00008 /tmp/zfile

h=`hostname`
if [ $h = $1 ]; then
  ./hwserver
else
  filename=$3
  servernode=$2
  port=555 	
  ./hwclient $servernode $port $filename
fi
