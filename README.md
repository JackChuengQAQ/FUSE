# FUSE

This repository contains the code for the SIGMOD-submitted paper, **"Topology Meets Semantics: Top-k Subgraph Matching With Vector Similarity"**.

FUSE performs top-k subgraph matching on knowledge graphs by combining topology-based structural constraints with vector similarity over node and relation embeddings. Given a query pattern and a knowledge graph where every node and relation carries a dense embedding, FUSE returns the k subgraph matches whose aggregate embedding similarity is highest.

## Prerequisites

- **OS**: Linux (uses `fork`, `/proc/self/status`, `sys/prctl.h`)
- **Compiler**: GCC or Clang with C++17 support
- **CMake**: >= 3.10
- **OpenMP**: required for parallel candidate initialization
- **Cereal** (header-only): required only for the CFLfs baseline (`cereal/archives/json.hpp`)

## Repository Structure

```
Fuse/                    # Main FUSE implementation
  main.cpp               # Entry point and query pipeline
  cover_refinement.*     # Vertex-cover-based candidate initialization and refinement
  upper_bound_pruning.*  # DAG-based upper-bound computation and pruning
  global_search.*        # Branch-and-bound top-k search
  newvamana.*            # Vamana approximate nearest neighbor index
  graph/new_graph.*      # Knowledge graph loader (CSR with embeddings)

baselines/
  CFLfs/                 # Constraint-based full-structure search baseline
  SimGRAG/               # Naive and greedy similarity matching baseline
  PTAB/                  # Path-based top-k subgraph matching baseline
```

## Building

### FUSE

```bash
cd Fuse
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

This produces the `fuse_query` executable.

### Baselines

Each baseline under `baselines/` (CFLfs, SimGRAG, PTAB) can be built independently. CFLfs and SimGRAG use CMake (same workflow as above); PTAB can be compiled directly with `g++`. See the source files in each subdirectory for details.

## Input Data Format

The knowledge graph directory must contain one of two formats:

### Compact format (recommended)

```
KG_DIR/
  stats.txt               # "node_num <N>\nrelation_num <R>\nedge_num <E>"
  node_names.txt           # One node name per line (line i = node i)
  relation_names.txt       # One relation name per line (line i = relation i)
  edges.txt                # One "src_id dst_id relation_id" triple per line
  node_embeddings.bin      # Binary float32 matrix, N x D (row-major)
  relation_embeddings.bin  # Binary float32 matrix, R x D (row-major)
```

### FactKG format

```
KG_DIR/
  FactKG.graph.txt         # Graph structure file
  FactKG.type_to_nodes.txt # Type-to-node mapping
  relation_name_to_id.txt  # "relation_name\tid" per line
  type_name_to_id.txt      # "type_name\tid" per line
  node_name_to_id/         # Directory of node name mapping shards
  node_embeddings/         # Directory of embedding shards
  relation_embeddings.bin  # Binary float32 matrix
  type_embeddings.bin      # Binary float32 matrix
```

### Query format

Queries are organized as numbered subdirectories:

```
QUERIES_ROOT/
  0/
    edges.txt              # "src_id dst_id relation_id" per line
    stats.txt              # Query metadata
  1/
    ...
```

A Vamana ANN index is built automatically on first run and cached at `KG_DIR/index/node_vamana/`.

## Running

### FUSE

```bash
./fuse_query KG_DIR QUERIES_ROOT OUTPUT_ROOT [K] [N_LIST] [UNDIRECTED] [START] [END] [DEBUG] [TIMEOUT_SEC]
```

| Argument | Default | Description |
|---|---|---|
| `KG_DIR` | (required) | Path to knowledge graph directory |
| `QUERIES_ROOT` | (required) | Path to query directory |
| `OUTPUT_ROOT` | (required) | Path for result output |
| `K` | 3 | Number of top-k results to return |
| `N_LIST` | 4096 | Candidate budget per node (comma-separated for sweep, e.g. `1024,2048,4096`) |
| `UNDIRECTED` | 1 | 1 = undirected matching, 0 = directed |
| `START` | -1 | Start query ID (-1 = all) |
| `END` | -1 | End query ID (-1 = all) |
| `DEBUG` | 0 | 1 = verbose debug output |
| `TIMEOUT_SEC` | 3600 | Per-query timeout in seconds |

Example:

```bash
./fuse_query data/graph data/queries results/ 3 4096 1
```

### CFLfs

```bash
./cflfs_query -d KG_DIR -batch_dir QUERIES_ROOT -topk 3 [-vlmatch none] [-time_limit 3600]
```

Key options: `-filter`, `-order`, `-engine` for algorithm variants, `-vlmatch none|intersection` for vertex label matching mode.

### SimGRAG

```bash
./simgrag_query KG_DIR QUERIES_ROOT OUTPUT_ROOT [UNDIRECTED] [K_LIST] [NODE_SIM_TOPK_LIST] [MODES] [START] [END] [TIMEOUT_SEC]
```

| Argument | Default | Description |
|---|---|---|
| `UNDIRECTED` | 1 | 1 = undirected, 0 = directed |
| `K_LIST` | 3 | Comma-separated list of k values |
| `NODE_SIM_TOPK_LIST` | 128,256,512,1024,2048 | Per-node similarity budget |
| `MODES` | greedy,naive | Comma-separated: `greedy`, `naive`, or both |

### PTAB

```bash
./ptab_query DB_NAME QUERY_BASE OUT_BASE [options]
```

Options: `--sizes 4,8,12`, `--method PTAB`, `--topk 3`, `--start-id 1`, `--end-id 100`, `--time-limit 3600`, `--metric l2|squared-l2|dot`, `--mem-limit-gib 100`.

## Output

FUSE writes the following to `OUTPUT_ROOT`:

- `progress.tsv` -- per-query status (ok / timeout / error)
- `timing.json` -- aggregate timing breakdown
- `<query_name>.txt` -- top-k results, one match per line:
  ```
  SCORE|src_id---rel_id---dst_id|src_name---rel_name---dst_name
  ```
- `_query_timing/<query_name>.json` -- detailed per-query timing and statistics
