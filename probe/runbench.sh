#!/bin/zsh
# Run probe.exe configurations in the Win98 VM, one per line on stdin (or args).
# Holds the VM lock (session 86box-86-probe) for the batch. Results append to RESULT.TXT.
# usage: ./runbench.sh "iso res=800 tile=64" ...    or   ./runbench.sh < list.txt
S=~/Documents/86box/samba/vmb   # VM B wrappers (this project's VM, docs/TASKS_M2.md)
P='M:\dev\lahio\probe\probe.exe'
L=86box-86-probe
if (( $# )); then tests=("$@"); else tests=("${(@f)$(cat)}"); fi
$S/vmlock wait $L "lahio render probe" 1800 || exit 1
for t in $tests; do
  [[ -z $t || $t == \#* ]] && continue
  $S/vmlock touch $L >/dev/null
  echo ">> $t"
  $S/w98 run "$P $t" --show -t 400 2>&1 | tail -2
  sleep 1
done
$S/vmlock release $L
