BINARY  := basl
CMD     := ./cmd/basl
PYTHON  ?= python3

.PHONY: all build fmt vet test regression thread-race cover prof-cpu prof-mem clean

all: build

build: fmt vet
	go build -o $(BINARY) $(CMD)

fmt:
	gofmt -w .

vet:
	go vet ./...

test: build
	go test ./... -count=1
	go test -race ./pkg/basl/interp -run 'TestThread|TestMutex' -count=1
	$(PYTHON) integration_tests/test_syntax_integration.py
	$(PYTHON) integration_tests/test_debugger.py

regression: build
	$(PYTHON) integration_tests/test_syntax_integration.py

thread-race: build
	go test -race ./pkg/basl/interp -run 'TestThread|TestMutex' -count=1

cover: fmt vet
	go test ./... -coverprofile=coverage.out
	go tool cover -func=coverage.out
	@rm -f coverage.out

prof-cpu:
	go test ./pkg/basl/interp/ -bench=. -cpuprofile=cpu.prof -benchmem
	go tool pprof -top cpu.prof

prof-mem:
	go test ./pkg/basl/interp/ -bench=. -memprofile=mem.prof -benchmem
	go tool pprof -top -alloc_space mem.prof

clean:
	rm -f $(BINARY) cpu.prof mem.prof interp.test
