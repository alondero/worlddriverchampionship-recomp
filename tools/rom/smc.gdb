set pagination off
set print thread-events off
break func_8002703C
run
dump binary memory build/runtime-text.bin rdram+0x400 rdram+0x7c6b0
quit
