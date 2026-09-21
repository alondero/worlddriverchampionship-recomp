set pagination off
set print thread-events off
run
bt 12
frame 0
info args
print/x *ctx
