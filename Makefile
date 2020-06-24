SHELL := /bin/bash
CC		=  gcc
CFLAGS  = -lpthread -Wall

.PHONY: test clean

all: compile
	
compile: jetmarket.c
	$(CC) jetmarket.c -o jetmarket.o $(CFLAGS)

test:
	@sleep 1s;
ifeq (./jetmarket.PID,$(wildcard ./jetmarket.PID))
	    @rm -f ./jetmarket.o ./logfile.log jetmarket.PID; #If there is already the .PID , clean and then execute
			@sleep 2s;
endif
ifeq (,$(wildcard ./jetmarket))
		@$(CC) jetmarket.c -o jetmarket.o $(CFLAGS) #If there is no object file, compile it
endif
	@echo "Executing (25 sec)"
	@(./jetmarket.o test/test2.txt & echo $$! >jetmarket.PID) & (number=0 ; while [[ $$number -le 25 ]] ; do \
        printf "."; \
        ((number = number + 1)) ; \
				sleep 1s; \
				done); \
				kill -1 $$(cat jetmarket.PID); \
				chmod +x ./script.sh            #rendi eseguibile lo script
				@./script.sh $$(cat jetmarket.PID); \

clean:
	rm -f ./jetmarket.o ./logfile.log jetmarket.PID
