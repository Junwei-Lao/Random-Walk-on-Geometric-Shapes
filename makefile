# Random Walk on Geometric Shapes -- build + run driver.
#
# Every simulation binary reads its shape/mode/index-range/etc. from CLI
# flags (see src/common/cli_args.h); nothing is hard-coded in the .cpp
# files. This Makefile is the single place that lists what to run --
# override any variable on the command line, e.g.:
#
#   make run-2d SHAPE=HEXAGON MODE=soft TEMPERATURE=1.5 INDEX_MIN=20 INDEX_MAX=100
#   make run-2d SHAPE=SQUARE MODE=drag DRAG_FORCE=500
#   make run-2d SHAPE=SNOWFLAKE COVER=1 RHO=0.02
#   make run-3d SHAPE3D=PYRAMID INDEX_MIN=10 INDEX_MAX=60
#   make run-2d-hull HULLSHAPE2D=HEXAGON
#   make run-3d-hull HULLSHAPE3D=OCTAHEDRON
#
# To sweep multiple shapes in one command, use the plural SHAPES variable
# (space-separated -- NOT comma-separated -- each run-* target loops the
# binary once per shape) instead of SHAPE:
#
#   make run-2d SHAPES="SQUARE CIRCLE HEXAGON"
#   make run-2d SHAPES=ALL              # every 2D shape
#   make run-2d 'SHAPES=ALL\SNOWFLAKE'  # every 2D shape except SNOWFLAKE
#   make run-2d 'SHAPES=ALL\SNOWFLAKE\BOWTIE'  # chain \NAME to exclude more
#   make run-3d SHAPES3D=ALL            # every 3D shape
#   make run-2d-hull HULLSHAPES2D=ALL
#   make run-3d-hull HULLSHAPES3D=ALL
#
# Run `make help` for the full variable/target list, or `make list-shapes-2d`
# (etc.) to see valid --shape values without reading any source file.

CXX      := g++
CXXSTD   := -std=c++17
CXXFLAGS := $(CXXSTD) -O2 -Wall -Wextra -pthread
CGAL_LIBS := -lgmp -lmpfr

OBJDIR := obj
BIN   := bin

# ---- Run parameters: override on the command line ----
SHAPE               ?= SHARP_TRIANGLE    # convex_2d shape name (make list-shapes-2d)
SHAPE3D             ?= CUBE      # convex_3d shape name (make list-shapes-3d)
HULLSHAPE2D         ?= SQUARE      # convex_2d_convexhull shape name (make list-shapes-2d-hull)
HULLSHAPE3D         ?= CUBE        # convex_3d_convexhull shape name (make list-shapes-3d-hull)
# Space-separated shape lists (NOT comma-separated) -- when set, these run
# the same sweep once per shape instead of the single SHAPE/SHAPE3D/etc.
# above, e.g. make run-2d SHAPES="SQUARE CIRCLE HEXAGON". Any variable that
# takes ALL also takes ALL\NAME\NAME... to drop shapes (case-insensitive):
#   make run-2d 'SHAPES=ALL\SNOWFLAKE'
SHAPES              ?= ALL\SNOWFLAKE
SHAPES3D            ?= ALL
HULLSHAPES2D        ?=
HULLSHAPES3D        ?=
MODE                ?= hard        # hard | soft | drag (drag requires SHAPE=SQUARE)
MODE3D              ?= hard        # run-3d: hard | soft (soft uses TEMPERATURE(S)/DISTANCE_POWER too)
INDEX_MIN           ?= 10
INDEX_MAX           ?= 90
INDEX_STEP          ?= 1
RUNS                ?= 1000
THREADS             ?= 0           # 0 = auto-detect hardware concurrency
OUTDIR              ?=             # empty = each binary's own default (results_2d, ...)
SEED                ?=             # empty = random seed
RECORD_PATH         ?= 1           # 1 = dump one sample walker path per index (analytic engines)
RECORD_DISTRIBUTION ?= 0           # 1 = write distribution_*.csv per index; 0 to skip (all engines)
RECORD_COVER        ?= 1           # 1 = with COVER=1, dump cover_*.txt (radius, then disk centers) per index
TEMPERATURE         ?= 0.5         # soft mode (doc section 1.8)
# Space-separated soft-mode temperature sweep; when set, overrides TEMPERATURE
# and maps out T_mix(n, T) over the whole INDEX x TEMPERATURES grid, e.g.
#   make run-2d MODE=soft SHAPE=SQUARE TEMPERATURES="0.1 0.25 0.5 1 2 8"
TEMPERATURES        ?= 0.1 0.25 0.5 1 2 8
DISTANCE_POWER      ?= 2.0         # soft mode
DRAG_FORCE          ?= 0           # drag mode, 0..10000 (doc section 1.9)
COVER               ?= 1           # 1 = enable disk-covering analysis (doc section 1.10)
RHO                 ?= 0.2         # covering radius R(n) = RHO * n
# Space-separated covering-radius sweep; when set (and COVER=1), overrides RHO
# and runs every INDEX [x TEMPERATURES] x RHOS grid point, e.g.
#   make run-2d SHAPES=SQUARE COVER=1 RHOS="0.05 0.1 0.2"
RHOS                ?= 0.1 0.25 0.5
COVERAGE_FRACTION_2D ?= 0.5        # hull engine stopping fraction (2D doc default)
COVERAGE_FRACTION_3D ?= 0.5        # hull engine stopping fraction (3D doc default)
INDEX               ?= 50          # mask-2d index
INDEX3D             ?= 20          # mask-3d index
SHELL_ONLY          ?= 1           # mask tools: 1 = boundary points only
PERIMETER_SHAPE     ?= ALL         # perimeter-2d: shape name(s), ALL (default), or ALL\NAME

