build:
	cc main.c -o main
clean:
	rm -rf main
run: clean build
	./main
