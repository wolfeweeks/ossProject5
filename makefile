all: .PHONY oss user_proc

.PHONY: clean

oss: oss.c
	gcc -o oss oss.c shared_memory.h shared_memory.c

user_proc:
	gcc -o user_proc user_proc.c shared_memory.h shared_memory.c

clean:
	rm -f oss user_proc