seed_flag = $(if $(SEED),--seed=$(SEED))
outdir_flag = $(if $(OUTDIR),--outdir=$(OUTDIR))
comma := ,
space := $(subst ,, )
# $(call resolve_shapes,LIST,BINARY) -> space-separated shape names.
# LIST is either explicit names ("SQUARE CIRCLE"), ALL, or ALL followed by
# \NAME exclusions (ALL\SNOWFLAKE\BOWTIE; matched case-insensitively against
# BINARY --list-shapes). An unknown excluded name is an error, so a typo
# can't silently leave the shape in. Quote the value on the shell command
# line ('SHAPES=ALL\SNOWFLAKE'): an unquoted backslash is eaten by the shell.
# Recipes are expanded after their prerequisites are built, so BINARY exists.
resolve_shapes = $(strip $(if $(filter ALL,$(firstword $(subst \, ,$(1)))),$(call all_except,$(wordlist 2,999,$(subst \, ,$(1))),$(2)),$(1)))
all_except = $(foreach x,$(1),$(if $(shell $(2) --list-shapes | grep -ixF -e '$(x)'),,$(error Unknown shape '$(x)' in ALL\ exclusion; see $(2) --list-shapes)))$(shell $(2) --list-shapes $(if $(1),| grep -vixF $(foreach x,$(1),-e '$(x)')))
temperatures_flag = $(if $(strip $(TEMPERATURES)),--temperatures=$(subst $(space),$(comma),$(strip $(TEMPERATURES))))
rhos_flag = $(if $(strip $(RHOS)),--rhos=$(subst $(space),$(comma),$(strip $(RHOS))))

.PHONY: all build clean help \
        run-2d run-3d run-2d-hull run-3d-hull \
        list-shapes-2d list-shapes-3d list-shapes-2d-hull list-shapes-3d-hull \
        mask-2d mask-3d perimeter-2d

.DEFAULT_GOAL := help

all: build

build: $(BIN)/walk2d $(BIN)/walk3d $(BIN)/hull2d $(BIN)/hull3d $(BIN)/mask2d $(BIN)/mask3d $(BIN)/perimeter2d

$(BIN) $(OBJDIR):
	mkdir -p $@

