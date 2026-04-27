SHELL:=bash
.SHELLFLAGS:=-eu -o pipefail -c
.ONESHELL:=true
.DELETE_ON_ERROR:=true

all: compile

build:; mkdir -p build
cmake: build; cd build && cmake ..
compile: cmake; make -C build && make pi_install
clean:; [ -d build ] && make -C build clean || true
distclean: ; rm -rf build

# Pi integration
pi_install:
	@echo "Installing/Updating Pi Package..."
	pi install $(PWD)/pi_integration

# Test targets
test: compile; cd build && ctest --output-on-failure

# Component specific tests
test_listree: compile; ./build/listree/listree_tests
test_edict: compile; ./build/edict/edict_tests
test_cartographer: compile; ./build/cartographer/cartographer_tests
test_kanren: compile; ./build/kanren/kanren_tests

# Golden reference for edict demo
edict_golden: compile
	./scripts/edict_demo.sh > doc/edict-golden.txt
	@echo "Updated doc/edict-golden.txt"

# Visualization demo
visualize_demo: compile
	./build/cartographer/agentc_visualize demo.h demo.dot
	dot -Tpng demo.dot -o demo.png
	@echo "Generated demo.png from demo.h"

# Run REPL (M10 fix: was referencing non-existent build/interpreter/j3interpreter;
# the actual REPL binary is produced by the edict/ subdirectory target)
repl: compile; ./build/edict/edict

# Utilities
debug: compile; cp build/edict/edict_tests ./a.out
test_debug: debug; ./a.out

# Design documentation (on-demand — does not run on every build)
# Requires: python3, ctags (universal-ctags), graphviz (dot)
# Output: design/index.html  (L1 system overview)
#         design/components/ (L2 per-component pages)
#         design/functions/  (L3 per-function call trees)
gendocs:
	@echo "Generating design documentation..."
	python3 scripts/gendocs.py --root . --out design
	@echo "Open design/index.html in a browser."

.PHONY: all build cmake compile clean distclean test test_listree test_edict test_cartographer test_kanren visualize_demo repl debug test_debug gendocs
