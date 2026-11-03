#!/bin/zsh
# Screenshot probe configurations: ./shotrun.sh name "iso args" [name "args" ...]
# Starts probe.exe (visible) with t=6, grabs the 86Box window after 4 s, waits for exit.
# Holds the VM lock for the batch. PNGs go to shots/<name>.png
S=~/Documents/86box/samba/vmb   # VM B wrappers (this project's VM, docs/TASKS_M2.md)
P='M:\dev\lahio\probe\probe.exe'
L=86box-86-probe
D=${0:A:h}
$S/vmlock wait $L "lahio probe screenshots" 1800 || exit 1
while (( $# >= 2 )); do
  name=$1; args=$2; shift 2
  $S/vmlock touch $L >/dev/null
  $S/w98 start "$P $args t=6" >/dev/null
  sleep 6
  $S/shot86.sh $D/shots/$name.png
  sleep 6
  echo "shot $name"
done
$S/vmlock release $L