# ---------------- 2D analytic engine (hard/soft/drag walks, covering) ----------------
WALK2D_SRC := $(wildcard src/convex_2d/*.cpp)
WALK2D_OBJ := $(patsubst src/convex_2d/%.cpp,$(OBJDIR)/convex_2d/%.o,$(WALK2D_SRC))

$(OBJDIR)/convex_2d/%.o: src/convex_2d/%.cpp src/convex_2d/*.h src/common/cli_args.h | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/walk2d: $(WALK2D_OBJ) | $(BIN)
	$(CXX) $(CXXFLAGS) $(WALK2D_OBJ) -o $@

# ---------------- 3D analytic engine (hard-boundary walk only, per doc) ----------------
WALK3D_SRC := $(wildcard src/convex_3d/*.cpp)
WALK3D_OBJ := $(patsubst src/convex_3d/%.cpp,$(OBJDIR)/convex_3d/%.o,$(WALK3D_SRC))

$(OBJDIR)/convex_3d/%.o: src/convex_3d/%.cpp src/convex_3d/*.h src/common/cli_args.h | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/walk3d: $(WALK3D_OBJ) | $(BIN)
	$(CXX) $(CXXFLAGS) $(WALK3D_OBJ) -o $@

# ---------------- 2D convex-hull engine (CGAL) ----------------
HULL2D_SRC := $(wildcard src/convex_2d_convexhull/*.cpp)
HULL2D_OBJ := $(patsubst src/convex_2d_convexhull/%.cpp,$(OBJDIR)/convex_2d_convexhull/%.o,$(HULL2D_SRC))

$(OBJDIR)/convex_2d_convexhull/%.o: src/convex_2d_convexhull/%.cpp src/convex_2d_convexhull/*.h src/common/cli_args.h | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/hull2d: $(HULL2D_OBJ) | $(BIN)
	$(CXX) $(CXXFLAGS) $(HULL2D_OBJ) $(CGAL_LIBS) -o $@

# ---------------- 3D convex-hull engine (CGAL) ----------------
HULL3D_SRC := $(wildcard src/convex_3d_convexhull/*.cpp)
HULL3D_OBJ := $(patsubst src/convex_3d_convexhull/%.cpp,$(OBJDIR)/convex_3d_convexhull/%.o,$(HULL3D_SRC))

$(OBJDIR)/convex_3d_convexhull/%.o: src/convex_3d_convexhull/%.cpp src/convex_3d_convexhull/*.h src/common/cli_args.h | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/hull3d: $(HULL3D_OBJ) | $(BIN)
	$(CXX) $(CXXFLAGS) $(HULL3D_OBJ) $(CGAL_LIBS) -o $@

# ---------------- Mask-dump tools (visualization/debugging) ----------------
$(BIN)/mask2d: tools/generate_mask_2d.cpp src/convex_2d/shapes.cpp src/convex_2d/shapes.h src/convex_2d/hash.h src/common/cli_args.h | $(BIN)
	$(CXX) $(CXXFLAGS) tools/generate_mask_2d.cpp src/convex_2d/shapes.cpp -o $@

$(BIN)/mask3d: tools/generate_mask_3d.cpp src/convex_3d/shapes.cpp src/convex_3d/shapes.h src/convex_3d/hash.h src/common/cli_args.h | $(BIN)
	$(CXX) $(CXXFLAGS) tools/generate_mask_3d.cpp src/convex_3d/shapes.cpp -o $@

# ---------------- Isoperimetric analysis tool ----------------
$(BIN)/perimeter2d: tools/compute_perimeter_2d.cpp src/convex_2d/shapes.cpp src/convex_2d/shapes.h src/convex_2d/hash.h src/common/cli_args.h | $(BIN)
	$(CXX) $(CXXFLAGS) tools/compute_perimeter_2d.cpp src/convex_2d/shapes.cpp -o $@

# ---------------- Run targets ----------------
run-2d: $(BIN)/walk2d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(SHAPES),$(SHAPES),$(SHAPE)),./$(BIN)/walk2d)"; \
	for s in $$shapes; do \
	  echo "=== SHAPE=$$s ==="; \
	  ./$(BIN)/walk2d --shape=$$s --mode=$(MODE) \
	    --index-min=$(INDEX_MIN) --index-max=$(INDEX_MAX) --index-step=$(INDEX_STEP) \
	    --runs=$(RUNS) --threads=$(THREADS) \
	    --temperature=$(TEMPERATURE) --distance-power=$(DISTANCE_POWER) --drag-force=$(DRAG_FORCE) \
	    --cover=$(COVER) --rho=$(RHO) --record-path=$(RECORD_PATH) --record-distribution=$(RECORD_DISTRIBUTION) \
	    --record-cover=$(RECORD_COVER) $(temperatures_flag) $(rhos_flag) $(outdir_flag) $(seed_flag); \
	done

run-3d: $(BIN)/walk3d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(SHAPES3D),$(SHAPES3D),$(SHAPE3D)),./$(BIN)/walk3d)"; \
	for s in $$shapes; do \
	  echo "=== SHAPE3D=$$s ==="; \
	  ./$(BIN)/walk3d --shape=$$s --mode=$(MODE3D) \
	    --index-min=$(INDEX_MIN) --index-max=$(INDEX_MAX) --index-step=$(INDEX_STEP) \
	    --runs=$(RUNS) --threads=$(THREADS) \
	    --temperature=$(TEMPERATURE) --distance-power=$(DISTANCE_POWER) \
	    --record-path=$(RECORD_PATH) --record-distribution=$(RECORD_DISTRIBUTION) \
	    $(temperatures_flag) $(outdir_flag) $(seed_flag); \
	done

run-2d-hull: $(BIN)/hull2d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(HULLSHAPES2D),$(HULLSHAPES2D),$(HULLSHAPE2D)),./$(BIN)/hull2d)"; \
	for s in $$shapes; do \
	  echo "=== HULLSHAPE2D=$$s ==="; \
	  ./$(BIN)/hull2d --shape=$$s \
	    --index-min=$(INDEX_MIN) --index-max=$(INDEX_MAX) --index-step=$(INDEX_STEP) \
	    --runs=$(RUNS) --threads=$(THREADS) --coverage-fraction=$(COVERAGE_FRACTION_2D) \
	    --record-distribution=$(RECORD_DISTRIBUTION) $(outdir_flag) $(seed_flag); \
	done

run-3d-hull: $(BIN)/hull3d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(HULLSHAPES3D),$(HULLSHAPES3D),$(HULLSHAPE3D)),./$(BIN)/hull3d)"; \
	for s in $$shapes; do \
	  echo "=== HULLSHAPE3D=$$s ==="; \
	  ./$(BIN)/hull3d --shape=$$s \
	    --index-min=$(INDEX_MIN) --index-max=$(INDEX_MAX) --index-step=$(INDEX_STEP) \
	    --runs=$(RUNS) --threads=$(THREADS) --coverage-fraction=$(COVERAGE_FRACTION_3D) \
	    --record-distribution=$(RECORD_DISTRIBUTION) $(outdir_flag) $(seed_flag); \
	done

list-shapes-2d: $(BIN)/walk2d
	./$(BIN)/walk2d --list-shapes

list-shapes-3d: $(BIN)/walk3d
	./$(BIN)/walk3d --list-shapes

list-shapes-2d-hull: $(BIN)/hull2d
	./$(BIN)/hull2d --list-shapes

list-shapes-3d-hull: $(BIN)/hull3d
	./$(BIN)/hull3d --list-shapes

mask-2d: $(BIN)/mask2d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(SHAPES),$(SHAPES),$(SHAPE)),./$(BIN)/mask2d)"; \
	for s in $$shapes; do \
	  ./$(BIN)/mask2d --shape=$$s --index=$(INDEX) --shell=$(SHELL_ONLY) $(outdir_flag); \
	done

mask-3d: $(BIN)/mask3d
	@set -e; \
	shapes="$(call resolve_shapes,$(if $(SHAPES3D),$(SHAPES3D),$(SHAPE3D)),./$(BIN)/mask3d)"; \
	for s in $$shapes; do \
	  ./$(BIN)/mask3d --shape=$$s --index=$(INDEX3D) --shell=$(SHELL_ONLY) $(outdir_flag); \
	done

# Isoperimetric ratio P/sqrt(A) for every 2D shape in one CSV (data/isoperimetric_2d.csv
# by default). This ratio is scale-invariant (see tools/compute_perimeter_2d.cpp) and the
# binary computes every shape in one process (no bash loop needed), so PERIMETER_SHAPE
# (default ALL) is its own variable rather than reusing SHAPE/SHAPES's SQUARE default.
perimeter-2d: $(BIN)/perimeter2d
	./$(BIN)/perimeter2d --shape=$(subst $(space),$(comma),$(call resolve_shapes,$(PERIMETER_SHAPE),./$(BIN)/perimeter2d)) --index=$(INDEX) $(outdir_flag)

clean:
	rm -rf $(OBJDIR) $(BIN)

help:
	@echo "Random Walk on Geometric Shapes"
	@echo ""
	@echo "Build targets:"
	@echo "  make build              Build all 4 engines + 2 mask tools into bin/"
	@echo "  make clean              Remove build/ and bin/"
	@echo ""
	@echo "Run targets (each builds its binary first if needed):"
	@echo "  make run-2d             2D analytic engine: hard/soft/drag walks, covering"
	@echo "  make run-3d             3D analytic engine: hard-boundary walk"
	@echo "  make run-2d-hull        2D convex-hull engine (CGAL)"
	@echo "  make run-3d-hull        3D convex-hull engine (CGAL)"
	@echo "  make mask-2d / mask-3d  Dump a shape's interior lattice points to a text file"
	@echo "  make perimeter-2d       Isoperimetric ratio P/sqrt(A) for every 2D shape (one CSV)"
	@echo "  make list-shapes-2d / list-shapes-3d / list-shapes-2d-hull / list-shapes-3d-hull"
	@echo ""
	@echo "Key variables (override on the command line):"
	@echo "  SHAPE=$(SHAPE) SHAPE3D=$(SHAPE3D) HULLSHAPE2D=$(HULLSHAPE2D) HULLSHAPE3D=$(HULLSHAPE3D)"
	@echo "  SHAPES/SHAPES3D/HULLSHAPES2D/HULLSHAPES3D  space-separated list (or ALL) to sweep"
	@echo "                             multiple shapes in one run-*/mask-* invocation instead of SHAPE"
	@echo "                             ALL\\NAME\\NAME... = every shape except those (quote it in the shell)"
	@echo "  MODE=hard|soft|drag        (run-2d only; drag requires SHAPE=SQUARE)"
	@echo "  MODE3D=hard|soft           (run-3d only)"
	@echo "  INDEX_MIN/INDEX_MAX/INDEX_STEP/RUNS/THREADS/OUTDIR/SEED"
	@echo "  TEMPERATURE(S)/DISTANCE_POWER (soft mode, 2D and 3D)   DRAG_FORCE (drag mode)"
	@echo "  COVER=1 RHO=... / RHOS=\"...\" (disk-covering analysis, run-2d only)"
	@echo "  RECORD_COVER=0|1           With COVER=1, dump cover_*.txt (radius + disk centers) per index"
	@echo "  COVERAGE_FRACTION_2D/COVERAGE_FRACTION_3D (hull engines)"
	@echo "  RECORD_PATH=0|1            (analytic engines only; default 1)"
	@echo "  RECORD_DISTRIBUTION=0|1    Skip writing distribution_*.csv per index (default 1)"
	@echo ""
	@echo "Examples:"
	@echo "  make run-2d SHAPE=HEXAGON MODE=soft TEMPERATURE=1.5 INDEX_MIN=20 INDEX_MAX=100"
	@echo "  make run-2d SHAPE=SQUARE MODE=drag DRAG_FORCE=500"
	@echo "  make run-2d SHAPE=SNOWFLAKE COVER=1 RHO=0.02"
	@echo "  make run-2d SHAPES=SQUARE MODE=soft TEMPERATURES=\"0.25 0.5 1 2\""
	@echo "  make run-2d SHAPES=SQUARE COVER=1 RHOS=\"0.05 0.1 0.2\""
	@echo "  make run-3d SHAPES3D=CUBE MODE3D=soft TEMPERATURES=\"0.5 1 2\""
	@echo "  make run-2d SHAPES=\"SQUARE CIRCLE HEXAGON\""
	@echo "  make run-2d SHAPES=ALL"
	@echo "  make run-2d 'SHAPES=ALL\\SNOWFLAKE'           # every 2D shape except SNOWFLAKE"
	@echo "  make run-3d SHAPE3D=PYRAMID INDEX_MIN=10 INDEX_MAX=60"
	@echo "  make run-2d-hull HULLSHAPE2D=HEXAGON"
	@echo "  make run-3d-hull HULLSHAPE3D=OCTAHEDRON"
	@echo "  make mask-2d SHAPES=ALL SHELL_ONLY=1        # dump every 2D shape's boundary points"
	@echo "  make mask-3d SHAPES3D=ALL SHELL_ONLY=1       # dump every 3D shape's boundary points"
	@echo "  make perimeter-2d                            # isoperimetric_2d.csv for every 2D shape"
	@echo "  make run-2d SHAPES=ALL RECORD_DISTRIBUTION=0 RECORD_PATH=0   # summary CSV only, no per-run dumps"
